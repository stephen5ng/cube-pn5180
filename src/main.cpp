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
#include "esp_system.h"
#include "esp_task_wdt.h"

// ============= Configuration =============
// MAC Address Table
const char *CUBE_MAC_ADDRESSES[] = {
  "C4:DD:57:8E:46:C8", "94:54:C5:EE:87:F0",
  "CC:DB:A7:92:9A:A0", "D8:BC:38:FD:E0:98",
  "CC:DB:A7:98:54:2C", "CC:DB:A7:99:0F:E0",
  "CC:DB:A7:9F:C2:84", "D8:BC:38:FD:D0:BC",
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

// Display Colors
#define LETTER_COLOR 0xFDCC
#define HIGHLIGHT_LETTER_COLOR GREEN
#define LAST_LETTER_COLOR 0x71c0
#define CARD_INDICATOR_COLOR 0x7c51

// Display Dimensions
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
#define VERSION "v0.8a"
#define PRINT_DEBUG true

// Timing Constants
#define NFC_DEBOUNCE_TIME_MS 1000
#define NFC_MIN_PUBLISH_INTERVAL_MS 100
#define ANIMATION_DURATION_MS 800
#define ANIMATION_SCALE 100
#define DISPLAY_STARTUP_DELAY_MS 600

// Sleep Configuration
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */

// MQTT Configuration
#define MQTT_SERVER_PI "192.168.0.247"
#define MQTT_PORT 1883

// MQTT Topic Prefixes
const char* MQTT_TOPIC_PREFIX_CUBE = "cube/";
const char* MQTT_TOPIC_PREFIX_GAME = "game/";
const char* MQTT_TOPIC_PREFIX_NFC = "nfc/";
const char* MQTT_TOPIC_PREFIX_ECHO = "echo";

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
// MatrixPanel_I2S_DMA *led_display;
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
uint16_t border_color = WHITE;
bool is_border_word = false;
String previous_string;

// Network Objects
EspMQTTClient mqtt_client(
  MQTT_SERVER_PI,
  MQTT_PORT,
  "",
  "",
  ""
);
WiFiClient wifi_client;
static String cube_identifier;
const char* nfc_topic_out;

// Animation
char last_neighbor_id[NFCID_LENGTH * 2 + 1] = "INIT";
unsigned long last_nfc_publish_time = 0;

// Pre-allocated MQTT topics
String mqtt_topic_cube;
String mqtt_topic_cube_nfc;
String mqtt_topic_game_nfc;
String mqtt_topic_echo;
String mqtt_topic_version;

// ============= Debug Functions =============
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

// ============= DisplayManager Class =============
class DisplayManager {
private:
  MatrixPanel_I2S_DMA* led_display;
  static long last_highlight_time;
  static uint16_t animation_speed;
  bool is_front;
  bool is_string_mode;
  String display_string;
  char border_style;
  bool is_border_word;
  uint16_t border_color;
  unsigned long animation_start_time;
  long highlight_end_time;
  uint8_t letter_position;
  uint16_t current_letter_color;
  EasingFunc<Ease::BounceOut> letter_animation;

public:
  bool is_dirty;
  DisplayManager(bool is_front) : is_front(is_front), is_string_mode(false), is_dirty(true), 
                                border_style(' '), is_border_word(false), border_color(WHITE),
                                animation_start_time(0), highlight_end_time(0), letter_position(100),
                                current_letter_color(LETTER_COLOR) {
    setupDisplay();
    letter_animation.duration(ANIMATION_DURATION_MS);
    letter_animation.scale(ANIMATION_SCALE);
  }

  void setupDisplay() {
    display_config.clkphase = false;
    display_config.double_buff = true;
    led_display = new MatrixPanel_I2S_DMA(display_config);
    led_display->begin();
    led_display->setBrightness8(BRIGHTNESS);
    led_display->setRotation(is_front ? 2 : 3);
    led_display->setTextWrap(true);
    led_display->clearScreen();
    configureDisplayFont(led_display);
  }

  void configureDisplayFont(MatrixPanel_I2S_DMA* display) {
    display->setFont(&Roboto_Mono_Bold_78);
  }
  
  void clearScreen() {
    led_display->clearScreen();
  }

  void displayDebugMessage(const char* message) {
    led_display->setCursor(0, 0);
    led_display->setTextColor(RED, BLACK);
    led_display->print(message);
    led_display->flipDMABuffer();    
    led_display->clearScreen();
  }

  void displayLetter(uint16_t vertical_position, char letter, uint16_t color) {
    // Serial.println("displayLetter");
    int16_t row = (PANEL_RES_Y * vertical_position) / 100;
    led_display->setCursor(BIG_COL, row-4);
    led_display->setTextColor(color, BLACK);
    led_display->setTextSize(BIG_TEXT_SIZE);
    led_display->print(letter);
  }

  void drawBorder(char style, uint16_t color) {
    if (style == ' ') {
      return;
    }
    Serial.println("draw border");
    // Draw horizontal border lines
    led_display->drawFastHLine(0, 0, PANEL_RES_X, color);
    led_display->drawFastHLine(0, 1, PANEL_RES_X, color);
    led_display->drawFastHLine(0, PANEL_RES_Y-1, PANEL_RES_X, color);
    led_display->drawFastHLine(0, PANEL_RES_Y-2, PANEL_RES_X, color);

    // Draw vertical border lines if needed
    if (style == '[') {
      led_display->drawFastVLine(0, 2, PANEL_RES_Y-4, color);
      led_display->drawFastVLine(1, 2, PANEL_RES_Y-4, color);
    } else if (style == ']') {
      led_display->drawFastVLine(PANEL_RES_X-1, 2, PANEL_RES_Y-4, color);
      led_display->drawFastVLine(PANEL_RES_X-2, 2, PANEL_RES_Y-4, color);
    }
  }

  void handleFlashCommand(const String& message) {
    debugPrintln("flashing due to /flash");
    highlight_end_time = millis() + HIGHLIGHT_TIME_MS;
    is_border_word = true;
    border_color = GREEN;
    is_dirty = true;
  }

  void handleBorderColorCommand(const String& message) {
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
    is_dirty = true;  
  }

  void handleOldCommand(const String& message) {
    debugPrintln("setting old due to /old");
    border_color = YELLOW;
    is_dirty = true;  
  }

  void animate(unsigned long current_time) {
    static uint16_t last_letter_color = -1;

    current_letter_color = current_time < highlight_end_time ? HIGHLIGHT_LETTER_COLOR : LETTER_COLOR;
    if (last_letter_color != current_letter_color) {
      last_letter_color = current_letter_color;
      is_dirty = true;
    }

    if (previous_letter != current_letter) {
      static uint8_t previous_letter_position = -1;
      if (current_time - animation_start_time >= letter_animation.duration()) {
        previous_letter = current_letter;
        letter_position = ANIMATION_SCALE;
        is_dirty = true;
      } 
      else {
        letter_position = letter_animation.get(current_time - animation_start_time);

        if (letter_position != previous_letter_position) {
            previous_letter_position = letter_position;
            is_dirty = true;
        }
      }
    }
  }

  void updateDisplay(unsigned long current_time) {
    if (!is_dirty) {
      return;
    }

    if (is_string_mode) {
      led_display->setCursor(0, BIG_ROW);
      led_display->setTextColor(LETTER_COLOR, BLACK);
      led_display->print(display_string);
    } else {    
      if (current_letter != previous_letter) {
        displayLetter(100 + letter_position, previous_letter, RED);
      }
      displayLetter(letter_position, current_letter, current_letter_color);
    }

    if (current_letter == ' ' && !is_string_mode) {
      border_style = ' ';
    }
    drawBorder(border_style, border_color);

    // Update display
    led_display->flipDMABuffer();    
    led_display->clearScreen();
    is_dirty = false;
  }

  void handleFontSizeCommand(const String& message) {
    debugPrintln("setting font size due to /font_size");
    if (!is_string_mode) {
      debugPrintln("ignoring font size change in letter mode");
      return;
    }
    int size = message.toInt();
    if (size > 0) {
      led_display->setTextSize(size);
    }
  }

  void handleStringCommand(const String& message) {
    debugPrintln("setting string due to /string");
    is_string_mode = true;
    display_string = message;
    led_display->setFont(nullptr);  // Use default font for string mode
    led_display->setRotation(is_front ? 0 : 3);    // Set rotation to 0 for string mode
    is_dirty = true;
  }

  void handleLetterCommand(const String& message) {
    debugPrintln("setting letter due to /letter");
    is_string_mode = false;
    current_letter = message.charAt(0);
    animation_start_time = millis();
    configureDisplayFont(led_display);  // Restore custom font for letter mode
    led_display->setTextSize(1);  // Always use size 1 for letter mode
    led_display->setRotation(is_front ? 2 : 3);  // Restore original rotation for letter mode
    is_dirty = true;
  }

  void handleBorderLineCommand(const String& message) {
    debugPrintln("setting border line due to /border_line");
    border_style = message.charAt(0);
    is_dirty = true;  
  }

  MatrixPanel_I2S_DMA* getDisplay() {
    return led_display;
  }
};

// Static member initialization
long DisplayManager::last_highlight_time = 0;
uint16_t DisplayManager::animation_speed = 1;

// ============= Global Variables =============
DisplayManager* display_manager;

// ============= Utility Functions =============
int findMacAddressPosition(const char *mac_address) {
  for (int i = 0; i < NUM_CUBE_MAC_ADDRESSES; i++) {
    if (strcmp(mac_address, CUBE_MAC_ADDRESSES[i]) == 0) {
      return i;
    }
  }
  return -1;
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

void convertNfcIdToHexString(uint8_t* nfc_id, int id_length, char* hex_buffer) {
  for (int i = 0; i < id_length; i++) {
    snprintf(hex_buffer + (i * 2), 3, "%02X", nfc_id[i]);
  }
  hex_buffer[id_length * 2] = '\0';  // Ensure null termination
}

String createMqttTopic(const char* suffix) {
  static String topic;
  topic = MQTT_TOPIC_PREFIX_CUBE;
  topic += cube_identifier;
  topic += '/';
  topic += suffix;
  return topic;
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
void handleSleepCommand(const String& message) {
  if (message == "1") {
    debugPrintln("sleeping due to /sleep");
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
  }
}

void handleRebootCommand(const String& message) {
  debugPrintln("rebooting due to /reboot");
  ESP.restart();
}

void handleResetCommand(const String& message) {
  debugPrintln("resetting due to /reset");
  nfc_reader.reset();
  nfc_reader.setupRF();
}

void handlePingCommand(const String& message) {
  debugPrintln("pinging due to /ping");
  mqtt_client.publish(mqtt_topic_echo, message);
}

void handleNfcCommand(const String& message) {
  debugPrintln("nfc due to /nfc");
  strncpy(last_neighbor_id, message.c_str(), sizeof(last_neighbor_id) - 1);
  last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
}

void onConnectionEstablished() {
  // Pre-allocate common topics
  mqtt_topic_cube = MQTT_TOPIC_PREFIX_CUBE + cube_identifier;
  mqtt_topic_cube_nfc = String(MQTT_TOPIC_PREFIX_CUBE) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_game_nfc = String(MQTT_TOPIC_PREFIX_GAME) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_echo = createMqttTopic(MQTT_TOPIC_PREFIX_ECHO);
  mqtt_topic_version = createMqttTopic("version");
  
  // Subscribe to all command topics
  mqtt_client.subscribe(mqtt_topic_cube + "/sleep", handleSleepCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/reboot", handleRebootCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/reset", handleResetCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/letter", [](const String& msg) { display_manager->handleLetterCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/string", [](const String& msg) { display_manager->handleStringCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/font_size", [](const String& msg) { display_manager->handleFontSizeCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/flash", [](const String& msg) { display_manager->handleFlashCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_line", [](const String& msg) { display_manager->handleBorderLineCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_color", [](const String& msg) { display_manager->handleBorderColorCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/old", [](const String& msg) { display_manager->handleOldCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/ping", handlePingCommand);
  mqtt_client.subscribe(mqtt_topic_game_nfc, handleNfcCommand);
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
  mqtt_client.setMqttReconnectionAttemptDelay(5);
  debugPrintln("starting....");
  
  // Initialize watchdog timer
  esp_task_wdt_init(10, true);  // 5 second timeout, panic on timeout
  esp_task_wdt_add(NULL);      // Add current thread to WDT watch
  
  // Initialize WiFi and get cube identifier
  setupWiFiConnection();
  String cube_id = cube_identifier;
  bool is_front = (findMacAddressPosition(WiFi.macAddress().c_str()) % 2) == 1;
  
  // Create display manager
  display_manager = new DisplayManager(is_front);
  display_manager->displayDebugMessage(VERSION);
  delay(DISPLAY_STARTUP_DELAY_MS);
  uint8_t wakeup = getWakeupReason();

  display_manager->displayDebugMessage((String("wake up:") + String(wakeup)).c_str());
  debugPrintln("setting up wifi...");
  Serial.println(cube_id);
  static String client_name = cube_id + (is_front ? "_F" : "");
  Serial.println(client_name);
  mqtt_client.setMqttClientName(client_name.c_str());
  
  char ipDisplay[128];
  snprintf(ipDisplay, sizeof(ipDisplay), "%s, %s,%s", 
    WiFi.localIP().toString().c_str(),
    WiFi.macAddress().c_str(),
    cube_id.c_str());
  display_manager->displayDebugMessage(ipDisplay);
  
  mqtt_client.publish(mqtt_topic_version, VERSION);

  display_manager->displayDebugMessage("nfc...");
  debugPrintln(WiFi.macAddress().c_str());

  setupNfcReader();
  display_manager->clearScreen();

  debugPrintln(F("Setup Complete"));
}

void loop() {
  esp_task_wdt_reset();  // Feed the watchdog timer
  
  mqtt_client.loop();
  if (!is_front_display) {
    nfc_reader.reset();
    nfc_reader.setupRF();
  }
  display_manager->animate(millis());
  display_manager->updateDisplay(millis());

  if (is_front_display) {
    return;
  }

  unsigned long current_time = millis();

  uint8_t card_id[NFCID_LENGTH];
  ISO15693ErrorCode read_result = readNfcCard(card_id);
  if (read_result == ISO15693_EC_OK) {
    char neighbor_id[NFCID_LENGTH * 2 + 1];
    convertNfcIdToHexString(card_id, sizeof(card_id) / sizeof(card_id[0]), neighbor_id);
    if (strcmp(neighbor_id, last_neighbor_id) == 0) {
      return;
    }
    if (current_time - last_nfc_publish_time < NFC_DEBOUNCE_TIME_MS) {
      return;
    }

    debugPrintln(F("New card"));
    mqtt_client.publish(mqtt_topic_cube_nfc, neighbor_id, true);
    strncpy(last_neighbor_id, neighbor_id, sizeof(last_neighbor_id) - 1);
    last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';

    last_nfc_publish_time = millis();
  } else if (read_result == EC_NO_CARD) {
    if (current_time - last_nfc_publish_time < NFC_DEBOUNCE_TIME_MS) {
      return;
    }
    long time_since_last_publish = current_time - last_nfc_publish_time;
    if (time_since_last_publish < NFC_MIN_PUBLISH_INTERVAL_MS) { // DEBOUNCE
      debugPrintln(F("skipping 0: debounce"));
      return;
    }
    debugPrintln(F("publishing no-link"));
    mqtt_client.publish(mqtt_topic_cube_nfc, "", true);
    last_neighbor_id[0] = '\0';  // Clear the neighbor ID
    last_nfc_publish_time = millis();
  } else {
    debugPrintln(F("not ok"));
  }
}
