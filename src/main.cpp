#include "cube_messages.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Easing.h>
#include <Fonts/FreeSans18pt7b.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <EspMQTTClient.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <secrets.h>
#include "font.h"

// ============= Configuration =============
// MAC Address Table
const char *CUBE_MAC_ADDRESSES[] = {
  "C4:DD:57:8E:46:C8", "94:54:C5:EE:87:F0",
  "D8:BC:38:FD:D0:BC", "D8:BC:38:FD:E0:98",
  "CC:DB:A7:98:54:2C", "CC:DB:A7:99:0F:E0",
  "CC:DB:A7:9F:C2:84", "94:54:C5:ED:C6:34",
  "CC:DB:A7:95:E7:70", "94:54:C5:F1:AF:00",
  "14:2B:2F:DA:FB:F4", "94:54:C5:EE:89:4C"
};
#define NUM_CUBE_MAC_ADDRESSES (sizeof(CUBE_MAC_ADDRESSES) / sizeof(CUBE_MAC_ADDRESSES[0]))

// Display Configuration
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

// Pin Definitions
#define MISO 39
#define PN5180_BUSY 36
#define PN5180_NSS 32
#define PN5180_RST 17

// Display Settings
#define BIG_ROW 0
#define BIG_COL 10
#define BIG_TEXT_SIZE 1
#define BRIGHTNESS 255
#define HIGHLIGHT_TIME_MS 2000
#define VERSION "v0.7d"
#define PRINT_DEBUG true

// Sleep Configuration
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */

// MQTT Configuration
#define MQTT_SERVER_PI "192.168.0.247"

// ============= Global Variables =============
bool is_front_display = false;

// HUB75 Display Configuration
HUB75_I2S_CFG::i2s_pins display_pins = {
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

HUB75_I2S_CFG display_config(
  PANEL_RES_X,
  PANEL_RES_Y,
  PANEL_CHAIN,
  display_pins
);

// Hardware Objects
MatrixPanel_I2S_DMA *led_display;
PN5180ISO15693 nfc_reader = PN5180ISO15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);

// Message Objects
MessageLetter letter_message;
MessageNfcId nfc_message;

// NFC State
uint8_t last_nfc_id[NFCID_LENGTH];
uint8_t DEBUG_NFC_ID[NFCID_LENGTH] = {
  0xdd, 0x11, 0xf8, 0xb8,
  0x50, 0x01, 0x04, 0xe0};
uint8_t NO_DEBUG_NFC_ID[NFCID_LENGTH] = {
  0xbc, 0x10, 0xf8, 0xb8,
  0x50, 0x01, 0x04, 0xe0};

// Display State
char previous_letter = ' ';
char current_letter = '?';
char border_style = ' ';
uint16_t border_color = WHITE;
bool is_border_word = false;
bool has_card_present = false;

// Network Objects
EspMQTTClient mqtt_client(
  MQTT_SERVER_PI,
  1883,
  "",
  "",
  "TestClient"
);
WiFiClient wifi_client;
static String cube_identifier;
const char* nfc_topic_out;

// Animation
EasingFunc<Ease::BounceOut> letter_animation;
long highlight_end_time = 0;
unsigned long animation_start_time = 0;
String last_neighbor_id = "INIT";
unsigned long last_nfc_publish_time = 0;

// ============= Debug Functions =============
void displayDebugMessage(const char* message) {
  led_display->setCursor(0, 0);
  led_display->setTextColor(RED, BLACK);
  led_display->print(message);
  led_display->flipDMABuffer();    
  led_display->clearScreen();
}

void debugPrint(const char* message) {
  if (PRINT_DEBUG) {
    Serial.print(message);
  }
}

void debugPrintln(const char* message) {
  if (PRINT_DEBUG) {
    Serial.println(message);
  }
}

void debugPrint(const __FlashStringHelper* message) {
  if (PRINT_DEBUG) {
    Serial.print(message);
  }
}

