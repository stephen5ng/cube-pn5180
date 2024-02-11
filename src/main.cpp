#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <PN5180.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <Wire.h>

#define PANEL_RES_X 64  // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1   // Total number of panels chained one to another

#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define PN5180_NSS 32
#define PN5180_BUSY 36
#define PN5180_RST 17

MatrixPanel_I2S_DMA *dma_display;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool hasDisplay = false;

PN5180ISO15693 nfc = PN5180ISO15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);

HUB75_I2S_CFG::i2s_pins _pins = {
  33,  //R1_PIN,
  26,  //G1_PIN,
  25,  //B1_PIN,
  14,  //R2_PIN,
  27,  //G2_PIN,
  13,  //B2_PIN,
  5,   //A_PIN,
  21,  //B_PIN,
  4,   //C_PIN,
  22,  //D_PIN,
  12,  //E_PIN,
  2,   //LAT_PIN,
  15,  //OE_PIN,
  16,  //CLK_PIN
};

HUB75_I2S_CFG mxconfig(
  PANEL_RES_X,
  PANEL_RES_Y,
  PANEL_CHAIN,
  _pins
);

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
  Serial.println("starting");

  SPI.begin(SCK, 39, MOSI, SS);

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

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness(255);
  dma_display->setTextSize(2);      // size 1 == 8 pixels high
  dma_display->setTextWrap(true);  // Don't wrap at end of line - will do ourselves
  dma_display->clearScreen();

  Serial.println(F("Initializing..."));
  nfc.begin();
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
    static char str[] = "A";
    dma_display->clearScreen();
    dma_display->setCursor(12, 4);

    String s = convertToHexString(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));
    Serial.println(s);
    dma_display->print(s);
    if (hasDisplay) {
      display.clearDisplay();

      display.setCursor(0, 0);
      display.println(F("Card.:"));
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
      dma_display->clearScreen();
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
  // static char str[] = "A";
  // dma_display->clearScreen();
  // dma_display->setCursor(12, 4);
  // dma_display->print(str);
  // Serial.println(str);
  // if (str[0]++ == 'Z') {
  //   str[0] = 'A';
  // }
  // delay(300);

}
