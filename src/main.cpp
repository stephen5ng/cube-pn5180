#include "cube_messages.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Easing.h>
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
#define VERSION "v0.5e"

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

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */

char last_letter = ' ';
char current_letter = '?';
char border = ' ';
bool border_is_word = false;
long loop_count = 0;
bool debug = false;
bool last_has_card = false;
const char* ssid = SSID_NAME;
const char* password = WIFI_PASSWORD;
#define MQTT_SERVER_MACBOOK_ALBANY "192.168.0.211"
#define MQTT_SERVER_MACBOOK_YACHATS "192.168.0.161"
const char* mqtt_server_macbook = MQTT_SERVER_MACBOOK_YACHATS;
const char* mqtt_server_pi = "192.168.0.247";

WiFiClient espClient;
PubSubClient client(espClient);
static String mac_id;
const char* topic_out;

EasingFunc<Ease::BounceOut> easing;

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

unsigned long start_ease = 0;

void display_current_letter() {
  static long last_highlighted = 0;
  static uint16_t speed = 1;
  unsigned long now = millis();
  uint8_t letter_percentage = 100;

  if (current_letter != last_letter) {
    // turn off highlighting which was probably meant for last letter.
    end_highlighting = 0;

    float duration = now - start_ease;
    if (now - start_ease >= easing.duration()) {
      last_letter = current_letter; // done
    } else {
      letter_percentage = easing.get(duration);
      // String s = String("easing: ");
      // s+= String(duration) + " " + String(letter_percentage);
      // Serial.println(s);
    }
    display_letter(100 + letter_percentage, last_letter, RED);
  }
  uint16_t current_color = now < end_highlighting ? HIGHLIGHT_LETTER_COLOR : LETTER_COLOR;

  // Serial.println("display_current_letter: " + String(last_letter) + ", "+ String(current_letter) + ", " + String(letter_percentage));
  display_letter(letter_percentage, current_letter, current_color);

  if (last_has_card) {
    hub75_display->drawFastVLine(62, 30, 2, 0x7c51);
  }
  if (current_letter == ' ') {
    border = ' ';
  }
  uint16_t border_color = now < end_highlighting ? HIGHLIGHT_LETTER_COLOR : WHITE;
  if (border != ' ') {
    hub75_display->drawFastHLine(0, 0, PANEL_RES_X, border_color);
    hub75_display->drawFastHLine(0, 1, PANEL_RES_X, border_color);

    hub75_display->drawFastHLine(0, PANEL_RES_Y-1, PANEL_RES_X, border_color);
    hub75_display->drawFastHLine(0, PANEL_RES_Y-2, PANEL_RES_X, border_color);
  } 
  if (border == '[') {
    hub75_display->drawFastVLine(0, 2, PANEL_RES_Y-4, border_color);
    hub75_display->drawFastVLine(1, 2, PANEL_RES_Y-4, border_color);
  } else if (border == ']') {
    hub75_display->drawFastVLine(PANEL_RES_X-1, 2, PANEL_RES_Y-4, border_color);
    hub75_display->drawFastVLine(PANEL_RES_X-2, 2, PANEL_RES_Y-4, border_color);
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

  // hub75_display->setBrightness(BRIGHTNESS);
  hub75_display->setBrightness8(BRIGHTNESS);
  // hub75_display->setPanelBrightness(BRIGHTNESS);
  // hub75_display->fillScreenRGB888(255,0,0);
  // sleep(2);

  hub75_display->setTextWrap(true);
  hub75_display->clearScreen();
}

unsigned int simpleHash(const char* str) {
  unsigned int hash = 0;
  while (*str) {
    hash = (hash << 5) - hash + *str++; // Simple hash function
  }
  return hash;
}

uint8_t getIpOctet() {
  const char* char_array = WiFi.macAddress().c_str();
  unsigned int hashedNumber = simpleHash(char_array);
  return hashedNumber % 98 + 2; // Map to range 2-99
}

void setup_wifi() {
  Serial.println("mac address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // Set your Static IP address
  IPAddress local_IP(192, 168, 0, getIpOctet());
  // Set your Gateway IP address
  IPAddress gateway(192, 168, 0, 1);

  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);   //optional
  IPAddress secondaryDNS(8, 8, 4, 4); //optional


  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    uint16_t retries = 0;

    delay(100);
    Serial.print(".");
    if (retries++ > 50) {
        esp_sleep_enable_timer_wakeup(5 * uS_TO_S_FACTOR);
        esp_deep_sleep_start();
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  uint16_t retries = 0;
  bool pi_server = true;
  client.setServer(mqtt_server_pi, 1883);

  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mac_id.c_str(), topic_out, 0, true, "")) {
      Serial.print("connected, pi_server: ");
      Serial.println(pi_server);
      client.subscribe(String("cube/" + mac_id + "/#").c_str());
      client.subscribe(String("game/nfc/" + mac_id).c_str());
    } else {
      pi_server = ! pi_server;
      client.setServer(pi_server ? mqtt_server_macbook : mqtt_server_pi, 1883);
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.print(" retries=");
      Serial.println(retries);
      if (retries++ > 20) {
        esp_sleep_enable_timer_wakeup(5 * uS_TO_S_FACTOR);
        esp_deep_sleep_start();
      }
      Serial.println(" retrying...");
      delay(200);
    }
  }
}