void debugPrintln(const __FlashStringHelper* message) {
  if (PRINT_DEBUG) {
    Serial.println(message);
  }
}

// ============= Utility Functions =============
int findMacAddressPosition(const char *mac_address) {
  for (int i = 0; i < NUM_CUBE_MAC_ADDRESSES; i++) {
    if (strcmp(mac_address, CUBE_MAC_ADDRESSES[i]) == 0) {
      return i;
    }
  }
  return -1;
}

void configureDisplayFont(MatrixPanel_I2S_DMA* display) {
  display->setFont(&Roboto_Mono_Bold_78);
}

String removeColonsFromMac(const String& mac_address) {
  String result;
  result.reserve(mac_address.length());  // Pre-allocate to avoid reallocations
  for (int i = 0; i < mac_address.length(); i++) {
    if (mac_address.charAt(i) != ':') {
      result += mac_address.charAt(i); 
    }
  }
  return result;
}

String convertNfcIdToHexString(uint8_t* nfc_id, int id_length) {
  String hex_string;
  hex_string.reserve(id_length * 2);  // Pre-allocate for hex string
  for (int i = 0; i < id_length; i++) {
    char hex_chars[3];
    snprintf(hex_chars, sizeof(hex_chars), "%02X", nfc_id[i]);
    hex_string += hex_chars;
  }
  return hex_string;
}

String createMqttTopic(const String& topic_suffix) {
  char topic[128];  // Large enough for any reasonable topic
  snprintf(topic, sizeof(topic), "cube/%s/%s", topic_suffix.c_str(), cube_identifier.c_str());
  return String(topic);
}

// ============= Display Functions =============
void displayLetter(uint16_t vertical_position, char letter, uint16_t color) {
  int16_t row = (PANEL_RES_Y * vertical_position) / 100;
  led_display->setCursor(BIG_COL, row-4);
  led_display->setTextColor(color, BLACK);
  led_display->setTextSize(BIG_TEXT_SIZE);
  led_display->print(letter);
}

void updateDisplay() {
  static long last_highlight_time = 0;
  static uint16_t animation_speed = 1;
  unsigned long current_time = millis();
  uint8_t letter_position = 100;

  if (current_letter != previous_letter) {
    highlight_end_time = 0;
    float animation_duration = current_time - animation_start_time;
    if (current_time - animation_start_time >= letter_animation.duration()) {
      previous_letter = current_letter;
    } else {
      letter_position = letter_animation.get(animation_duration);
    }
    displayLetter(100 + letter_position, previous_letter, RED);
  }
  
  uint16_t current_color = current_time < highlight_end_time ? HIGHLIGHT_LETTER_COLOR : LETTER_COLOR;
  displayLetter(letter_position, current_letter, current_color);

  if (has_card_present) {
    led_display->drawFastVLine(62, 30, 2, 0x7c51);
  }
  if (current_letter == ' ') {
    border_style = ' ';
  }
  if (border_style != ' ') {
    led_display->drawFastHLine(0, 0, PANEL_RES_X, border_color);
    led_display->drawFastHLine(0, 1, PANEL_RES_X, border_color);
    led_display->drawFastHLine(0, PANEL_RES_Y-1, PANEL_RES_X, border_color);
    led_display->drawFastHLine(0, PANEL_RES_Y-2, PANEL_RES_X, border_color);
  } 
  if (border_style == '[') {
    led_display->drawFastVLine(0, 2, PANEL_RES_Y-4, border_color);
    led_display->drawFastVLine(1, 2, PANEL_RES_Y-4, border_color);
  } else if (border_style == ']') {
    led_display->drawFastVLine(PANEL_RES_X-1, 2, PANEL_RES_Y-4, border_color);
    led_display->drawFastVLine(PANEL_RES_X-2, 2, PANEL_RES_Y-4, border_color);
  }
  led_display->flipDMABuffer();    
  led_display->clearScreen();
}

