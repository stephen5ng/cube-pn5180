// #include "cube_messages.h"
typedef struct MessageLetter {
  char letter;
  char secret;
} MessageLetter;

#define NFCID_LENGTH 8

typedef struct MessageNfcId {
  char id[NFCID_LENGTH*2 + 1];
} MessageNfcId;
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
#include "cube_tags.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "driver/rtc_io.h"

// ============= Configuration =============
// Hardware pin configuration is determined at compile time by board type:
//   BOARD_V6 (v6 board): MISO=34, PN5180_BUSY=35, A_PIN=19, GPIO5=TPS22975 power switch
//   Default (v1 board):  MISO=39, PN5180_BUSY=36, A_PIN=19
// Pin definitions (set by configurePins based on board type)
static int miso_pin = 0;        // Will be set by configurePins()
static int pn5180_busy_pin = 0; // Will be set by configurePins()

// Forward declarations
extern PN5180ISO15693* nfc_reader;
void initializeNfcReader();

// Function to configure pins based on board type (compile-time)
void configurePins(int cube_id) {
#ifdef BOARD_V6
  miso_pin = 34;
  pn5180_busy_pin = 35;
  Serial.printf("Cube %d: 38-pin board - MISO=%d, PN5180_BUSY=%d\n", cube_id, miso_pin, pn5180_busy_pin);
#else
  miso_pin = 39;
  pn5180_busy_pin = 36;
  Serial.printf("Cube %d: socket board - MISO=%d, PN5180_BUSY=%d\n", cube_id, miso_pin, pn5180_busy_pin);
#endif

  // Initialize NFC reader with correct pins
  initializeNfcReader();
  Serial.printf("Pin configuration complete for cube %d\n", cube_id);
}

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
#define PN5180_NSS 32
#define PN5180_RST 17


// Display Settings
#define BIG_ROW 0
#define BIG_COL 10
#define BIG_TEXT_SIZE 1
#define BRIGHTNESS 255
#define HIGHLIGHT_TIME_MS 2000
#define PRINT_DEBUG true

// Timing Constants
#define NFC_DEBOUNCE_TIME_MS 200
#define NFC_MIN_PUBLISH_INTERVAL_MS 100
#define ANIMATION_DURATION_MS 1000
#define ANIMATION_SCALE 100
#define DISPLAY_STARTUP_DELAY_MS 600
#define HALL_SENSOR_CHECK_INTERVAL_MS 50  /* Hall sensor polling interval (matches NFC read rate) */

// Hall Sensor Status Strings
#define HALL_SENSOR_STATUS_CONNECTED "connected"
#define HALL_SENSOR_STATUS_DISCONNECTED "disconnected"

// Sleep Configuration
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */
#define BATTERY_MAINTENANCE_INTERVAL_S  360000ULL  /* Wake up for battery maintenance */
#define SLEEP_PIN GPIO_NUM_0     /* Pin 0 for external wake-up (boot button) */
#ifdef BOARD_V6
#define POWER_SWITCH_PIN GPIO_NUM_5  /* GPIO5 controls TPS22975 HUB75 power switch */
// #define HALL_SENSOR_ENABLED    /* Uncomment to enable Hall effect sensor gating on this cube */
#ifdef HALL_SENSOR_ENABLED
#define HALL_SENSOR_PIN GPIO_NUM_36  /* GPIO36 reads A3144 Hall effect sensor (open-collector, active LOW) */
#endif
#endif

// Sleep state management
RTC_DATA_ATTR bool is_sleep_mode = false;
RTC_DATA_ATTR unsigned long sleep_start_time = 0;
RTC_DATA_ATTR bool pin0_state_at_sleep = HIGH;
RTC_DATA_ATTR uint32_t sleep_interval_s = 20;  // Default 20s, configurable via MQTT
RTC_DATA_ATTR uint16_t saved_brightness = BRIGHTNESS;  // Persist brightness across sleep

// Auto-sleep inactivity tracking
#define AUTO_SLEEP_TIMEOUT_MS  600000UL  // 10 minutes
RTC_DATA_ATTR unsigned long last_mqtt_message_time = 0;

// MQTT Configuration
#define MQTT_SERVER_PI "192.168.8.247"
#define MQTT_PORT 1883

// MQTT Topic Prefixes moved to cube_utilities.h/.cpp

// ============= Global Variables =============

