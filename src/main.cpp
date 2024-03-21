#include "cube_messages.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <esp_now.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0 
#define WHITE    0xFFFF

uint8_t SEND_ADDRESS[] = {0xA8, 0x42, 0xE3, 0xA9, 0x75, 0x5C};

#define PANEL_RES_X 64  // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1   // Total number of panels chained one to another

#define MISO 39
#define PN5180_BUSY 36
#define PN5180_NSS 32
#define PN5180_RST 17

#define BIG_ROW 0
#define BIG_COL 10
#define BIG_TEXT_SIZE 9
#define BRIGHTNESS 128
#define VERSION "v0.19"

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
uint8_t DEBUG_NFC[NFCID_LENGTH] = {
  0xdd, 0x11, 0xf8, 0xb8,
  0x50, 0x01, 0x04, 0xe0};
uint8_t NO_DEBUG_NFC[NFCID_LENGTH] = {
  0xbc, 0x10, 0xf8, 0xb8,
  0x50, 0x01, 0x04, 0xe0};

char current_letter = '?';
long loop_count = 0;
bool debug = false;
bool has_card = false;

static String last_esp_message = String();
static int nfc_rebroadcast_interval = 1;
static unsigned long last_nfc_broadcast = 0;

String convert_to_hex_string(uint8_t* data, int dataSize) {
  String hexString = "";
  for (int i = 0; i < dataSize; i++) {
    char hexChars[3];
    sprintf(hexChars, "%02X", data[i]);
    hexString += hexChars;
  }
  return hexString;
}

static char last_letter = ' ';
void display_current_letter() {
  hub75_display->drawFastVLine(63, 16, 32, has_card ? YELLOW : BLACK);
  if (current_letter == last_letter) {
    return;
  }
  hub75_display->setCursor(BIG_COL, BIG_ROW);
  hub75_display->setTextColor(0x9260, BLACK);
  hub75_display->setTextSize(BIG_TEXT_SIZE);
  hub75_display->print(current_letter);
  last_letter = current_letter;
}

void setup_nfc() {
  SPI.begin(SCK, MISO, MOSI, SS);
  Serial.println(F("Initializing nfc..."));

  nfc.begin();
  nfc.reset();
  Serial.println(F("Enabling RF field..."));
  nfc.setupRF();
}

void on_data_recv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  static int hang_count = 0;
  memcpy(&message_letter, incomingData, sizeof(message_letter));
  Serial.print(" Char: ");
  Serial.println(message_letter.letter);
  current_letter = message_letter.letter;

  // new letter might be due to game cycling--re-send NFC info
  nfc_rebroadcast_interval = 1;
  last_nfc_broadcast = millis();
}

void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "1" : "0");
}


void setup_espnow() {
  static esp_now_peer_info_t peer_info;
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(on_data_sent);

  memcpy(peer_info.peer_addr, SEND_ADDRESS, 6);
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
  hub75_display->setBrightness(BRIGHTNESS);
  hub75_display->setTextWrap(true);
}

void hub75log(const String& s) {
  Serial.println(s);
  if (!debug) {
    return;
  }
  hub75_display->setTextColor(RED, BLACK);
  hub75_display->setCursor(0, 0);
  hub75_display->setTextSize(1);
  hub75_display->print(s);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(0);
  Serial.println("starting....");

  setup_espnow();
  setup_hub75();
  setup_nfc();
  debug = true;
  hub75log(VERSION);
  debug = false;
  delay(1000);
  Serial.println(WiFi.macAddress());
  hub75_display->clearScreen();
  Serial.println(F("Setup Complete"));
}

ISO15693ErrorCode getInventory(uint8_t* uid) {
  return nfc.getInventory(uid);
}

void esp_resend() {
  unsigned long now = millis();
  // Serial.println(String(now) + ", " + String(last_nfc_broadcast) + ", " + String(nfc_rebroadcast_interval));
  if (last_nfc_broadcast == 0) {
    return;
  }
  if (nfc_rebroadcast_interval > 1000000 || now < last_nfc_broadcast + nfc_rebroadcast_interval) {
    return;
  }

  Serial.println("resending: " + last_esp_message);

  last_esp_message.toCharArray(message_nfcid.id, sizeof(message_nfcid.id));
  esp_now_send(SEND_ADDRESS, (uint8_t *) &message_nfcid, sizeof(message_nfcid));
  nfc_rebroadcast_interval *= 2;
}

esp_err_t esp_send(String s) {
  last_esp_message = s;
  nfc_rebroadcast_interval = 1;
  last_nfc_broadcast = millis();
  s.toCharArray(message_nfcid.id, sizeof(message_nfcid.id));
  return esp_now_send(SEND_ADDRESS, (uint8_t *) &message_nfcid, sizeof(message_nfcid));
}

void loop() {
  static uint16_t heartbeat_color = 3;
  hub75_display->drawPixel(1, 1, heartbeat_color);
  heartbeat_color = (heartbeat_color >> 1) | (heartbeat_color << (16 - 1));
  display_current_letter();
  esp_resend();

  // Loop waiting for NFC changes.
  uint8_t thisUid[NFCID_LENGTH];
  ISO15693ErrorCode rc = getInventory(thisUid);
  // Serial.println("looping getinv ");
  // return;
  if (rc == ISO15693_EC_OK) {
    if (memcmp(thisUid, DEBUG_NFC, NFCID_LENGTH) == 0) {
      debug = true;
      hub75log("DEBUG ON");
      return;
    }
    if (memcmp(thisUid, NO_DEBUG_NFC, NFCID_LENGTH) == 0) {
      debug = false;
      hub75_display->clearScreen();
      last_letter = ' ';
      display_current_letter();
      hub75log("DEBUG OFF");
      return;
    }

    if (has_card && memcmp(thisUid, last_nfcid, NFCID_LENGTH) == 0) {
      return;
    }
    has_card = true;
    Serial.println(F("New card"));
    String s = convert_to_hex_string(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));
    // s.toCharArray(message_nfcid.id, sizeof(message_nfcid.id));
    hub75log(s);

    Serial.println(String("Sending..." + s));
    esp_err_t result = esp_send(s);
    if (result != ESP_OK) {
      Serial.print("fail");
    }
    
    for (int j = 0; j < sizeof(thisUid); j++) {
      last_nfcid[j] = thisUid[j];
    }
  } else if (rc == EC_NO_CARD) {
    if (has_card) {
      hub75log("no nfc                  ");
      esp_send("");
      has_card = false;
    }
  } else {
      Serial.print("not ok");
  }
}