// ============= Hardware Setup Functions =============
void setupNfcReader() {
  if (is_front_display) {
    return;
  }
  SPI.begin(SCK, MISO, MOSI, SS);
  Serial.println(F("Initializing nfc..."));
  nfc_reader.begin();
  nfc_reader.reset();
  Serial.println(F("Enabling RF field..."));
  nfc_reader.setupRF();
}

void setupLedDisplay() {
  display_config.clkphase = false;
  display_config.double_buff = true;
  led_display = new MatrixPanel_I2S_DMA(display_config);
  led_display->begin();
  led_display->setBrightness8(BRIGHTNESS);
  led_display->setRotation(3);
  led_display->setTextWrap(true);
  led_display->clearScreen();
}

// ============= Network Functions =============
uint8_t getCubeIpOctet() {
  String mac_address = WiFi.macAddress();
  int mac_position = findMacAddressPosition(mac_address.c_str());
  if (mac_position == -1) {
    mac_position = 1;
  }
  int cube_index = mac_position - (mac_position % 2);
  cube_identifier = removeColonsFromMac(CUBE_MAC_ADDRESSES[cube_index]);
  is_front_display = (mac_position % 2) == 1;
  Serial.print("mac_address: ");
  Serial.println(mac_address);
  Serial.print("mac_position: ");
  Serial.println(mac_position);
  Serial.print("cube_id: ");
  Serial.println(cube_identifier);
  Serial.print("front display: ");
  Serial.println(is_front_display);
  return 20 + mac_position;
}

void setupWiFiConnection() {
  bool try_portable = true;
  Serial.print("mac address: ");
  Serial.println(WiFi.macAddress());
  
  IPAddress local_IP(192, 168, 0, getCubeIpOctet());
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);   //optional
  IPAddress secondaryDNS(8, 8, 4, 4); //optional

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  while (WiFi.status() != WL_CONNECTED) {
    const char* ssid = try_portable ? SSID_NAME_PORTABLE : SSID_NAME;
    const char* password = try_portable ? WIFI_PASSWORD_PORTABLE : WIFI_PASSWORD;
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    delay(1000);
    try_portable = ! try_portable;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// ============= MQTT Functions =============
void onConnectionEstablished() {
  String topic_cube_cube_id = "cube/" + cube_identifier;
  mqtt_client.subscribe(topic_cube_cube_id + "/sleep", [](const String& message) {
    if (message == "1") {
      debugPrintln("sleeping due to /sleep");
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      esp_deep_sleep_start();
    }
  });
  mqtt_client.subscribe(topic_cube_cube_id + "/reboot", [](const String& message) {
    debugPrintln("rebooting due to /reboot");
    ESP.restart();
  });

  mqtt_client.subscribe(topic_cube_cube_id + "/reset", [](const String& message) {
    debugPrintln("resetting due to /reset");
    nfc_reader.reset();
    nfc_reader.setupRF();
  });

  mqtt_client.subscribe(topic_cube_cube_id + "/letter", [](const String& message) {
    debugPrintln("setting letter due to /letter");
    current_letter = message.charAt(0);
    animation_start_time = millis();
  });

  mqtt_client.subscribe(topic_cube_cube_id + "/flash", [](const String& message) {
    debugPrintln("flashing due to /flash");
    highlight_end_time = millis() + HIGHLIGHT_TIME_MS;
    is_border_word = true;
    border_color = GREEN;
  });
  mqtt_client.subscribe(topic_cube_cube_id + "/border_line", [](const String& message) {
    debugPrintln("setting border line due to /border_line");
    border_style = message.charAt(0);
  });

  mqtt_client.subscribe(topic_cube_cube_id + "/border_color", [](const String& message) {
    debugPrintln("setting border color due to /border_color");
    switch (message.charAt(0)) {
      case 'Y':
        border_color = YELLOW;  
        break;
      case 'W':
        border_color = WHITE;
        break;
      case 'G':
        border_color = GREEN;
        break;
    }
    is_border_word = false;
  });
  mqtt_client.subscribe(topic_cube_cube_id + "/old", [](const String& message) {
    debugPrintln("setting old due to /old");
    border_color = YELLOW;
  });
  mqtt_client.subscribe(topic_cube_cube_id + "/ping", [](const String& message) {
    debugPrintln("pinging due to /ping");
    static String echo_topic = createMqttTopic("echo");
    mqtt_client.publish(echo_topic, message);
  });

  mqtt_client.subscribe(String("game/nfc") + cube_identifier, [](const String& message) {
    debugPrintln("nfc due to /nfc");
    last_neighbor_id = message;
  });  
}

// ============= System Functions =============
uint8_t getWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
  return wakeup_reason;
}

