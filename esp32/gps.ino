#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// --- Konfigurasi WiFi ---
const char* ssid = "Al Barokah";
const char* password = "1juni2023";

// --- Konfigurasi Server ---
const char* serverName = "http://192.168.1.5:5000/api/location";
const char* confirmUrl = "http://192.168.1.5:5000/api/confirm_connection";

// --- ID unik perangkat anak ---
String device_id = "CHILD001";

// --- GPS ---
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2: RX=16, TX=17

// --- Status Koneksi ---
bool wifiConnected = false;
bool isConnected = false;

// --- Debug Flags ---
bool gpsSignalFound = false;
unsigned long lastGpsCheck = 0;
unsigned long lastDataSent = 0;
int gpsSatellites = 0;
float gpsHDOP = 0.0;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  
  Serial.println("\n=== SafeZoneX Debug Mode ===");
  Serial.println("Memulai inisialisasi sistem...");
  
  // Coba koneksi WiFi
  connectWiFi();
  
  // Coba konfirmasi koneksi saat startup
  if (wifiConnected) {
    confirmConnection();
  }
  
  Serial.println("Sistem siap. Menunggu data GPS...");
  Serial.println("--------------------------------");
}

void connectWiFi() {
  Serial.println("Mencoba koneksi WiFi...");
  Serial.print("SSID: "); Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi terhubung!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nGagal terhubung ke WiFi!");
  }
}

void confirmConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Mencoba konfirmasi koneksi ke server...");
    
    HTTPClient http;
    http.begin(confirmUrl);
    http.addHeader("Content-Type", "application/json");
    
    String jsonPayload = "{\"device_id\":\"" + device_id + "\"}";
    int httpCode = http.POST(jsonPayload);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      if (payload.indexOf("success") != -1) {
        isConnected = true;
        Serial.println("Koneksi ke server berhasil dikonfirmasi!");
      }
    } else {
      Serial.print("Gagal konfirmasi koneksi. HTTP Code: "); Serial.println(httpCode);
    }
    http.end();
  }
}

void sendDataViaWiFi(float latitude, float longitude) {
  if (!wifiConnected) {
    Serial.println("Tidak dapat mengirim data: WiFi tidak terhubung");
    return;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nMengirim data lokasi ke server...");
    Serial.print("Latitude: "); Serial.println(latitude, 6);
    Serial.print("Longitude: "); Serial.println(longitude, 6);
    
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    
    String jsonPayload = "{";
    jsonPayload += "\"device_id\":\"" + device_id + "\",";
    jsonPayload += "\"latitude\":" + String(latitude, 6) + ",";
    jsonPayload += "\"longitude\":" + String(longitude, 6);
    jsonPayload += "}";
    
    Serial.println("Payload: " + jsonPayload);
    
    int httpResponseCode = http.POST(jsonPayload);
    Serial.print("HTTP Response code: "); Serial.println(httpResponseCode);
    
    if (httpResponseCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Response: " + response);
      lastDataSent = millis();
    } else {
      Serial.print("Error mengirim data. HTTP Code: "); Serial.println(httpResponseCode);
    }
    
    http.end();
  }
}

void printDebugInfo() {
  Serial.println("\n=== Status Sistem ===");
  Serial.print("WiFi Status: "); 
  if (wifiConnected) {
    Serial.println("Terhubung");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Tidak Terhubung");
  }
  
  Serial.print("Server Connection: ");
  Serial.println(isConnected ? "Terhubung" : "Tidak Terhubung");
  
  Serial.print("GPS Signal: ");
  Serial.println(gpsSignalFound ? "Ditemukan" : "Tidak Ditemukan");
  
  if (gpsSignalFound) {
    Serial.print("Satelit: "); Serial.println(gpsSatellites);
    Serial.print("HDOP: "); Serial.println(gpsHDOP, 2);
    Serial.print("Lokasi Terakhir: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);
  }
  
  Serial.print("Data Terakhir Dikirim: ");
  if (lastDataSent > 0) {
    Serial.print((millis() - lastDataSent) / 1000);
    Serial.println(" detik yang lalu");
  } else {
    Serial.println("Belum ada data dikirim");
  }
  
  Serial.println("===================");
}

void loop() {
  // Baca data GPS
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  
  // Update status GPS setiap 5 detik
  if (millis() - lastGpsCheck > 5000) {
    gpsSignalFound = gps.location.isValid();
    gpsSatellites = gps.satellites.value();
    gpsHDOP = gps.hdop.hdop();
    lastGpsCheck = millis();
    
    // Print debug info setiap 5 detik
    printDebugInfo();
  }
  
  if (gps.location.isUpdated()) {
    float latitude = gps.location.lat();
    float longitude = gps.location.lng();
    
    // Kirim data melalui WiFi
    if (wifiConnected && isConnected) {
      sendDataViaWiFi(latitude, longitude);
    }
  }
  
  // Cek koneksi WiFi setiap 30 detik
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      Serial.println("Koneksi WiFi terputus. Mencoba menghubungkan kembali...");
      connectWiFi();
      if (wifiConnected) {
        confirmConnection();
      }
    }
    lastCheckTime = millis();
  }
  
  delay(1000); // Delay 1 detik untuk mengurangi beban CPU
}