// HUB75 Display Configuration
HUB75_I2S_CFG::i2s_pins display_pins = {
  0,  //R1_PIN,
  0,  //G1_PIN,
  0,  //B1_PIN,
  0,  //R2_PIN,
  0,  //G2_PIN,
  0,  //B2_PIN,
#ifdef BOARD_V6
  19,  //A_PIN (38-pin board: GPIO5 used for TPS22975 power switch)
#else
  19,  //A_PIN (socket board)
#endif
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

HUB75_I2S_CFG display_config(
  PANEL_RES_X,
  PANEL_RES_Y,
  PANEL_CHAIN,
  display_pins
);

// Hardware Objects
// MatrixPanel_I2S_DMA *led_display;
PN5180ISO15693* nfc_reader = nullptr;  // Will be initialized after cube ID is determined

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

// Track first boot vs wake from sleep
static bool is_first_boot = true;

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
String mqtt_topic_cube_right;  // publishes neighbor cube index to cube/right/<id>

// UDP Configuration
#define UDP_PORT 54321  // Port for ping-pong
#define DEBUG_UDP_PORT 54322  // Port for debug output
WiFiUDP udp;
char udpBuffer[255];
IPAddress debugIP = IPAddress(192, 168, 8, 196);  // Default debug destination

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

// Send debug message via UDP
void debugSend(const char* message) {
  // Only send if UDP server is already running
  if (udp.beginPacket(debugIP, DEBUG_UDP_PORT)) {
    udp.write((const uint8_t*)message, strlen(message));
    udp.endPacket();
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

// MQTT letter latency tracking (forward-declared for use in DisplayManager)
unsigned long last_letter_recv_time = 0;
unsigned long letter_interval_accum = 0;
int letter_interval_count = 0;
unsigned long max_letter_interval = 0;
unsigned long nfc_read_max_us = 0;

// ============= DisplayManager Class =============
class DisplayManager {
private:
  MatrixPanel_I2S_DMA* led_display;
  bool is_image_mode;
  uint16_t* image1;
  uint16_t* image2;
  uint16_t* image;
  uint16_t* previous_image;
  String display_string;
  bool is_border_word;
  uint8_t debug_line;
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
  DisplayManager(String cube_id) : is_image_mode(false), is_dirty(true),
                                is_border_word(false), debug_line(0),
                                animation_start_time(0), highlight_end_time(0), percent_complete(100),
                                current_letter_color(LETTER_COLOR), current_font(&Roboto_Mono_Bold_78),
                                text_size(1), font_size(1), is_lock(false),
                                vline_color_left(0), vline_color_right(0),
                                vline_height(PANEL_RES),
                                hline_color_top(0),
                                hline_color_bottom(0),
                                image1(nullptr), image2(nullptr), image(nullptr), previous_image(nullptr),
                                previous_letter(' '), current_letter(' ') {
    int cube_id_int = cube_id.toInt();    
    rotation = (cube_id_int <= 6) ? 2 : 0;
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
    
    // Safe bounds checking for RGB pin array access
    int cube_id_int = cube_id.toInt();    
    int8_t* rgb = cube_id_int <= 6 ? rgb_small : rgb_large;
    display_config.gpio.r1 = rgb[0];
    display_config.gpio.g1 = rgb[1];
    display_config.gpio.b1 = rgb[2];
    display_config.gpio.r2 = rgb[3];
    display_config.gpio.g2 = rgb[4];
    display_config.gpio.b2 = rgb[5];
    led_display = new MatrixPanel_I2S_DMA(display_config);
    led_display->begin();
    led_display->setBrightness(saved_brightness);  // Use saved brightness (persistent across sleep)
    led_display->setRotation(rotation);
    led_display->setTextWrap(true);
    led_display->clearScreen();
    led_display->setFont(current_font);
    led_display->setTextSize(text_size);
  }

  void clearScreen() {
    led_display->clearScreen();
  }

  void clearDebugDisplay() {
    led_display->clearScreen();
    debug_line = 0;
  }

  void displayDebugMessage(const char* message) {
    int y_pos = debug_line * 8 + 8;

    led_display->setCursor(1, y_pos);
    led_display->setTextSize(1);
    led_display->setFont(NULL);
    led_display->setTextColor(RED, BLACK);
    led_display->print(message);
    led_display->flipDMABuffer();
    led_display->setCursor(1, y_pos);
    led_display->setTextSize(1);
    led_display->setFont(NULL);
    led_display->setTextColor(RED, BLACK);
    led_display->print(message);

    debug_line++;
    if (strlen(message) > 10) {
      debug_line++;
    }
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

  void drawOrientationIndicator() {
    // Draw 2x2 red dots in bottom-left and bottom-right corners
    // Indicates which way is up for rotationally symmetric letters (N, S, O, X, H, Z)
    uint16_t red = 0xF800;
    led_display->fillRect(2, 60, 2, 2, red);
    led_display->fillRect(60, 60, 2, 2, red);
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

      // Draw orientation indicator only when letter animation is complete
      if (percent_complete >= 100) {
        drawOrientationIndicator();
      }
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

#ifdef BOARD_V6
  void shutdownForSleep() {
    led_display->stopDMAoutput();
    const int hub75_pins[] = {
      display_config.gpio.r1, display_config.gpio.g1, display_config.gpio.b1,
      display_config.gpio.r2, display_config.gpio.g2, display_config.gpio.b2,
      display_config.gpio.a,  display_config.gpio.b,  display_config.gpio.c,
      display_config.gpio.d,  display_config.gpio.e,
      display_config.gpio.lat, display_config.gpio.oe, display_config.gpio.clk
    };
    for (int pin : hub75_pins) {
      pinMode(pin, INPUT);
    }
  }
#endif


  void handleBrightnessCommand(const String& message) {
    debugPrintln("setting brightness due to /brightness");
    uint16_t brightness = message.toInt();
    saved_brightness = brightness;  // Save to RTC memory for persistence across sleep
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

  void handleConsolidatedBorderCommand(const String& message) {
    // Protocol: "NSW:0xFF0000" or "N:0x00FF00" or ":0xFF0000" for all sides
    // Unmentioned sides are automatically cleared
    
    debugPrint("Consolidated border: ");
    debugPrintln(message.c_str());
    
    // First clear all borders
    hline_color_top = 0;
    hline_color_bottom = 0; 
    vline_color_left = 0;
    vline_color_right = 0;
    
    // Parse the message: directions:color
    int colonIndex = message.indexOf(':');
    if (colonIndex == -1) return; // Invalid format
    
    String directions = message.substring(0, colonIndex);
    String colorStr = message.substring(colonIndex + 1);
    
    uint32_t color = 0;
    if (colorStr.length() > 0) {
      color = strtoul(colorStr.c_str(), NULL, 16);
    }
    
    // Apply color to specified directions
    for (int i = 0; i < directions.length(); i++) {
      char dir = directions.charAt(i);
      switch (dir) {
        case 'N': // North = top
          hline_color_top = color;
          break;
        case 'S': // South = bottom  
          hline_color_bottom = color;
          break;
        case 'E': // East = right
          vline_color_right = color;
          break;
        case 'W': // West = left
          vline_color_left = color;
          break;
      }
    }
    
    // Force display update
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

    // Track letter interval statistics
    if (last_message_time > 0 && time_since_last < 5000) {
      letter_interval_accum += time_since_last;
      letter_interval_count++;
      if (time_since_last > max_letter_interval) {
        max_letter_interval = time_since_last;
      }
    }
    last_letter_recv_time = current_time;

    last_message_time = current_time;

    Serial.printf("[%lu] MQTT letter '%s' delta=%lu ms\n", current_time, message.c_str(), time_since_last);
    
    if (previous_letter != current_letter) {
      previous_letter = current_letter;
    }
      
    is_image_mode = false;
    if (message.length() > 0) {
      current_letter = message.charAt(0);
      animation_start_time = millis();
      current_font = &Roboto_Mono_Bold_78;  // Restore custom font for letter mode
      text_size = 1;  // Always use size 1 for letter mode
      is_dirty = true;
    }
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


// Loop timing variables
unsigned long loop_start_time = 0;

// Average loop timing over multiple iterations
#define TIMING_SAMPLE_SIZE 100
unsigned long timing_samples[TIMING_SAMPLE_SIZE];
int timing_sample_index = 0;
bool timing_samples_filled = false;
unsigned long timing_accumulator = 0;

// Per-section timing diagnostics
struct SectionTiming {
  unsigned long mqtt_us;
  unsigned long display_us;
  unsigned long udp_us;
  unsigned long nfc_us;
  unsigned long total_us;
};
SectionTiming section_timing_accum = {0, 0, 0, 0, 0};
int section_timing_count = 0;

// Per-section timing diagnostics (forward declarations removed, definitions below)

// ============= Utility Functions =============
// Utility functions moved to cube_utilities.h/.cpp

// ============= Hardware Setup Functions =============
void initializeNfcReader() {
  // Validate pins are configured
  if (pn5180_busy_pin == 0) {
    Serial.println("ERROR: Pins not configured! Call configurePins() first.");
    return;
  }
  
  // Clean up previous instance if any
  if (nfc_reader != nullptr) {
    delete nfc_reader;
  }
  
  // Create new NFC reader with current pin configuration
  nfc_reader = new PN5180ISO15693(PN5180_NSS, pn5180_busy_pin, PN5180_RST);
  Serial.printf("NFC reader initialized with pn5180_busy_pin=%d\n", pn5180_busy_pin);
}

void setupNfcReader() {
  if (nfc_reader == nullptr) {
    Serial.println(F("Error: NFC reader not initialized! Call configurePins first."));
    return;
  }
  
  // Validate pins are configured
  if (miso_pin == 0) {
    Serial.println(F("ERROR: MISO pin not configured! Call configurePins first."));
    return;
  }
  
  SPI.begin(SCK, miso_pin, MOSI, SS);
  Serial.printf("SPI initialized with miso_pin=%d\n", miso_pin);
  Serial.println(F("Initializing nfc..."));
  nfc_reader->begin();
  nfc_reader->reset();
  Serial.println(F("Enabling RF field..."));
  nfc_reader->setupRF();
}

// ============= Network Functions =============
uint8_t getCubeIpOctet() {
  String mac_address = WiFi.macAddress();
  uint8_t mac_position = findMacAddressPosition(mac_address.c_str());
  if (mac_position == -1) {
    mac_position = 20;
  }
  uint8_t cube_id = ((mac_position <= 5) ? 1 : 5) + mac_position;
  cube_identifier = cube_id;
  
  // Configure pins based on cube ID
  configurePins(cube_id);
  
  Serial.print("mac_address: ");
  Serial.println(mac_address);
  Serial.print("mac_position: ");
  Serial.println(mac_position);
  Serial.print("cube_id: ");
  Serial.println(cube_identifier);
  return cube_id + 20;
}

void setupWiFiConnection() {
  bool try_portable = true;
  Serial.print("mac address: ");
  Serial.println(WiFi.macAddress());
  uint8_t ip_octet = getCubeIpOctet();
  Serial.print("ip octet: ");
  Serial.println(ip_octet);
  IPAddress local_IP(192, 168, 8, ip_octet);
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
    WiFi.setSleep(WIFI_PS_NONE);
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
  if (nfc_reader != nullptr) {
    nfc_reader->reset();
    nfc_reader->setupRF();
  }
}

void publishAutoSleepFlag() {
  if (mqtt_topic_cube.isEmpty()) return;
  String flag_topic = mqtt_topic_cube + "/auto_sleep";
  mqtt_client.publish(flag_topic.c_str(), "1", true);
  delay(100);  // Give MQTT time to flush before sleep
}

void enterSleepMode() {
  debugPrintln("Entering deep sleep mode...");
  display_manager->displayDebugMessage("sleep...");
  delay(2000);

#ifdef BOARD_V6
  // Stop DMA and tri-state HUB75 pins to prevent backfeed through panel clamping diodes.
  // Hold all GPIO states through deep sleep so tri-stated pins don't float on power-down.
  display_manager->shutdownForSleep();
  digitalWrite(POWER_SWITCH_PIN, LOW);
  gpio_hold_en(POWER_SWITCH_PIN);
  gpio_deep_sleep_hold_en();
#endif

  // Read current pin state and store it in RTC memory
  pin0_state_at_sleep = digitalRead(SLEEP_PIN);
  int wake_level = pin0_state_at_sleep ? 0 : 1; // Wake on opposite level

  Serial.print("Going to sleep with Pin 0 at ");
  Serial.print(pin0_state_at_sleep ? "HIGH" : "LOW");
  Serial.print(", will wake on ");
  Serial.println(wake_level ? "HIGH" : "LOW");
  
  // Configure external wake-up on Pin 0 for opposite level
  esp_sleep_enable_ext0_wakeup(SLEEP_PIN, wake_level);
  
  // Configure pull-up to ensure stable high state during sleep
  rtc_gpio_pulldown_dis(SLEEP_PIN);
  rtc_gpio_pullup_en(SLEEP_PIN);

  // Enable timer wake-up using configurable interval
  esp_sleep_enable_timer_wakeup((uint64_t)sleep_interval_s * uS_TO_S_FACTOR);

  is_sleep_mode = true;
  sleep_start_time = millis();

  // Send debug via UDP
  char dbg[64];
  snprintf(dbg, sizeof(dbg), "sleeping for %lu seconds", sleep_interval_s);
  debugSend(dbg);
  Serial.printf("Will wake on Pin 0 release or every %lu seconds...\n", sleep_interval_s);
  Serial.flush();
  
  esp_deep_sleep_start();
}

void handleWakeUp() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // Timer wake - check if we should stay asleep or wake fully
    debugSend("timer wake");

    // Connect to MQTT to check for retained sleep message
    WiFiClient keepalive_tcp;
    PubSubClient keepalive_mqtt(keepalive_tcp);
    keepalive_mqtt.setServer(MQTT_SERVER_PI, MQTT_PORT);

    volatile bool stay_asleep = false;  // Default: wake up unless we see "1"
    String client_id = "cube-" + String(cube_identifier) + "-ka";
    String auto_sleep_topic = "cube/" + String(cube_identifier) + "/auto_sleep";

    // Callback to receive retained sleep message.
    // IMPORTANT: Read payload BEFORE any publish() calls — PubSubClient reuses its
    // internal buffer for both incoming and outgoing messages, so publishing inside
    // the callback overwrites the payload bytes.
    keepalive_mqtt.setCallback([&stay_asleep](char* topic, byte* payload, unsigned int length) {
      stay_asleep = (length == 1 && payload[0] == '1');
    });

    if (keepalive_mqtt.connect(client_id.c_str())) {
      debugSend("mqtt ok");

      // Publish keep-alive status
      String status_topic = "cube/" + String(cube_identifier) + "/status";
      keepalive_mqtt.publish(status_topic.c_str(), "keep-alive");

      // Subscribe to per-cube auto_sleep flag
      keepalive_mqtt.subscribe(auto_sleep_topic.c_str());

      // Wait for retained message to arrive
      unsigned long check_start = millis();
      while (millis() - check_start < 1000) {
        keepalive_mqtt.loop();
        delay(10);
      }

      char dbg[64];
      snprintf(dbg, sizeof(dbg), "stay_asleep=%d", stay_asleep);
      debugSend(dbg);

      if (stay_asleep) {
        // Go back to sleep
        keepalive_mqtt.disconnect();
        debugSend("sleep again");
        enterSleepMode();
      } else {
        // Clear the auto_sleep retained flag so cube stays awake after next reboot
        keepalive_mqtt.publish(auto_sleep_topic.c_str(), "", true);
        delay(100);
        keepalive_mqtt.disconnect();

        debugSend("WAKE FULL - staying awake");
        last_mqtt_message_time = millis();  // Reset auto-sleep timer on wake
        is_sleep_mode = false;
        Serial.println("Waking fully - continuing setup");
      }
    } else {
      debugSend("mqtt fail");
      // If MQTT failed, assume we should wake (safer default)
      last_mqtt_message_time = millis();  // Reset auto-sleep timer
      is_sleep_mode = false;
    }
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    debugPrintln("Woken by external signal (Pin 0 released)");
    // Wake reason "wake:2" already indicates button wake, no need for "WAKE" message
    last_mqtt_message_time = millis();  // Reset auto-sleep timer
    is_sleep_mode = false;
    Serial.println("Pin 0 wake-up detected - staying awake");
  }
  else {
    is_sleep_mode = false;
    debugPrintln("Normal boot - staying awake");
  }
}

void handleSleepNowCommand(const String& /*message*/) {
  debugSend("sleep_now cmd received");
  publishAutoSleepFlag();
  enterSleepMode();
}

#ifdef BOARD_V6
void handlePowerTestCommand(const String& message) {
  if (message == "0") {
    display_manager->shutdownForSleep();
    digitalWrite(POWER_SWITCH_PIN, LOW);
    debugSend("DMA stopped, pins tri-stated, GPIO5 LOW");
  } else {
    digitalWrite(POWER_SWITCH_PIN, HIGH);
    debugSend("GPIO5 HIGH - reboot to restore display");
  }
}
#endif

void handleSleepIntervalCommand(const String& message) {
  uint32_t new_interval = message.toInt();
  if (new_interval >= 10 && new_interval <= 300) {
    sleep_interval_s = new_interval;
    char dbg[64];
    snprintf(dbg, sizeof(dbg), "sleep_interval=%lu", sleep_interval_s);
    debugSend(dbg);
    Serial.printf("Sleep interval set to %lu seconds\n", sleep_interval_s);
  } else {
    debugSend("invalid sleep_interval");
    Serial.println("Invalid sleep interval: must be 10-300 seconds");
  }
}


void onConnectionEstablished() {
  debugSend("MQTT connected");

  // Pre-allocate common topics
  mqtt_topic_cube = MQTT_TOPIC_PREFIX_CUBE + cube_identifier;
  mqtt_topic_cube_nfc = String(MQTT_TOPIC_PREFIX_CUBE) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_game_nfc = String(MQTT_TOPIC_PREFIX_GAME) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_echo = createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_ECHO);
  mqtt_topic_cube_right = String(MQTT_TOPIC_PREFIX_CUBE) + String("right/") + cube_identifier;

  // Only publish version on first boot, not on wake from sleep
  if (is_first_boot) {
    mqtt_client.publish(createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_VERSION), GIT_VERSION, true);  // retained
  }

  // Subscribe to all command topics
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "brightness", [](const String& msg) { display_manager->handleBrightnessCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "reboot", handleRebootCommand );
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "border_bottom_banner", [](const String& msg) { display_manager->handleBorderBottomBannerCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "border_top_banner", [](const String& msg) { display_manager->handleBorderTopBannerCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "sleep_now", handleSleepNowCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/sleep_interval", handleSleepIntervalCommand);
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "string", [](const String& msg) { display_manager->handleStringCommand(msg); });

  // Game-activity topics reset the inactivity timer. Infrastructure topics (sleep, reboot,
  // brightness, reset) intentionally do NOT reset it — we don't want a reboot command to
  // keep the cube awake, and we don't want retained messages on reconnect to reset the timer.
  auto resetActivityTimer = []() { last_mqtt_message_time = millis(); };

  mqtt_client.subscribe(mqtt_topic_cube + "/border", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleConsolidatedBorderCommand(msg); });

  // Legacy border topics for backward compatibility
  mqtt_client.subscribe(mqtt_topic_cube + "/border_hline_bottom", [](const String& msg) { display_manager->handleBorderBottomBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_hline_top", [](const String& msg) { display_manager->handleBorderTopBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_frame", [](const String& msg) { display_manager->handleBorderFrameCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_height", [](const String& msg) { display_manager->handleBorderLineHeightCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_left", [](const String& msg) { display_manager->handleBorderVLineLeftCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_right", [](const String& msg) { display_manager->handleBorderVLineRightCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/font_size", [](const String& msg) { display_manager->handleFontSizeCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/flash", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleFlashCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/imagex", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleImageBinaryCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/letter", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleLetterCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/lock", [](const String& msg) { display_manager->handleLockCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/ping", [resetActivityTimer](const String& msg) { resetActivityTimer(); handlePingCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/reboot", handleRebootCommand);
#ifdef BOARD_V6
  mqtt_client.subscribe(mqtt_topic_cube + "/power_test", handlePowerTestCommand);
#endif
  mqtt_client.subscribe(mqtt_topic_cube + "/reset", handleResetCommand);
  mqtt_client.subscribe(mqtt_topic_game_nfc, [resetActivityTimer](const String& msg) { resetActivityTimer(); handleNfcCommand(msg); });

  // Start inactivity timer from first MQTT connection
  if (last_mqtt_message_time == 0) {
    last_mqtt_message_time = millis();
  }
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
  // Clear the card_id buffer first
  memset(card_id, 0, NFCID_LENGTH);
  
  // Check if NFC reader is initialized
  if (nfc_reader == nullptr) {
    Serial.println("Error: NFC reader not initialized");
    return EC_NO_CARD;
  }
  
  // Try to read the card with error handling
  ISO15693ErrorCode result = nfc_reader->getInventory(card_id);
  
  // Log detailed error information for debugging
  if (result != ISO15693_EC_OK && result != EC_NO_CARD) {
    Serial.printf("NFC read error: %d\n", result);
  }
  
  return result;
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
      // Check if message is "timing" - return cube_id:avg_loop_time_us
      else if (strcmp(udpBuffer, "timing") == 0) {
        // Calculate average loop time over recent samples
        unsigned long avg_loop_time_us = 0;

        if (timing_samples_filled) {
          // Use all samples for average
          avg_loop_time_us = timing_accumulator / TIMING_SAMPLE_SIZE;
        } else if (timing_sample_index > 0) {
          // Use available samples
          unsigned long sum = 0;
          for (int i = 0; i < timing_sample_index; i++) {
            sum += timing_samples[i];
          }
          avg_loop_time_us = sum / timing_sample_index;
        } else {
          // No samples yet, return current loop time
          unsigned long loop_end_time = micros();
          avg_loop_time_us = loop_end_time - loop_start_time;
        }

        char timingStr[32];
        snprintf(timingStr, sizeof(timingStr), "%s:%lu", cube_identifier.c_str(), avg_loop_time_us);

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)timingStr, strlen(timingStr));
        udp.endPacket();

        Serial.printf("Sent timing to %s:%d: %s (avg over %d samples)\n",
                      udp.remoteIP().toString().c_str(), udp.remotePort(), timingStr,
                      timing_samples_filled ? TIMING_SAMPLE_SIZE : timing_sample_index);
      }
      // Check if message is "diag" - return detailed per-section timing breakdown
      else if (strcmp(udpBuffer, "diag") == 0) {
        char diagStr[256];
        unsigned long avg_mqtt = section_timing_count > 0 ? section_timing_accum.mqtt_us / section_timing_count : 0;
        unsigned long avg_display = section_timing_count > 0 ? section_timing_accum.display_us / section_timing_count : 0;
        unsigned long avg_udp = section_timing_count > 0 ? section_timing_accum.udp_us / section_timing_count : 0;
        unsigned long avg_nfc = section_timing_count > 0 ? section_timing_accum.nfc_us / section_timing_count : 0;
        unsigned long avg_total = timing_samples_filled ? timing_accumulator / TIMING_SAMPLE_SIZE :
                                  (timing_sample_index > 0 ? timing_accumulator / timing_sample_index : 0);
        unsigned long avg_letter_interval = letter_interval_count > 0 ? letter_interval_accum / letter_interval_count : 0;

        const char* fw_board =
#ifdef BOARD_V6
          "v6";
#else
          "v1";
#endif
        snprintf(diagStr, sizeof(diagStr),
          "%s|fw=%s|mac=%s|loop=%lu|mqtt=%lu|disp=%lu|udp=%lu|nfc=%lu|nfc_max=%lu|letter_avg=%lu|letter_max=%lu|letter_n=%d|rssi=%d|samples=%d",
          cube_identifier.c_str(), fw_board, WiFi.macAddress().c_str(), avg_total, avg_mqtt, avg_display, avg_udp, avg_nfc,
          nfc_read_max_us, avg_letter_interval, max_letter_interval, letter_interval_count,
          WiFi.RSSI(), section_timing_count);

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)diagStr, strlen(diagStr));
        udp.endPacket();

        // Reset accumulators after reading
        section_timing_accum = {0, 0, 0, 0, 0};
        section_timing_count = 0;
        letter_interval_accum = 0;
        letter_interval_count = 0;
        max_letter_interval = 0;
        nfc_read_max_us = 0;
      }
      // Check if message is "temp" - return cube_id:temperature_celsius
      else if (strcmp(udpBuffer, "temp") == 0) {
        // Read internal temperature sensor
        float temperature_c = temperatureRead();

        char tempStr[32];
        snprintf(tempStr, sizeof(tempStr), "%s:%.1f", cube_identifier.c_str(), temperature_c);

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)tempStr, strlen(tempStr));
        udp.endPacket();

        Serial.printf("Sent temperature to %s:%d: %s\n",
                      udp.remoteIP().toString().c_str(), udp.remotePort(), tempStr);
      }
      // Check if message is "testdebug" - send test UDP debug packet
      else if (strcmp(udpBuffer, "testdebug") == 0) {
        const char* testMsg = "debug test: hello from cube";
        debugSend(testMsg);
        Serial.printf("Sent debug test\n");
      }
      // Check if message is "setdebugip" - set debug destination IP
      else if (strncmp(udpBuffer, "setdebugip ", 11) == 0) {
        debugIP = udp.remoteIP();  // Use requester's IP
        char reply[64];
        snprintf(reply, sizeof(reply), "debug IP set to %s", debugIP.toString().c_str());
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)reply, strlen(reply));
        udp.endPacket();
        Serial.printf("Debug IP set to %s\n", debugIP.toString().c_str());
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

  // Handle wake up from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Track if this is first boot or wake from sleep
  is_first_boot = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);

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
  
  // Configure Pin 0 for momentary switch (with internal pull-up)
  pinMode(0, INPUT_PULLUP);

#ifdef BOARD_V6
  // Release holds set in enterSleepMode(), re-enable TPS22975
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(POWER_SWITCH_PIN);
  pinMode(POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(POWER_SWITCH_PIN, HIGH);

#ifdef HALL_SENSOR_ENABLED
  // Initialize Hall effect sensor (GPIO36, input-only, open-collector active LOW)
  pinMode(HALL_SENSOR_PIN, INPUT);
#endif
#endif
  
  // Initialize WiFi and get cube identifier
  debugPrintln("setting up wifi...");
  setupWiFiConnection();
  debugPrintln("wifi done");

  String cube_id = cube_identifier;
  // Create display manager
  display_manager = new DisplayManager(cube_id);

  display_manager->clearDebugDisplay();
  display_manager->displayDebugMessage(GIT_TIMESTAMP);
  delay(DISPLAY_STARTUP_DELAY_MS);

  display_manager->displayDebugMessage((String("wake:") + String(wakeup_reason)).c_str());

  handleWakeUp();
  
  // If we're in battery maintenance mode, skip the rest of setup
  if (is_sleep_mode && wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    debugSend("setup: returning to sleep");
    return;  // Will go back to sleep in handleWakeUp()
  }

  debugSend("setup: continuing normally");
  Serial.println(cube_id);
  static String client_name = cube_id;
  Serial.println(client_name);
  mqtt_client.setMqttClientName(client_name.c_str());
  char ipDisplay[64];
  snprintf(ipDisplay, sizeof(ipDisplay), "%s",
    WiFi.localIP().toString().c_str());
  display_manager->displayDebugMessage(ipDisplay);

  display_manager->displayDebugMessage("nfc...");
  debugPrintln(WiFi.macAddress().c_str());

  debugPrintln("setting up nfc reader...");
  setupNfcReader();
  debugPrintln("nfc reader done");

  debugPrintln("setting up udp...");
  setupUDP(); // Add UDP setup

  debugPrintln(F("Setup Complete"));
}

void loop() {
  loop_start_time = micros();

  unsigned long section_start = micros();
  mqtt_client.loop();
  unsigned long mqtt_end = micros();
  unsigned long mqtt_us = mqtt_end - section_start;

  if (last_mqtt_message_time > 0 &&
      (millis() - last_mqtt_message_time > AUTO_SLEEP_TIMEOUT_MS)) {
    debugSend("auto-sleep: inactivity timeout");
    publishAutoSleepFlag();
    enterSleepMode();
  }

  esp_task_wdt_reset();  // Feed the watchdog timer

  // Throttle display updates to 30 FPS for improved MQTT responsiveness
  static unsigned long last_display_update = 0;
  unsigned long current_time = millis();
  unsigned long display_us = 0;
  if (current_time - last_display_update >= 33) { // ~30 FPS
    unsigned long display_start = micros();
    display_manager->animate(current_time);
    display_manager->updateDisplay(current_time);
    last_display_update = current_time;
    display_us = micros() - display_start;
  }

  unsigned long udp_start = micros();
  handleUDP();
  unsigned long udp_us = micros() - udp_start;

  // Throttle NFC reads to ~20 Hz to prevent slow NFC hardware from blocking
  // the main loop and starving MQTT processing / display animation
  static unsigned long last_nfc_read = 0;
  unsigned long nfc_us = 0;
  if (current_time - last_nfc_read >= 50) {
    last_nfc_read = current_time;
    unsigned long nfc_start = micros();

    uint8_t card_id[NFCID_LENGTH];
    ISO15693ErrorCode read_result = readNfcCard(card_id);
    nfc_us = micros() - nfc_start;

    // A stuck BUSY pin causes ~1000ms NFC reads that return EC_NO_CARD (not an
    // error code), so the recovery below is triggered by timing, not result code.
    // Normal reads are ~200µs; 100ms is an unambiguous signal of stuck BUSY.
    static unsigned long last_nfc_reset = 0;
    static int consecutive_slow_reads = 0;
    if (nfc_us > 100000UL) {
      consecutive_slow_reads++;
      if (consecutive_slow_reads > 3 && (millis() - last_nfc_reset > 5000)) {
        Serial.printf("Resetting NFC reader: stuck BUSY detected (%lu us)\n", nfc_us);
        if (nfc_reader != nullptr) {
          nfc_reader->reset();
          nfc_reader->setupRF();
        }
        last_nfc_reset = millis();
        consecutive_slow_reads = 0;
      }
    } else {
      consecutive_slow_reads = 0;
    }

    if (read_result == ISO15693_EC_OK) {
      char neighbor_id[NFCID_LENGTH * 2 + 1];
      convertNfcIdToHexString(card_id, sizeof(card_id) / sizeof(card_id[0]), neighbor_id);
      int cube_num = lookupCubeNumberByTag(neighbor_id);
      if (strcmp(neighbor_id, last_neighbor_id) != 0) {
        debugPrintln(F("New card"));
        unsigned long publish_start = millis();
        bool success = mqtt_client.publish(mqtt_topic_cube_nfc, neighbor_id, true);
        if (cube_num > 0) {
          char buf[8];
          snprintf(buf, sizeof(buf), "%d", cube_num);
          mqtt_client.publish(mqtt_topic_cube_right, buf, true);
        }
        unsigned long publish_end = millis();
        Serial.printf("[%lu] MQTT publish took %lu ms - payload: %s - success: %d\n", publish_end, publish_end - publish_start, neighbor_id, success);
        if (success) {
          strncpy(last_neighbor_id, neighbor_id, sizeof(last_neighbor_id) - 1);
          last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
        }
      }
    } else if (read_result == EC_NO_CARD) {
      // Publish "-" to indicate no neighbor.
      if (strcmp(last_neighbor_id, "-") != 0) {
        debugPrintln(F("No card detected"));
        unsigned long publish_start = millis();
        bool success = mqtt_client.publish(mqtt_topic_cube_nfc, "-", true);
        // Also publish to numeric neighbor topic with "-" to denote no neighbor
        mqtt_client.publish(mqtt_topic_cube_right, "-", true);
        unsigned long publish_end = millis();
        Serial.printf("[%lu] MQTT publish took %lu ms - dash payload, success: %d\n", publish_end, publish_end - publish_start, success);
        if (success) {
          strncpy(last_neighbor_id, "-", sizeof(last_neighbor_id) - 1);
          last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
        }
      }
    } else {
      Serial.printf("NFC read failed with error code: %d\n", read_result);
    }

    if (nfc_us > nfc_read_max_us) {
      nfc_read_max_us = nfc_us;
    }
  }

#ifdef HALL_SENSOR_ENABLED
  // Track Hall sensor state and log connect/disconnect via MQTT
  static unsigned long last_hall_check = 0;
  static bool last_hall_present = false;
  if (current_time - last_hall_check >= HALL_SENSOR_CHECK_INTERVAL_MS) {
    last_hall_check = current_time;
    bool hall_present = (digitalRead(HALL_SENSOR_PIN) == LOW);

    if (hall_present != last_hall_present) {
      last_hall_present = hall_present;
      const char* status = hall_present ? HALL_SENSOR_STATUS_CONNECTED : HALL_SENSOR_STATUS_DISCONNECTED;
      mqtt_client.publish(mqtt_topic_cube + "/hall_sensor", status, true);
      Serial.printf("Hall sensor %s\n", status);
    }
  }
#endif

  // Accumulate per-section timing
  section_timing_accum.mqtt_us += mqtt_us;
  section_timing_accum.display_us += display_us;
  section_timing_accum.udp_us += udp_us;
  section_timing_accum.nfc_us += nfc_us;
  section_timing_count++;

  // Collect timing sample at end of loop
  unsigned long loop_end_time = micros();
  unsigned long current_loop_time = loop_end_time - loop_start_time;

  // Update rolling average using circular buffer
  if (timing_samples_filled) {
    // Remove old sample from accumulator
    timing_accumulator -= timing_samples[timing_sample_index];
  }

  // Add new sample
  timing_samples[timing_sample_index] = current_loop_time;
  timing_accumulator += current_loop_time;

  // Advance index
  timing_sample_index = (timing_sample_index + 1) % TIMING_SAMPLE_SIZE;
  if (timing_sample_index == 0 && !timing_samples_filled) {
    timing_samples_filled = true;
  }
}