// ============= NFC Functions =============
ISO15693ErrorCode readNfcCard(uint8_t* card_id) {
  return nfc_reader.getInventory(card_id);
}

// ============= Main Functions =============
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(0);
  mqtt_client.enableDebuggingMessages(true);
  debugPrintln("starting....");
  setupLedDisplay();
  displayDebugMessage(VERSION);
  delay(600);
  uint8_t wakeup = getWakeupReason();

  letter_animation.duration(800);
  letter_animation.scale(100);

  displayDebugMessage((String("wake up:") + String(wakeup)).c_str());
  debugPrintln("setting up wifi...");
  setupWiFiConnection();
  Serial.println(cube_identifier);
  static String client_name = cube_identifier + (is_front_display ? "_F" : "");
  Serial.println(client_name);
  mqtt_client.setMqttClientName(client_name.c_str());
  if (is_front_display) {
    led_display->setRotation(2);
  }
  
  char ipDisplay[128];
  snprintf(ipDisplay, sizeof(ipDisplay), "%s, %s,%s", 
    WiFi.localIP().toString().c_str(),
    WiFi.macAddress().c_str(),
    cube_identifier.c_str());
  displayDebugMessage(ipDisplay);
  
  static String nfc_topic = createMqttTopic("nfc");
  nfc_topic_out = nfc_topic.c_str();
  mqtt_client.publish(createMqttTopic("version"), VERSION);

  displayDebugMessage("nfc...");
  debugPrintln(WiFi.macAddress().c_str());

  setupNfcReader();
  led_display->clearScreen();
  configureDisplayFont(led_display);

  debugPrintln(F("Setup Complete"));
}

void loop() {
  mqtt_client.loop();

  if (!is_front_display) {
    nfc_reader.reset();
    nfc_reader.setupRF();
  }

  updateDisplay();
  if (is_front_display) {
    return;
  }

  unsigned long current_time = millis();

  uint8_t card_id[NFCID_LENGTH];
  ISO15693ErrorCode read_result = readNfcCard(card_id);
  if (read_result == ISO15693_EC_OK) {
    String neighbor_id = convertNfcIdToHexString(card_id, sizeof(card_id) / sizeof(card_id[0]));
    if (neighbor_id.equals(last_neighbor_id)) {
      return;
    }
    if (current_time - last_nfc_publish_time < 1000) {
      return;
    }

    debugPrintln(F("New card"));
    mqtt_client.publish(nfc_topic_out, neighbor_id, true);

    last_nfc_publish_time = millis();
  } else if (read_result == EC_NO_CARD) {
    if (last_neighbor_id.equals("")) {
      return;
    }
    if (current_time - last_nfc_publish_time < 1000) {
      return;
    }
    long time_since_last_publish = current_time - last_nfc_publish_time;
    if (time_since_last_publish < 100) { // DEBOUNCE
      debugPrintln("skipping 0: debounce");
      return;
    }
    debugPrintln("publishing no-link");
    mqtt_client.publish(nfc_topic_out, "", true);
    last_nfc_publish_time = millis();
  } else {
    debugPrintln("not ok");
  }
}
