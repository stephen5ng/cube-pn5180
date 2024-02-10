#include <Arduino.h>
#include <PN5180.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool hasDisplay = true;

#define PN5180_NSS 16
#define PN5180_BUSY 5
#define PN5180_RST 17
PN5180ISO15693 nfc = PN5180ISO15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);

String convertToHexString(uint8_t* data, int dataSize) {
  String hexString = "";
  for (int i = 0; i < dataSize; i++) {
    // Convert each byte to a two-digit hexadecimal representation
    char hexChars[3];
    sprintf(hexChars, "%02X", data[i]);
    hexString += hexChars;
  }
  return hexString;
}

void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    hasDisplay = false;
  }

  if (hasDisplay) {
    Serial.println(F("init display..."));

    display.clearDisplay();

    display.setTextSize(2);  // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.println(F("Started"));
    display.display();

  }

  Serial.println(F("Initializing..."));
  nfc.begin();
  Serial.println(F("Initializing... done"));
  nfc.reset();
  Serial.println(F("Enabling RF field..."));
  nfc.setupRF();

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
    Serial.print(F("New Card Detected on Reader "));
    if (hasDisplay) {
      display.clearDisplay();

      display.setCursor(0, 0);
      display.println(F("Card:"));
      String s = convertToHexString(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));
      Serial.println(s);
      display.println(s);
      display.display();
    }
    for (int j = 0; j < sizeof(thisUid); j++) {
      Serial.print(thisUid[j], HEX);
      Serial.print(" ");
      lastUid[j] = thisUid[j];
    }
    Serial.println();
  } else if (rc == EC_NO_CARD) {
    if (hasCard) {
      hasCard = false;
      Serial.println("Removed card");
      if (hasDisplay) {
        display.clearDisplay();

        display.setCursor(10, 0);
        display.println(F("No card!"));
        display.display();  // Show initial text
      }
    }
  } else {
    Serial.print("not ok");
    Serial.println(rc, HEX);
  }
}
