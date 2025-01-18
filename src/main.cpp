#include "cube_messages.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans18pt7b.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <PN5180ISO15693.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <secrets.h>
#include "font.h"

#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0 
#define WHITE    0xFFFF

#define LETTER_COLOR 0xFDCC
#define HIGHLIGHT_LETTER_COLOR GREEN
#define LAST_LETTER_COLOR 0x71c0
#define PANEL_RES_X 64  // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1   // Total number of panels chained one to another

#define MISO 39
#define PN5180_BUSY 36
#define PN5180_NSS 32
#define PN5180_RST 17

#define BIG_ROW 0
#define BIG_COL 10
#define BIG_TEXT_SIZE 1
#define BRIGHTNESS 255
#define HIGHLIGHT_TIME_MS 2000
#define VERSION "v0.4d"

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

char last_letter = ' ';
char current_letter = '?';
char border = ' ';
long loop_count = 0;
bool debug = false;
bool has_card = false;
const char* ssid = SSID_NAME;
const char* password = WIFI_PASSWORD;
const char* mqtt_server = "192.168.0.211";

WiFiClient espClient;
PubSubClient client(espClient);
static String mac_id;
static String topic_out;

void setFont(MatrixPanel_I2S_DMA* hub75_display) {
  hub75_display->setFont(&Roboto_Mono_Bold_78);
}

String removeColons(const String& str) {
  String result = ""; 
  for (int i = 0; i < str.length(); i++) {
    if (str.charAt(i) != ':') {
      result += str.charAt(i); 
    }
  }
  return result;
}

String convert_to_hex_string(uint8_t* data, int dataSize) {
  String hexString = "";
  for (int i = 0; i < dataSize; i++) {
    char hexChars[3];
    sprintf(hexChars, "%02X", data[i]);
    hexString += hexChars;
  }
  return hexString;
}

long end_highlighting = 0;
uint8_t letter_percentage = 100;

void display_letter(uint16_t percent, char letter, uint16_t color) {
  int16_t row = (PANEL_RES_Y * percent) / 100;

  // char strBuf[80];
  // sprintf(strBuf, "display_letter %c: %2d (%8d)", letter, row, color);
  // Serial.println(strBuf);

  hub75_display->setCursor(BIG_COL, row-4);
  hub75_display->setTextColor(color, BLACK);
  hub75_display->setTextSize(BIG_TEXT_SIZE);
  hub75_display->print(letter);
  // delay(100);
}

void display_current_letter() {
  static long last_highlighted = 0;
  static uint16_t speed = 1;
  unsigned long now = millis();

  if (current_letter != last_letter) {
    // turn off highlighting which was probably meant for last letter.
    end_highlighting = 0;

    if (letter_percentage == 100) {
      // Begin transition.
      letter_percentage = 0;
      speed = 1;
    } else {
      letter_percentage = min(letter_percentage + speed, 100);
      speed += 1;
      // End transition.
      if (letter_percentage == 100) {
        last_letter = current_letter;
      }
    }
    display_letter(100 + letter_percentage, last_letter, RED);
  }
  uint16_t current_color = now < end_highlighting ? HIGHLIGHT_LETTER_COLOR : LETTER_COLOR;

  // Serial.println("display_current_letter: " + String(last_letter) + ", "+ String(current_letter) + ", " + String(letter_percentage));
  display_letter(letter_percentage, current_letter, current_color);

  if (has_card) {
    hub75_display->drawFastVLine(63, 30, 4, 0x7c51);
  }
  if (border != ' ') {
    hub75_display->drawFastHLine(0, 0, PANEL_RES_X, BLUE);
    hub75_display->drawFastHLine(0, 1, PANEL_RES_X, 0x0019);

    hub75_display->drawFastHLine(0, PANEL_RES_Y-1, PANEL_RES_X, BLUE);
    hub75_display->drawFastHLine(0, PANEL_RES_Y-2, PANEL_RES_X, 0x0019);
  } 
  if (border == '[') {
    hub75_display->drawFastVLine(0, 2, PANEL_RES_Y-4, BLUE);
    hub75_display->drawFastVLine(1, 2, PANEL_RES_Y-4, 0x0019);
  } else if (border == ']') {
    hub75_display->drawFastVLine(PANEL_RES_X-1, 2, PANEL_RES_Y-4, BLUE);
    hub75_display->drawFastVLine(PANEL_RES_X-2, 2, PANEL_RES_Y-4, 0x0019);
  }
  hub75_display->flipDMABuffer();    
  hub75_display->clearScreen();
}

void setup_nfc() {
  SPI.begin(SCK, MISO, MOSI, SS);
  Serial.println(F("Initializing nfc..."));

  nfc.begin();
  nfc.reset();
  Serial.println(F("Enabling RF field..."));
  nfc.setupRF();
}

