#include "cube_messages.h"
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <esp_now.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

uint8_t broadcast_address[] = {0xE8, 0x6B, 0xEA, 0xF6, 0xFB, 0x98};

#define PANEL_RES_X 64  // Number of pixels wide of each INDIVIDUAL panel module.
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

MessageLetter message_letter;
MessageNfcId message_nfcid;

uint8_t last_nfcid[NFCID_LENGTH];

String convert_to_hex_string(uint8_t* data, int dataSize) {
  String hexString = "";
  for (int i = 0; i < dataSize; i++) {
    char hexChars[3];
    sprintf(hexChars, "%02X", data[i]);
    hexString += hexChars;
  }
  return hexString;
}

void on_data_recv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&message_letter, incomingData, sizeof(message_letter));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("Char: ");
  Serial.println(message_letter.letter);
  hub75_display->clearScreen();
  hub75_display->setCursor(0, 0);
  hub75_display->setTextSize(10);

  hub75_display->print(message_letter.letter);
}
 
void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "1" : "0");
}

esp_now_peer_info_t peer_info;

void setup_espnow() {
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(on_data_sent);

  memcpy(peer_info.peer_addr, broadcast_address, 6);
  peer_info.channel = 0;  
  peer_info.encrypt = false;
  
  if (esp_now_add_peer(&peer_info) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  esp_now_register_recv_cb(on_data_recv);
}

void setup_hub75() {
  mxconfig.clkphase = false;
  hub75_display = new MatrixPanel_I2S_DMA(mxconfig);
  hub75_display->begin();
  hub75_display->setBrightness(255);
  hub75_display->setTextSize(10);
  hub75_display->setTextWrap(true);
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

  setup_espnow();
  setup_hub75();
  setup_nfc();

  Serial.println(F("Setup Complete"));
}

bool hasCard = false;
void loop() {
  uint8_t thisUid[8];
  ISO15693ErrorCode rc = nfc.getInventory(thisUid);
  if (rc == ISO15693_EC_OK) {
    if (hasCard && memcmp(thisUid, last_nfcid, 8) == 0) {
      return;
    }
    hasCard = true;
    Serial.println(F("New card detected on reader"));
    String s = convert_to_hex_string(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));
    s.toCharArray(message_nfcid.id, sizeof(message_nfcid.id));

    Serial.println(String("sending..." + String(message_nfcid.id)));
    esp_err_t result = ESP_FAIL;
    result = esp_now_send(broadcast_address, (uint8_t *) &message_nfcid, sizeof(message_nfcid));
    if (result == ESP_OK) {
      Serial.println(String("Success: ") + message_nfcid.id);
    }
    else {
      Serial.print("fail");
    }

    hub75_display->clearScreen();
    hub75_display->setCursor(0, 0);
    hub75_display->setTextSize(1);
    hub75_display->print(s);

    for (int j = 0; j < sizeof(thisUid); j++) {
      Serial.print(thisUid[j], HEX);
      Serial.print(" ");
      last_nfcid[j] = thisUid[j];
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
