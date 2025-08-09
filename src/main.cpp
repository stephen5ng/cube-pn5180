#include "cube_messages.h"
#include "cube_utilities.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Easing.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <EspMQTTClient.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include "mbedtls/base64.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <secrets.h>
#include "font.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#define MINI false

// ============= Configuration =============
// MAC Address Table moved to cube_utilities.h/.cpp

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
#define PANEL_RES 64
#define PANEL_RES_X PANEL_RES  // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y PANEL_RES  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1   // Total number of panels chained one to another

#define PIXEL_COUNT (PANEL_RES_X * PANEL_RES_Y)
#define IMAGE_SIZE (PIXEL_COUNT * sizeof(uint16_t))

#define BAND_COUNT 4
#define BAND_WIDTH (PANEL_RES_X/BAND_COUNT)
#define BORDER_LINE_COUNT 4

// Pin Definitions
#if MINI
  #define MISO 34
#else
  #define MISO 39
#endif

#if MINI
  #define PN5180_BUSY 35
#else
  #define PN5180_BUSY 36
#endif
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
#define NFC_DEBOUNCE_TIME_MS 200
#define NFC_MIN_PUBLISH_INTERVAL_MS 100
#define ANIMATION_DURATION_MS 800
#define ANIMATION_SCALE 100
#define DISPLAY_STARTUP_DELAY_MS 600

// Sleep Configuration
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */

// MQTT Configuration
#define MQTT_SERVER_PI "192.168.8.247"
#define MQTT_PORT 1883

// MQTT Topic Prefixes moved to cube_utilities.h/.cpp

// ============= Global Variables =============
bool is_front_display = false;

// HUB75 Display Configuration
HUB75_I2S_CFG::i2s_pins display_pins = {
  0,  //R1_PIN,
  0,  //G1_PIN,
  0,  //B1_PIN,
  0,  //R2_PIN,
  0,  //G2_PIN,
  0,  //B2_PIN,
  19,  //A_PIN,
  21,  //B_PIN,
  4,   //C_PIN,
  22,  //D_PIN,
  12,  //E_PIN,
  2,   //LAT_PIN,
  15,  //OE_PIN,
  16,  //CLK_PIN
};

int8_t rgb_large[] = {25, 26, 33, 13, 27, 14};
int8_t rgb_small[] = {33, 26, 25, 14, 27, 13};

int8_t* rgb_pins[] = {
  rgb_small,
  rgb_small,
  rgb_small,
  rgb_small,
  rgb_small,
  rgb_small};

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

// UDP Configuration
#define UDP_PORT 54321  // Port for ping-pong
WiFiUDP udp;
char udpBuffer[255];

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
  bool is_front;
  bool is_image_mode;
  uint16_t* image1;
  uint16_t* image2;
  uint16_t* image;
  uint16_t* previous_image;
  String display_string;
  uint8_t border_style_left;
  uint8_t border_style_right;
  bool is_border_word;
  uint16_t border_color;
  unsigned long animation_start_time;
  long highlight_end_time;
  bool is_lock;
  uint8_t percent_complete;
  uint16_t current_letter_color;
  uint16_t vline_color_right;
  uint16_t vline_color_left;
  uint8_t vline_height;
  uint16_t hline_color_top;
  uint16_t hline_color_bottom;
  EasingFunc<Ease::BounceOut> letter_animation;
  const GFXfont* current_font;
  uint8_t text_size;
  uint8_t rotation;
  uint8_t font_size;
  bool is_dirty;
  char previous_letter;
  char current_letter;

