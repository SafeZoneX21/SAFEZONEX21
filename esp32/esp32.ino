#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// --- Konfigurasi WiFi ---
const char* ssid = "Al Barokah";
const char* password = "1juni2023";

// --- Konfigurasi Server ---
const char* serverName = "http://192.168.1.15:5000/api/location";
const char* confirmUrl = "http://192.168.1.15:5000/api/confirm_connection";

// --- ID unik perangkat anak ---
String device_id = "CHILD001";

// --- GPS ---
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2: RX=16, TX=17

// --- Status Koneksi ---
bool wifiConnected = false;
bool isConnected = false;
bool gpsSignalFound = false;
unsigned long lastGpsCheck = 0;
unsigned long lastDataSent = 0;

void setup() {
  Serial.begin(115200);
  delay(3000); // Lebih lama delay untuk stabilitas
  
  Serial.println("\n=== SafeZoneX Debug Mode ===");
  Serial.println("ESP32 berhasil diinisialisasi");
  Serial.println("Memulai inisialisasi sistem...");
  
  // Inisialisasi GPS Serial dengan error handling
  Serial.println("Menginisialisasi GPS...");
  try {
    gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
    Serial.println("GPS Serial berhasil diinisialisasi");
  } catch (...) {
    Serial.println("Error: Gagal inisialisasi GPS Serial");
  }
  
  // WiFi Initialization yang lebih aman
  Serial.println("Mencoba koneksi WiFi...");
  safeWiFiInit();
  
  // Coba konfirmasi koneksi saat startup
  if (wifiConnected) {
    Serial.println("Mencoba konfirmasi koneksi ke server...");
    confirmConnection();
  }
  
  Serial.println("Sistem siap. Menunggu data GPS...");
  Serial.println("--------------------------------");
  
  // Print status awal
  printDebugInfo();
}

void safeWiFiInit() {
  Serial.println("Memulai inisialisasi WiFi yang aman...");
  
  // Set WiFi mode terlebih dahulu
  WiFi.mode(WIFI_STA);
  delay(1000);
  
  // Disconnect jika sudah ada koneksi sebelumnya
  WiFi.disconnect(true);
  delay(1000);
  
  // Set power mode
  WiFi.setSleep(false);
  delay(500);
  
  Serial.print("Menyambungkan ke WiFi: ");
  Serial.println(ssid);
  Serial.print("Password length: ");
  Serial.println(strlen(password));
  
  // Begin dengan delay
  WiFi.begin(ssid, password);
  delay(2000); // Tambahan delay setelah begin
  
  Serial.println("WiFi.begin() berhasil dipanggil");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000); // Delay lebih lama
    Serial.print(".");
    Serial.print(WiFi.status()); // Print status code
    attempts++;
    
    // Watchdog reset prevention
    yield();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi berhasil terhubung!");
    Serial.print("IP Address: "); 
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
  } else {
    wifiConnected = false;
    Serial.println("\nGagal terhubung ke WiFi!");
    Serial.print("Final WiFi Status: ");
    Serial.println(WiFi.status());
    printWiFiStatus();
  }
}

void printWiFiStatus() {
  Serial.println("--- WiFi Status Debug ---");
  Serial.print("Status Code: "); Serial.println(WiFi.status());
  Serial.print("SSID: "); Serial.println(WiFi.SSID());
  Serial.print("Signal Strength: "); Serial.println(WiFi.RSSI());
  Serial.print("MAC Address: "); Serial.println(WiFi.macAddress());
  Serial.println("------------------------");
}

void confirmConnection() {
  if (!wifiConnected) {
    Serial.println("WiFi tidak terhubung, skip konfirmasi server");
    return;
  }
  
  Serial.println("Mengecek koneksi ke server...");
  
  HTTPClient http;
  http.setTimeout(5000); // 5 detik timeout
  
  if (!http.begin(confirmUrl)) {
    Serial.println("Error: Gagal memulai HTTP client");
    return;
  }
  
  http.addHeader("Content-Type", "application/json");
  
  String jsonPayload = "{\"device_id\":\"" + device_id + "\"}";
  
  int httpCode = http.POST(jsonPayload);
  Serial.print("HTTP Response code: ");
  Serial.println(httpCode);
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.print("Server response: ");
    Serial.println(payload);
    
    if (payload.indexOf("success") != -1) {
      isConnected = true;
      Serial.println("Koneksi ke server berhasil dikonfirmasi!");
    } else {
      Serial.println("Server response tidak valid");
    }
  } else if (httpCode > 0) {
    Serial.print("HTTP Error: ");
    Serial.println(http.getString());
  } else {
    Serial.print("Connection Error: ");
    Serial.println(http.errorToString(httpCode));
  }
  
  http.end();
}