void setup_hub75() {
  mxconfig.clkphase = false;
  // mxconfig.driver = HUB75_I2S_CFG::ICN2038S;
  
  mxconfig.double_buff = true;
  hub75_display = new MatrixPanel_I2S_DMA(mxconfig);
  hub75_display->begin();
  setFont(hub75_display);

  // hub75_display->setBrightness(BRIGHTNESS);
  hub75_display->setBrightness8(BRIGHTNESS);
  // hub75_display->setPanelBrightness(BRIGHTNESS);
  // hub75_display->fillScreenRGB888(255,0,0);
  // sleep(2);

  hub75_display->setTextWrap(true);
  hub75_display->clearScreen();
}

void hub75log(const String& s) {
  Serial.println(s);
  if (!debug) {
    return;
  }
  hub75_display->setFont();
  hub75_display->setTextColor(RED, BLACK);
  hub75_display->setCursor(0, PANEL_RES_Y/2);
  hub75_display->setTextSize(1);
  hub75_display->print(s);
  setFont(hub75_display);
  hub75_display->flipDMABuffer();    
  hub75_display->clearScreen();
}

void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mac_id.c_str())) {
      Serial.println("connected");
      String s = "cube/" + mac_id + "/#";
      client.subscribe(s.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying...");
      delay(200);
    }
  }
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message: ");
  Serial.print(topic);
  Serial.print(", ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if (strstr(topic, "letter") != nullptr) {
    current_letter = messageTemp.charAt(0);
  }
  if (strstr(topic, "flash") != nullptr) {
    end_highlighting = millis() + HIGHLIGHT_TIME_MS;
  }
  if (strstr(topic, "border") != nullptr) {
    border = messageTemp.charAt(0);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(0);
  Serial.println("starting....");
  debug = true;
  setup_hub75();
  hub75log(VERSION);
  debug = false;
  delay(800);

  display_letter(100, '3', MAGENTA);
  hub75_display->flipDMABuffer();    
  hub75_display->clearScreen();
  Serial.println("setting up wifi...");
  setup_wifi();
  display_letter(100, '2', MAGENTA);
  hub75_display->flipDMABuffer();    
  hub75_display->clearScreen();
  mac_id = removeColons(WiFi.macAddress());
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  reconnect();
  
  topic_out = String("cube/nfc/");
  topic_out += mac_id;

  display_letter(100, '1', MAGENTA);
  hub75_display->flipDMABuffer();    
  hub75_display->clearScreen();
  Serial.println(WiFi.macAddress());

  setup_nfc();
  hub75_display->clearScreen();
  Serial.println(F("Setup Complete"));
}

ISO15693ErrorCode getInventory(uint8_t* uid) {
  return nfc.getInventory(uid);
}

void loop() {
  static unsigned long last_loop_time = millis();
  static uint16_t last_nfc_reset = 0;
  static uint16_t nfc_reset_count = 1;
  static long last_nfc_ms = 0;
  static uint16_t heartbeat_color = 3;
  // hub75_display->drawPixel(4, 4, heartbeat_color);
  hub75_display->drawFastHLine(4, 5, nfc_reset_count, heartbeat_color);
  heartbeat_color = (heartbeat_color >> 1) | (heartbeat_color << (16 - 1));
  display_current_letter();
  // esp_resend();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  // Serial.println(now - last_loop_time);
  if (now - last_loop_time > 100) {
    Serial.println("reseting nfc");
    nfc.reset();
    nfc.setupRF();
    // hub75_display->drawPixel(10, 10, heartbeat_color);
    nfc_reset_count++;
    last_nfc_reset = 0;
  }
  last_loop_time = now;
  // Loop waiting for NFC changes.
  uint8_t thisUid[NFCID_LENGTH];
  ISO15693ErrorCode rc = getInventory(thisUid);
  // Serial.println("looping getinv ");
  // return;
  if (rc == ISO15693_EC_OK) {
    if (has_card && memcmp(thisUid, last_nfcid, NFCID_LENGTH) == 0) {
      return;
    }
    has_card = true;
    Serial.println(F("New card"));
    String s = convert_to_hex_string(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));
    client.publish(topic_out.c_str(), s.c_str(), true);
    
    for (int j = 0; j < sizeof(thisUid); j++) {
      last_nfcid[j] = thisUid[j];
    }
    last_nfc_ms = millis();
  } else if (rc == EC_NO_CARD) {
    if (has_card) {
      String delay_msg("last delay: ");
      long delay = millis() - last_nfc_ms;
      delay_msg += String(delay);
      Serial.println(delay_msg);
      hub75log("no nfc                  ");
      client.publish("cube/delay", delay_msg.c_str(), true);
      if (delay < 100) { // DEBOUNCE
        return;
      }
      client.publish(topic_out.c_str(), "", true);
      has_card = false;
    }
  } else {
      Serial.println("not ok");
  }
}
