#include <WiFi.h> 
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <esp_sleep.h>
#include <time.h>

// --- Konfigurasi WiFi ---
const char* ssid = "Al Barokah";
const char* password = "1juni2023";

// --- Konfigurasi Server ---
const char* serverName = "http://82.29.165.106:5000/api/location";
const char* confirmUrl = "http://82.29.165.106:5000/api/confirm_connection";
const char* statusUrl = "http://82.29.165.106:5000/api/check_connection_status/CHILD001";

// --- ID unik perangkat anak ---
String device_id = "CHILD001";

// --- GPS: UART1 (D4 = GPIO4 = RX, D2 = GPIO2 = TX) ---
TinyGPSPlus gps;
HardwareSerial gpsSerial(1); // UART1

// --- GSM: UART2 (GPIO16 = RX, GPIO17 = TX) ---
HardwareSerial gsmSerial(2); // UART2

// --- NTP Time Config ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // UTC+7
const int daylightOffset_sec = 0;

// --- Status Flags ---
bool wifiConnected = false;
bool isConnected = false;
bool gpsSignalFound = false;
bool gsmReady = false;
bool simReady = false;
int gsmSignalStrength = -1;
String lastConnectionStatus = "unknown";

unsigned long lastGpsCheck = 0;
unsigned long lastDataSent = 0;
unsigned long lastStatusCheck = 0;

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n=== SafeZoneX Debug Mode ===");
  Serial.println("GPS Neo-6M di pin D4 (RX) dan D2 (TX)");

  // GPS UART: D4 = GPIO4 (RX), D2 = GPIO2 (TX)
  gpsSerial.begin(9600, SERIAL_8N1, 2, 4); // GPS UART
  gsmSerial.begin(9600, SERIAL_8N1, 16, 17); // GSM UART

  safeWiFiInit();

  if (wifiConnected) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    confirmConnection();
    checkConnectionStatus(); // Cek status koneksi saat startup
  }

  checkGsmStatus();

  Serial.println("Sistem siap. Menunggu data GPS...");
  Serial.println("Koneksi GPS:");
  Serial.println("- GPS RX (ke ESP32 TX) -> Pin D2 (GPIO2)");
  Serial.println("- GPS TX (ke ESP32 RX) -> Pin D4 (GPIO4)");
  Serial.println("- GPS VCC -> 3.3V");
  Serial.println("- GPS GND -> GND");
  Serial.println("--------------------------------");
  printDebugInfo();
}

void safeWiFiInit() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(1000);
  WiFi.setSleep(false);

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi terhubung!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("\nGagal terhubung ke WiFi!");
  }
}

void confirmConnection() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(confirmUrl)) return;
  http.addHeader("Content-Type", "application/json");
  String jsonPayload = "{\"device_id\":\"" + device_id + "\"}";
  int httpCode = http.POST(jsonPayload);
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    if (payload.indexOf("success") != -1) {
      isConnected = true;
      Serial.println("*** KONEKSI DITERIMA ***");
    }
  }
  http.end();
}

void checkConnectionStatus() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(statusUrl)) return;
  
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Parse JSON response sederhana
    String connectionStatus = "unknown";
    if (payload.indexOf("\"connection_status\":\"connected\"") != -1) {
      connectionStatus = "connected";
    } else if (payload.indexOf("\"connection_status\":\"disconnected\"") != -1) {
      connectionStatus = "disconnected";
    } else if (payload.indexOf("\"connection_status\":\"pending\"") != -1) {
      connectionStatus = "pending";
    } else if (payload.indexOf("\"connection_status\":\"not_found\"") != -1) {
      connectionStatus = "not_found";
    }
    
    // Cek apakah status berubah
    if (connectionStatus != lastConnectionStatus) {
      Serial.println("*** STATUS KONEKSI BERUBAH ***");
      if (connectionStatus == "connected") {
        Serial.println(">>> TERHUBUNG DENGAN PARENT <<<");
        isConnected = true;
      } else if (connectionStatus == "disconnected") {
        Serial.println(">>> KONEKSI DIPUTUSKAN OLEH PARENT <<<");
        isConnected = false;
      } else if (connectionStatus == "pending") {
        Serial.println(">>> MENUNGGU KONFIRMASI PARENT <<<");
        isConnected = false;
      } else if (connectionStatus == "not_found") {
        Serial.println(">>> PERANGKAT TIDAK TERDAFTAR <<<");
        isConnected = false;
      }
      lastConnectionStatus = connectionStatus;
    }
  } else {
    Serial.println("Gagal mengecek status koneksi");
  }
  
  http.end();
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Waktu tidak tersedia";
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void checkGsmStatus() {
  gsmReady = false;
  simReady = false;
  gsmSignalStrength = -1;

  gsmSerial.flush();
  delay(500);

  // Kirim AT
  gsmSerial.println("AT");
  delay(500);
  if (gsmSerial.find("OK")) {
    gsmReady = true;
  }

  // Cek SIM
  gsmSerial.println("AT+CPIN?");
  delay(500);
  if (gsmSerial.find("READY")) {
    simReady = true;
  }

  // Cek sinyal
  gsmSerial.println("AT+CSQ");
  delay(500);
  String response = "";
  while (gsmSerial.available()) {
    char c = gsmSerial.read();
    response += c;
  }

  int index = response.indexOf("+CSQ:");
  if (index != -1) {
    int commaIndex = response.indexOf(",", index);
    String rssiStr = response.substring(index + 6, commaIndex);
    gsmSignalStrength = rssiStr.toInt();
  }
}