public:
  DisplayManager(bool is_front, String cube_id) : is_front(is_front), is_image_mode(false), is_dirty(true), 
                                is_border_word(false),
                                animation_start_time(0), highlight_end_time(0), percent_complete(100),
                                current_letter_color(LETTER_COLOR), current_font(&Roboto_Mono_Bold_78),
                                text_size(1), rotation(2), font_size(1), is_lock(false),
                                border_style_left(0), border_style_right(0),
                                vline_color_left(0), vline_color_right(0),
                                vline_height(PANEL_RES),
                                hline_color_top(0),
                                hline_color_bottom(0),
                                image1(nullptr), image2(nullptr), image(nullptr), previous_image(nullptr), 
                                previous_letter(' '), current_letter('?') {
    setupDisplay(cube_id);
    letter_animation.duration(ANIMATION_DURATION_MS);
    letter_animation.scale(ANIMATION_SCALE);

    // Allocate image buffers. Failure is fatal.
    image = image1 = new uint16_t[PIXEL_COUNT];
    previous_image = image2 = new uint16_t[PIXEL_COUNT];
    memset(image1, 0, PIXEL_COUNT * sizeof(uint16_t));
    memset(image2, 0, PIXEL_COUNT * sizeof(uint16_t));
  }

  void setupDisplay(String cube_id) {
    display_config.clkphase = false;
    display_config.double_buff = true;
    int8_t* rgb = rgb_pins[cube_id.toInt()-1];
    display_config.gpio.r1 = rgb[0];
    display_config.gpio.g1 = rgb[1];
    display_config.gpio.b1 = rgb[2];
    display_config.gpio.r2 = rgb[3];
    display_config.gpio.g2 = rgb[4];
    display_config.gpio.b2 = rgb[5];
    led_display = new MatrixPanel_I2S_DMA(display_config);
    led_display->begin();
    led_display->setBrightness(BRIGHTNESS);
    led_display->setRotation(rotation);
    led_display->setTextWrap(true);
    led_display->clearScreen();
    led_display->setFont(current_font);
    led_display->setTextSize(text_size);
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

  void animate(unsigned long current_time) {
    static uint16_t last_letter_color = -1;

    current_letter_color = current_time < highlight_end_time ? HIGHLIGHT_LETTER_COLOR : LETTER_COLOR;
    if (is_lock) {
      current_letter_color = YELLOW;
    }
    if (last_letter_color != current_letter_color) {
      last_letter_color = current_letter_color;
      is_dirty = true;
    }

    if (previous_letter != current_letter || previous_image != image) {
      static uint8_t previous_percent_complete = -1;
      if (current_time - animation_start_time >= letter_animation.duration()) {
        // complete animation
        previous_image = image;
        previous_letter = current_letter;
        percent_complete = ANIMATION_SCALE;
        is_dirty = true;
      } 
      else {
        // animation in progress
        percent_complete = letter_animation.get(current_time - animation_start_time);
        if (percent_complete != previous_percent_complete) {
            previous_percent_complete = percent_complete;
            is_dirty = true;
        }
      }
    }
  }

  void drawLetter(uint16_t vertical_position, char letter, uint16_t color) {
    // Serial.println("displayLetter");
    int16_t row = (PANEL_RES_Y * vertical_position) / 100;
    led_display->setTextColor(color, BLACK);
    led_display->setTextSize(BIG_TEXT_SIZE);
    led_display->setCursor(BIG_COL, row-4);
    led_display->print(letter);
  }
    
  void drawBorderFrame() {
    drawBorders(true, true, hline_color_top);
    drawBorders(true, false, hline_color_bottom);
    drawBorders(false, true, vline_color_left);
    drawBorders(false, false, vline_color_right);
  }

  void drawBorders(bool isHorizontal, bool isTopLeft, uint16_t color) {
    if (color == 0) {
      return;
    }
    for (uint8_t line = 0; line < BORDER_LINE_COUNT/2; line++) {
      uint16_t pos;
      if (isTopLeft) {
        pos = line;  // Top/left two lines
      } else {
        pos = PANEL_RES - BORDER_LINE_COUNT/2 + line;  // Bottom/right two lines
      }
      if (isHorizontal) {
        led_display->drawFastHLine(0, pos, PANEL_RES_X, color);
      } else {
        led_display->drawFastVLine(pos, 
          PANEL_RES_Y - vline_height, vline_height, color);
      }
    }
  }

  void handleBorderFrameCommand(const String& message) {
    debugPrintln("setting border frame due to /border_frame");
    handleBorderTopBannerCommand(message);
    handleBorderBottomBannerCommand(message);
    handleBorderVLineLeftCommand(message);
    handleBorderVLineRightCommand(message);
    is_dirty = true;
  }

  void handleBorderVLineRightCommand(const String& message) {
    debugPrintln("setting border vline right color due to /border_vline_right");
    vline_color_right = strtol(message.c_str(), NULL, 16);
    is_dirty = true;
  }

  void handleBorderVLineLeftCommand(const String& message) {
    debugPrintln("setting border vline left color due to /border_vline_left");
    vline_color_left = strtol(message.c_str(), NULL, 16);
    is_dirty = true;
  }

  void handleBorderLineHeightCommand(const String& message) {
    debugPrintln("setting border vline height due to /border_vline_height");
    vline_height = message.length() == 0 ? PANEL_RES_Y : message.toInt();
    is_dirty = true;
  }

  void handleFlashCommand(const String& message) {
    if (message.length() <= 0) {
      return;
    }
    debugPrintln("flashing due to /flash");
    highlight_end_time = millis() + HIGHLIGHT_TIME_MS;
    is_dirty = true;
  }

  void handleFontSizeCommand(const String& message) {
    debugPrintln("setting font size due to /font_size");
    // if (!is_image_mode) {
    //   debugPrintln("ignoring font size change in image mode");
    //   return;
    // }

    if (message.length() <= 0) {
      return;
    }

    int size = max(0L, message.toInt());
    font_size = size;
    is_dirty = true;
  }

  void handleLockCommand(const String& message) {
    debugPrintln("locking due to /lock");
    is_lock = message.length() > 0 && message.charAt(0) == '1';
    Serial.println(is_lock);
    Serial.println(message);
    is_dirty = true;
  }

  void drawImage(int8_t percent_complete, uint16_t* image) {
    // debugPrintln("drawImage");
    // Serial.printf("image_position: %d\n", image_position);
    // Serial.printf("image: %p\n", image);
    int16_t row = (PANEL_RES_Y * percent_complete) / 100;
    led_display->drawRGBBitmap(0, row, image, 64, 64);
  }

  void updateDisplay(unsigned long current_time) {
    if (!is_dirty) {
      return;
    }

    led_display->setFont(current_font);
    led_display->setTextSize(text_size);
    led_display->setRotation(rotation);

    if (is_image_mode) {
      // Serial.printf("image: %p, previous_image: %p\n", image, previous_image);
      if (image != previous_image) {
        drawImage(-percent_complete, previous_image);
      }
      drawImage(100 - percent_complete, image);
    } else {    
      if (current_letter != previous_letter) {
        drawLetter(100 + percent_complete, previous_letter, RED);
      }
      drawLetter(percent_complete, current_letter, current_letter_color);
    } 

    if (display_string.length() > 0) {
      Serial.println("displaying string");
      Serial.println(display_string);
      led_display->setCursor(5, 28);
      led_display->setTextColor(RED, BLACK);
      led_display->print(display_string);
    }

    drawBorderFrame();
    led_display->flipDMABuffer();    
    led_display->clearScreen();
    is_dirty = false;
  }


  void handleBrightnessCommand(const String& message) {
    debugPrintln("setting brightness due to /brightness");
    uint16_t brightness = message.toInt();
    led_display->setBrightness(brightness);
  }

  void handleImageBinaryCommand(const String& message) {
    Serial.println("handling binary image");
    Serial.printf("message length: %d\n", message.length());
    if (message.length() > IMAGE_SIZE) {
      Serial.println("Image too large");
      return;
    }
    
    static unsigned long last_message_time = 0;
    is_image_mode = true;

    previous_image = image;
    image = (image == image1) ? image2 : image1;

    animation_start_time = millis();

    memcpy(image, message.c_str(), message.length());
    is_dirty = true;
  }

  void handleBorderTopBannerCommand(const String& message) {
    debugPrintln("setting border top banner due to /border_top_banner");
    Serial.println(message);
    hline_color_top = strtol(message.c_str(), NULL, 16);
    is_dirty = true;  
  }

  void handleBorderBottomBannerCommand(const String& message) {
    debugPrintln("setting border bottom banner due to /border_bottom_banner");
    Serial.println(message);
    hline_color_bottom = strtol(message.c_str(), NULL, 16);    
    is_dirty = true;  
  }

  void handleBorderSideCommand(const String& message) {
    debugPrintln("setting border side due to /border_side");
    char border_style_side = message.charAt(0);
    // < ( {
    switch (border_style_side)
    {
      case '<':
        border_style_left = 0;
        break;
      case '(':
        border_style_left = 1;
        break;
      case '{':
        border_style_left = 2;
        break;
        case '>':
        border_style_right = 0;
        break;
      case ')':
        border_style_right = 1;
        break;
      case '}':
        border_style_right = 2;
        break;
      default:
        break;
    }
    is_dirty = true;  
  }

  void handleLetterCommand(const String& message) {
    static unsigned long last_message_time = 0;
    unsigned long current_time = millis();
    unsigned long time_since_last = current_time - last_message_time;
    
    if (time_since_last > 1000) {
        Serial.println("----------------------------------------");
        Serial.printf("[%lu] WARNING: %lu ms since last message\n", current_time, time_since_last);
        Serial.println("----------------------------------------");
    }
    
    last_message_time = current_time;
    
    Serial.printf("[%lu] MQTT message received - Topic: letter, Payload: %s\n", current_time, message.c_str());
    
    debugPrintln("setting letter due to /letter");
    if (previous_letter != current_letter) {
      previous_letter = current_letter;
    }
      
    is_image_mode = false;
    current_letter = message.length() > 0 ? message.charAt(0) : '?';
    animation_start_time = millis();
    current_font = &Roboto_Mono_Bold_78;  // Restore custom font for letter mode
    text_size = 1;  // Always use size 1 for letter mode
    // rotation = is_front ? 2 : 0;  // Restore original rotation for letter mode
    is_dirty = true;
  }

  void handleStringCommand(const String& message) {
    debugPrintln("setting string due to /string");
    display_string = message;
    current_font = nullptr;  // Use default font for string mode
    is_dirty = true;
  }
};