String last_neighbor = "INIT";

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

  if (strstr(topic, "game/nfc") != nullptr) {
    last_neighbor = messageTemp;
  }

  if (strstr(topic, "sleep") != nullptr) {
    char sleep = messageTemp.charAt(0);
    if (sleep == '1') {
      Serial.println("sleeping due to /sleep");
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      esp_deep_sleep_start();
    }
    return;
  }
  if (strstr(topic, "reboot") != nullptr) {
    ESP.restart();
    return;
  }
  else if (strstr(topic, "reset") != nullptr) {
    nfc.reset();
    nfc.setupRF();
    return;
  } else if (strstr(topic, "letter") != nullptr) {
    current_letter = messageTemp.charAt(0);
    start_ease = millis();
    return;
  }
  else if (strstr(topic, "flash") != nullptr) {
    end_highlighting = millis() + HIGHLIGHT_TIME_MS;
    border_is_word = true;
    return;
  }
  else if (strstr(topic, "border") != nullptr) {
    border = messageTemp.charAt(0);
    border_is_word = false;
    return;
  }
}

String createTopic(String s) {
  String t = String("cube/");
  t += s + String("/");
  t += mac_id;
  return t;
}

uint8_t print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
  return wakeup_reason;
}

void debugPrint(const char* s)
{
  hub75_display->setCursor(0, 0);
  hub75_display->setTextColor(RED, BLACK);
  hub75_display->print(s);
  hub75_display->flipDMABuffer();    
  hub75_display->clearScreen();
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(0);
  Serial.println("starting....");
  debug = true;
  setup_hub75();
  debugPrint(VERSION);
  debug = false;
  delay(600);
  uint8_t wakeup = print_wakeup_reason();

  easing.duration(800);
  easing.scale(100);

  debugPrint((String("wake up:") + String(wakeup)).c_str());
  Serial.println("setting up wifi...");
  setup_wifi();
  mac_id = removeColons(WiFi.macAddress());
  debugPrint((String("ip,mac:") + WiFi.localIP().toString()+
    ", " + mac_id.c_str()).c_str());
  client.setCallback(callback);
  reconnect();
  static String nfc_topic = createTopic("nfc");
  topic_out = nfc_topic.c_str();
  client.publish(createTopic("version").c_str(), VERSION);

  debugPrint("nfc...");
  Serial.println(WiFi.macAddress());

  setup_nfc();
  hub75_display->clearScreen();
  setFont(hub75_display);

  Serial.println(F("Setup Complete"));
}

ISO15693ErrorCode getInventory(uint8_t* uid) {
  return nfc.getInventory(uid);
}

unsigned long last_nfc_publish_time = 0;

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
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  static uint32_t loop_count = 0;
  if (loop_count++ % 1000 == 0) {
    static String loop_delay_topic = createTopic("loop_delay");
    String loop_delay_msg = String(now - last_loop_time);
    client.publish(loop_delay_topic.c_str(), loop_delay_msg.c_str());
  }

  if (now - last_loop_time > 100) {
    nfc.reset();
    nfc.setupRF();
    static String loop_delay_topic = createTopic("loop_delay_reset");
    String loop_delay_msg = String(now - last_loop_time);
    client.publish(loop_delay_topic.c_str(), loop_delay_msg.c_str());
    // Serial.println("reseting nfc");
    // hub75_display->drawPixel(10, 10, heartbeat_color);
    nfc_reset_count++;
    last_nfc_reset = 0;
  }

  last_loop_time = now;
  // Loop waiting for NFC changes.
  uint8_t thisUid[NFCID_LENGTH];
  ISO15693ErrorCode rc = getInventory(thisUid);
  // Serial.println(rc);
  if (rc == ISO15693_EC_OK) {
    String neighbor = convert_to_hex_string(thisUid, sizeof(thisUid) / sizeof(thisUid[0]));
    if (neighbor.equals(last_neighbor)) {
      // Serial.println("skipping: server matches");
      return;
    }
    if (now - last_nfc_publish_time < 1000) {
      Serial.println("skipping: pause");
      return;
    }

    Serial.println(F("New card"));
    client.publish(topic_out, neighbor.c_str(), true);

    last_nfc_publish_time = millis();
  } else if (rc == EC_NO_CARD) {
    if (last_neighbor.equals("")) {
      // Serial.println("skipping 0: server up to date");
      return;
    }
    if (now - last_nfc_publish_time < 1000) {
      Serial.println("skipping 0: pause");
      return;
    }
    long delay = now - last_nfc_publish_time;
    if (delay < 100) { // DEBOUNCE
      Serial.println("skipping 0: debounce");
      return;
    }
    Serial.println("publishing no-link");
    client.publish(topic_out, "", true);
    last_nfc_publish_time = millis();
  } else {
      static String reset_topic = createTopic("reset");
      nfc.reset();
      nfc.setupRF();
      client.publish(reset_topic.c_str(), String(nfc_reset_count++).c_str());

      Serial.println("not ok");
  }
}