void printDebugInfo() {
  Serial.println("=== Status Sistem ===");
  Serial.print("Waktu: "); Serial.println(getCurrentTimestamp());
  Serial.print("WiFi: "); Serial.println(wifiConnected ? "Terhubung" : "Tidak Terhubung");
  Serial.print("Server: "); Serial.println(isConnected ? "Terhubung" : "Tidak Terhubung");
  Serial.print("Status Koneksi: "); Serial.println(lastConnectionStatus);
  Serial.print("GPS: "); Serial.println(gpsSignalFound ? "Sinyal Ditemukan" : "Sinyal Tidak Ditemukan");
  if (gps.satellites.isValid()) {
    Serial.print("Satelit: "); Serial.println(gps.satellites.value());
  }
  if (gps.location.isValid()) {
    Serial.print("Lokasi: Lat="); Serial.print(gps.location.lat(), 6);
    Serial.print(", Lng="); Serial.println(gps.location.lng(), 6);
  }

  // Status GSM
  Serial.print("GSM Modul: "); Serial.println(gsmReady ? "Aktif" : "Tidak Aktif");
  Serial.print("SIM Card: "); Serial.println(simReady ? "Terdeteksi" : "Tidak Terdeteksi");
  Serial.print("Sinyal GSM: ");
  if (gsmSignalStrength >= 0) {
    Serial.print(gsmSignalStrength);
    Serial.println(" (0-31)");
  } else {
    Serial.println("Tidak Diketahui");
  }

  Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());
  Serial.println("=======================");
}

void loop() {
  // Baca data GPS
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    gps.encode(c);
  }

  // Cek GPS setiap 5 detik
  if (millis() - lastGpsCheck > 5000) {
    gpsSignalFound = gps.location.isValid();
    lastGpsCheck = millis();
    printDebugInfo();
    checkGsmStatus(); // update status GSM setiap 5 detik
  }

  // Cek status koneksi setiap 10 detik
  if (millis() - lastStatusCheck > 10000) {
    if (wifiConnected) {
      checkConnectionStatus();
    }
    lastStatusCheck = millis();
  }

  // Ketika GPS mendapat update lokasi
  if (gps.location.isUpdated()) {
    float lat = gps.location.lat();
    float lng = gps.location.lng();
    Serial.println("*** Lokasi Diperbarui ***");
    Serial.print("Lat: "); Serial.println(lat, 6);
    Serial.print("Lng: "); Serial.println(lng, 6);

    if (wifiConnected && isConnected) {
      sendDataViaWiFi(lat, lng);
    }
  }

  // Cek koneksi WiFi setiap 30 detik
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      isConnected = false;
      Serial.println("WiFi putus, mencoba reconnect...");
      safeWiFiInit();
    }
    if (wifiConnected && !isConnected) {
      confirmConnection();
    }
    lastCheckTime = millis();
  }

  yield();
  delay(100);
}

void sendDataViaWiFi(float latitude, float longitude) {
  if (!wifiConnected || !isConnected) return;

  Serial.println("\nMengirim data ke server...");
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(serverName)) return;

  http.addHeader("Content-Type", "application/json");
  String timestamp = getCurrentTimestamp();
  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + device_id + "\",";
  jsonPayload += "\"latitude\":" + String(latitude, 6) + ",";
  jsonPayload += "\"longitude\":" + String(longitude, 6) + ",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\"}";

  int httpCode = http.POST(jsonPayload);
  Serial.print("HTTP Response: "); Serial.println(httpCode);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Data berhasil dikirim.");
    lastDataSent = millis();
  } else {
    Serial.print("Gagal kirim: "); Serial.println(http.getString());
  }

  http.end();
}