void sendDataViaWiFi(float latitude, float longitude) {
  if (!wifiConnected || !isConnected) {
    Serial.println("WiFi atau server tidak terhubung, skip pengiriman data");
    return;
  }
  
  Serial.println("\nMengirim data lokasi ke server...");
  Serial.print("Latitude: "); Serial.println(latitude, 6);
  Serial.print("Longitude: "); Serial.println(longitude, 6);
  
  HTTPClient http;
  http.setTimeout(10000); // 10 detik timeout
  
  if (!http.begin(serverName)) {
    Serial.println("Error: Gagal memulai HTTP client untuk pengiriman data");
    return;
  }
  
  http.addHeader("Content-Type", "application/json");
  
  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + device_id + "\",";
  jsonPayload += "\"latitude\":" + String(latitude, 6) + ",";
  jsonPayload += "\"longitude\":" + String(longitude, 6);
  jsonPayload += "}";
  
  Serial.print("JSON Payload: ");
  Serial.println(jsonPayload);
  
  int httpResponseCode = http.POST(jsonPayload);
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.print("Server Response: ");
    Serial.println(response);
    lastDataSent = millis();
    Serial.println("Data berhasil dikirim!");
  } else if (httpResponseCode > 0) {
    Serial.print("HTTP Error Response: ");
    Serial.println(http.getString());
  } else {
    Serial.print("Connection Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }
  
  http.end();
}

void printDebugInfo() {
  Serial.println("=== Status Sistem ===");
  
  // WiFi Status
  Serial.print("WiFi Status: "); 
  if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    Serial.println("Terhubung");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Tidak Terhubung");
  }
  
  // Server Connection
  Serial.print("Server Connection: ");
  Serial.println(isConnected ? "Terhubung" : "Tidak Terhubung");
  
  // GPS Status
  Serial.print("GPS Signal: ");
  Serial.println(gpsSignalFound ? "Ditemukan" : "Tidak Ditemukan");
  
  // GPS Satellites
  if (gps.satellites.isValid()) {
    Serial.print("GPS Satellites: ");
    Serial.println(gps.satellites.value());
  }
  
  // Last Data Sent
  Serial.print("Data Terakhir Dikirim: ");
  if (lastDataSent > 0) {
    Serial.print((millis() - lastDataSent) / 1000);
    Serial.println(" detik yang lalu");
  } else {
    Serial.println("Belum ada data dikirim");
  }
  
  // Memory Info
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  Serial.println("===================");
}

void loop() {
  // Baca data GPS dengan error handling
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    if (gps.encode(c)) {
      // Data GPS berhasil di-decode
    }
  }
  
  // Update status GPS setiap 5 detik
  if (millis() - lastGpsCheck > 5000) {
    gpsSignalFound = gps.location.isValid();
    lastGpsCheck = millis();
    
    // Print debug info setiap 5 detik
    printDebugInfo();
    
    // Print raw GPS data untuk debugging
    if (gpsSerial.available()) {
      Serial.print("GPS Raw Data Available: ");
      Serial.println(gpsSerial.available());
    }
  }
  
  // Cek jika ada update lokasi GPS
  if (gps.location.isUpdated()) {
    float latitude = gps.location.lat();
    float longitude = gps.location.lng();
    
    Serial.println("\n*** GPS Location Updated ***");
    Serial.print("Lat: "); Serial.println(latitude, 6);
    Serial.print("Lng: "); Serial.println(longitude, 6);
    
    // Kirim data jika WiFi dan server terhubung
    if (wifiConnected && isConnected) {
      sendDataViaWiFi(latitude, longitude);
    }
  }
  
  // Cek koneksi setiap 30 detik
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime > 30000) {
    Serial.println("\n--- Pengecekan Koneksi Berkala ---");
    
    // Cek WiFi
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      isConnected = false;
      Serial.println("WiFi terputus, mencoba reconnect...");
      safeWiFiInit();
    }
    
    // Konfirmasi koneksi server jika WiFi terhubung tapi server belum
    if (wifiConnected && !isConnected) {
      Serial.println("WiFi OK, tapi server belum terhubung, mencoba lagi...");
      confirmConnection();
    }
    
    lastCheckTime = millis();
  }
  
  // Tambahan yield untuk mencegah watchdog timeout
  yield();
  delay(100);
}