// ============= Global Variables =============
DisplayManager* display_manager;

// ============= Utility Functions =============
// Utility functions moved to cube_utilities.h/.cpp

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
    mac_position = 21;
  }
  int cube_index = mac_position - (mac_position % 2);
  cube_identifier = 1 + mac_position / 2;
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
  Serial.print("ip octet: ");
  Serial.println(getCubeIpOctet());
  IPAddress local_IP(192, 168, 8, getCubeIpOctet());
  Serial.print("local IP: ");
  Serial.println(local_IP);
  IPAddress gateway(192, 168, 8, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

  while (WiFi.status() != WL_CONNECTED) {
    const char* ssid = try_portable ? SSID_NAME_PORTABLE : SSID_NAME;
    const char* password = try_portable ? WIFI_PASSWORD_PORTABLE : WIFI_PASSWORD;
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    delay(2000);
    // try_portable = ! try_portable;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void handleNfcCommand(const String& message) {
  debugPrintln("nfc due to /nfc");
  strncpy(last_neighbor_id, message.c_str(), sizeof(last_neighbor_id) - 1);
  last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
}

void handlePingCommand(const String& message) {
  if (message.length() == 0) {
    return;
  }
  debugPrintln("pinging due to /ping");
  mqtt_client.publish(mqtt_topic_echo, message);
}

void handleRebootCommand(const String& message) {
  if (message.length() == 0) {
    return;
  }
  debugPrintln("rebooting due to /reboot");
  ESP.restart();
}

void handleResetCommand(const String& message) {
  if (message.length() == 0) {
    return;
  }
  debugPrintln("resetting due to /reset");
  nfc_reader.reset();
  nfc_reader.setupRF();
}

void handleSleepCommand(const String& message) {
  if (message == "1") {
    debugPrintln("sleeping due to /sleep");
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
  }
}

void onConnectionEstablished() {
  // Pre-allocate common topics
  mqtt_topic_cube = MQTT_TOPIC_PREFIX_CUBE + cube_identifier;
  mqtt_topic_cube_nfc = String(MQTT_TOPIC_PREFIX_CUBE) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_game_nfc = String(MQTT_TOPIC_PREFIX_GAME) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_echo = createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_ECHO);
  mqtt_topic_version = createMqttTopic(cube_identifier, "version");
  
  // Subscribe to all command topics
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "brightness", [](const String& msg) { display_manager->handleBrightnessCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "reboot", handleRebootCommand );
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "border_bottom_banner", [](const String& msg) { display_manager->handleBorderBottomBannerCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "border_top_banner", [](const String& msg) { display_manager->handleBorderTopBannerCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "sleep", handleSleepCommand);
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "string", [](const String& msg) { display_manager->handleStringCommand(msg); });

  mqtt_client.subscribe(mqtt_topic_cube + "/border_hline_bottom", [](const String& msg) { display_manager->handleBorderBottomBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_hline_top", [](const String& msg) { display_manager->handleBorderTopBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_frame", [](const String& msg) { display_manager->handleBorderFrameCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_height", [](const String& msg) { display_manager->handleBorderLineHeightCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_left", [](const String& msg) { display_manager->handleBorderVLineLeftCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_right", [](const String& msg) { display_manager->handleBorderVLineRightCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/font_size", [](const String& msg) { display_manager->handleFontSizeCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/flash", [](const String& msg) { display_manager->handleFlashCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/imagex", [](const String& msg) { display_manager->handleImageBinaryCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/letter", [](const String& msg) { display_manager->handleLetterCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/lock", [](const String& msg) { display_manager->handleLockCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/ping", handlePingCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/reboot", handleRebootCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/reset", handleResetCommand);
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

void setupUDP() {
  udp.begin(UDP_PORT);
  Serial.printf("UDP server listening on port %d\n", UDP_PORT);
}

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // Serial.printf("packetSize: %d\n", packetSize);
    int len = udp.read(udpBuffer, sizeof(udpBuffer)-1);
    // Serial.printf("len: %d\n", len);
    if (len > 0) {
      udpBuffer[len] = 0; // Null terminate
      
      // Check if message is "ping"
      if (strcmp(udpBuffer, "ping") == 0) {
        // Send "pong" back to sender
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)"pong", 4);
        udp.endPacket();
        
        // Serial.printf("Received ping from %s:%d\n", udp.remoteIP().toString().c_str(), udp.remotePort());
      }
      // Check if message is "rssi"
      else if (strcmp(udpBuffer, "rssi") == 0) {
        char rssiStr[32];
        snprintf(rssiStr, sizeof(rssiStr), "%d", WiFi.RSSI());
        
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)rssiStr, strlen(rssiStr));
        udp.endPacket();
        
        // Serial.printf("Sent RSSI to %s:%d: %s\n", udp.remoteIP().toString().c_str(), udp.remotePort(), rssiStr);
      }
    }
  }
}

// ============= Main Functions =============
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(0);

  debugPrintln("starting....");
  Serial.print("Chip Model: ");
  Serial.println(ESP.getChipModel());

  Serial.print("Chip Revision: ");
  Serial.println(ESP.getChipRevision());

  mqtt_client.enableDebuggingMessages(true);
  mqtt_client.setMaxPacketSize(11999);
  Serial.printf("memory available: %d\n", ESP.getFreeHeap());
  mqtt_client.enableDebuggingMessages(false);
  mqtt_client.setMqttReconnectionAttemptDelay(5);
  mqtt_client.enableOTA();
  
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  Serial.printf("Model: %d, Cores: %d, Revision: %d\n", chip_info.model, chip_info.cores, chip_info.revision);

  // Initialize watchdog timer
  // esp_task_wdt_init(10, true); 
  // esp_task_wdt_add(NULL);      // Add current thread to WDT watch
  
  // Initialize WiFi and get cube identifier
  debugPrintln("setting up wifi...");
  setupWiFiConnection();
  debugPrintln("wifi done");
  
  String cube_id = cube_identifier;
  bool is_front = (findMacAddressPosition(WiFi.macAddress().c_str()) % 2) == 1;
  
  // Create display manager
  display_manager = new DisplayManager(is_front, cube_id);
  display_manager->displayDebugMessage(VERSION);
  delay(DISPLAY_STARTUP_DELAY_MS);
  uint8_t wakeup = getWakeupReason();

  display_manager->displayDebugMessage((String("wake up:") + String(wakeup)).c_str());
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

  display_manager->displayDebugMessage("nfc...");
  debugPrintln(WiFi.macAddress().c_str());

  debugPrintln("setting up nfc reader...");
  setupNfcReader();
  debugPrintln("nfc reader done");

  display_manager->clearScreen();

  debugPrintln("setting up udp...");
  setupUDP(); // Add UDP setup

  debugPrintln(F("Setup Complete"));
}

void loop() {
  mqtt_client.loop();
  esp_task_wdt_reset();  // Feed the watchdog timer
  display_manager->animate(millis());
  display_manager->updateDisplay(millis());
  handleUDP();

  if (is_front_display) {
    return;
  }
  nfc_reader.reset();
  nfc_reader.setupRF();

  unsigned long current_time = millis();

  uint8_t card_id[NFCID_LENGTH];
  ISO15693ErrorCode read_result = readNfcCard(card_id);
  if (read_result == ISO15693_EC_OK) {
    char neighbor_id[NFCID_LENGTH * 2 + 1];
    convertNfcIdToHexString(card_id, sizeof(card_id) / sizeof(card_id[0]), neighbor_id);
    if (strcmp(neighbor_id, last_neighbor_id) == 0) {
      return;  // No change in state, don't publish
    }

    debugPrintln(F("New card"));
    unsigned long publish_start = millis();
    bool success = mqtt_client.publish(mqtt_topic_cube_nfc, neighbor_id, true);
    unsigned long publish_end = millis();
    Serial.printf("[%lu] MQTT publish took %lu ms - payload: %s - success: %d\n", publish_end, publish_end - publish_start, neighbor_id, success);
    if (success) {
      strncpy(last_neighbor_id, neighbor_id, sizeof(last_neighbor_id) - 1);
      last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
    }
  } else if (read_result == EC_NO_CARD) {
    // Only publish if we had a card before and now we don't
    if (last_neighbor_id[0] != '\0') {
      debugPrintln(F("Card removed"));
      unsigned long publish_start = millis();
      bool success = mqtt_client.publish(mqtt_topic_cube_nfc, "", true);
      unsigned long publish_end = millis();
      Serial.printf("[%lu] MQTT publish took %lu ms - empty payload, success: %d\n", publish_end, publish_end - publish_start, success);
      if (success) {
        last_neighbor_id[0] = '\0';  // Clear the neighbor ID
      }
    }
  } else {
    debugPrintln(F("not ok"));
  }
}
