#include "secrets.h"  // SSID_NAME, WIFI_PASSWORD

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

#define PANEL_RES_X 128  // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1   // Total number of panels chained one to another

#define MISO 39
#define PN5180_BUSY 36
#define PN5180_NSS 32
#define PN5180_RST 17

HUB75_I2S_CFG::i2s_pins _pins = {
  25,  //R1_PIN,
  26,  //G1_PIN,
  33,  //B1_PIN,
  13,  //R2_PIN,
  27,  //G2_PIN,
  14,  //B2_PIN,
  19,  //A_PIN,
  21,  //B_PIN,
  4,   //C_PIN,
  22,  //D_PIN,
  12,  //E_PIN,
  15,  //LAT_PIN,
  2,   //OE_PIN,
  16,  //CLK_PIN
};

HUB75_I2S_CFG mxconfig(
  PANEL_RES_X,
  PANEL_RES_Y,
  PANEL_CHAIN,
  _pins
);

MatrixPanel_I2S_DMA *hub75_display;
PN5180ISO15693 nfc = PN5180ISO15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);
AsyncWebServer server(80);

String convertToHexString(uint8_t* data, int dataSize) {
  String hexString = "";
  for (int i = 0; i < dataSize; i++) {
    char hexChars[3];
    sprintf(hexChars, "%02X", data[i]);
    hexString += hexChars;
  }
  return hexString;
}

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID_NAME, WIFI_PASSWORD);

  Serial.println("connecting wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", "Hello, world");
      hub75_display->print("hello");
      Serial.println("hello");
  });
  server.begin();
  Serial.println("HTTP server started");
}

void setup_hub75() {
  hub75_display = new MatrixPanel_I2S_DMA(mxconfig);
  hub75_display->begin();
  hub75_display->setBrightness(255);
  hub75_display->setTextSize(2);     // size 1 == 8 pixels high
  hub75_display->setTextWrap(true);  // Don't wrap at end of line - will do ourselves
  hub75_display->clearScreen();
  hub75_display->setCursor(0, 0);
  hub75_display->print("hello");
}

void setup_nfc() {
  SPI.begin(SCK, MISO, MOSI, SS);
  Serial.println(F("Initializing nfc..."));

  nfc.begin();
  nfc.reset();
  Serial.println(F("Enabling RF field..."));
  nfc.setupRF();
}

void setup() {
  Serial.begin(115200);
  Serial.println("starting...");

  setup_wifi();
  setup_hub75();
  setup_nfc();

  Serial.println(F("Setup Complete"));
}

uint8_t lastUid[8];
bool hasCard = false;
void loop() {
  uint8_t thisUid[8];

  ISO15693ErrorCode rc = nfc.getInventory(thisUid);
  if (rc == ISO15693_EC_OK) {
    if (hasCard && memcmp(thisUid, lastUid, 8) == 0) {
      return;
    }
    hasCard = true;
    Serial.print(F("New card detected on reader"));
    String s = convertToHexString(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));

    hub75_display->clearScreen();
    hub75_display->setCursor(0, 0);
    hub75_display->print(s);

    for (int j = 0; j < sizeof(thisUid); j++) {
      Serial.print(thisUid[j], HEX);
      Serial.print(" ");
      lastUid[j] = thisUid[j];
    }
    Serial.println();
  } else if (rc == EC_NO_CARD) {
    if (hasCard) {
      hub75_display->clearScreen();
      hub75_display->setCursor(0, 0);
      hub75_display->println("no card");

      hasCard = false;
      Serial.println("Removed card");
    }
  } else {
      Serial.print("not ok");
  }
}
