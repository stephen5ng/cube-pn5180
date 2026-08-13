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
#include "hall_presence.h"
#include "sensor_mode.h"
#include "cube_slot_store.h"
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
void publishPresence(const char* state);

// Which neighbour sensor this cube carries. Both paths are compiled in;
// detectSensorMode() sets this at boot and it selects between them.
static SensorMode sensor_mode = SENSOR_MODE_NFC;

static bool sensorModeIsMagnets() { return sensor_mode == SENSOR_MODE_MAGNETS; }

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

  Serial.printf("Pin configuration complete for cube %d\n", cube_id);
}

// Called once the sensor mode is known. configurePins() must have run first:
// initializeNfcReader() reads pn5180_busy_pin and setupNfcReader() reads
// miso_pin, both of which it assigns.
void initialiseNeighbourSensor() {
  if (!sensorModeIsMagnets()) {
    initializeNfcReader();
  }
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
#define SLEEP_PIN GPIO_NUM_0     /* Pin 0 for external wake-up (boot button) */
// Timer-wake check-in window: how long the keep-alive holds the WiFi radio up
// waiting for the retained auto_sleep flag. This doubles as the current-pulse
// dwell that keeps a USB-C power bank from auto-shutting-off on low draw, so
// lengthen it (not add a display flash) if a bank still cuts off. Stopgap for
// the USB-C generation; irrelevant once the 18650 power board lands.
#define KEEPALIVE_CHECKIN_WINDOW_MS  1000UL
// How long to keep waiting for the round-trip marker that confirms the retained
// sleep flag has been delivered. Only reached when the broker or the link is
// slow; the common case ends at KEEPALIVE_CHECKIN_WINDOW_MS above.
#define KEEPALIVE_FLAG_READ_TIMEOUT_MS  3000UL
#define POWER_RAIL_SETTLE_MS  50  /* Let the HUB75 5V rail come up before I2S DMA drives the panel */
#ifdef BOARD_V6
#define POWER_SWITCH_PIN GPIO_NUM_5  /* GPIO5 controls TPS22975 HUB75 power switch */
#endif

// Hall sensor modes (mutually exclusive)
#if defined(HALL_SENSOR_ENABLED)
#define HALL_SENSOR_PIN GPIO_NUM_36
#define HAS_HALL_SENSOR true
#define HAS_HALL_ANALOG false
#elif defined(HALL_SENSOR_ANALOG)
#define HALL_SENSOR_PIN GPIO_NUM_36
#define HAS_HALL_SENSOR false
#define HAS_HALL_ANALOG true
#else
#define HAS_HALL_SENSOR false
#define HAS_HALL_ANALOG false
#endif

// 2-of-6 Hall-sensor neighbor ID decode, an alternative to the PN5180 NFC
// neighbor path. See cubes/docs/hall_sensor_replacement_design.md.
// Six ID sensor GPIOs, reusing the PN5180 connector pins per the design's pin
// table. Order is P1..P6, mapping to id_mask bits 0..5.
static const uint8_t HALL_ID_PINS[6] = {32, 17, 23, 18, 34, 35};
#define HALL_PRESENCE_PIN 36        // existing v6 hall tap (GPIO36, input-only)
// DRV5055 analog presence sensor. Thresholds are deltas from a tracked baseline, not
// absolute ADC values; see hall_presence.h.
#define HALL_PRESENCE_DIRECTION        1    // +1: presence magnet drives the reading up
#define HALL_PRESENCE_ON_DELTA         95   // ~50% of the 194-count deflection measured on slot 1
#define HALL_PRESENCE_OFF_DELTA        48   // ~25%, hysteresis
#define HALL_PRESENCE_FAST_SHIFT       3    // ~8 samples at the 1kHz poll
#define HALL_PRESENCE_BASE_SHIFT       7
#define HALL_PRESENCE_BASE_INTERVAL_MS 250  // baseline tau ~32s

// Presence telemetry. The ID sensors are digital, so cube/N/hall_debug shows
// whether a magnet tripped them but nothing shows how much margin the analog
// presence reading has over HALL_PRESENCE_ON_DELTA. Rate-limited and
// change-gated: retained, so the last value is always readable, and quiet while
// a docked cube sits still.
//
// The change threshold has to clear the ADC noise or "sits still" never
// happens. delta idles across a band of roughly 36 counts -- measured on slot 1
// both undocked (-13..+35) and docked (135..171) -- so a smaller gate is always
// open and the topic streams at the rate cap forever. An assert or release is
// published regardless of size, so the events still land immediately.
#define HALL_PRESENCE_PUBLISH_INTERVAL_MS 500
#define HALL_PRESENCE_PUBLISH_MIN_CHANGE  40

// cube/N/proximity is the 0..100 closeness of the neighbour, for driving an
// animation or any other control. Smoothed harder than the detection path,
// which is deliberately quick so a docking cube latches promptly: at the ~1kHz
// poll a shift of 7 is roughly 128ms, slow enough that the +/-13 counts of ADC
// noise measured on slot 1 do not show as jitter.
//
// 10Hz is a compromise against MQTT volume, which is this firmware's documented
// bottleneck -- six cubes streaming during a game is real traffic. Publishing
// only on a change keeps a still cube silent, so the cost is only paid while
// something is actually moving.
// A docked neighbour clamps to 100 and so goes silent on its own, but one
// parked mid-scale -- a loose magnet, a cube not fully seated -- sits on the
// residual wander of the reading and would otherwise publish at the cap
// indefinitely. Measured on slot 1 in that state: 278 messages in 35s across a
// span of 9. A deadband costs an animation nothing at this scale.
#define HALL_PROXIMITY_SHIFT           7
#define HALL_PROXIMITY_INTERVAL_MS     100
#define HALL_PROXIMITY_MIN_CHANGE      3
// GH1230KSW ID sensors are open-drain with 10k pull-ups on the PCB: lines
// idle HIGH and a magnet pulls them LOW (bench-verified 2026-07-07 via
// hall_debug: idle mask reads 111111 with HIGH as the reference level).
#define HALL_ID_ACTIVE_LEVEL LOW
#define HALL_POLL_INTERVAL_MS 1     // ~1 kHz polling; each digitalRead is ~us
#define HALL_DEBOUNCE_READS 8       // consecutive identical reads to confirm (~8 ms)

// Sleep state management
RTC_DATA_ATTR unsigned long sleep_start_time = 0;
RTC_DATA_ATTR bool pin0_state_at_sleep = HIGH;
// How long a sleeping cube stays down between keep-alive check-ins. Survives
// deep sleep in RTC memory, and cube/{id}/sleep_interval overrides it.
RTC_DATA_ATTR uint32_t sleep_interval_s = 20;
RTC_DATA_ATTR uint16_t saved_brightness = BRIGHTNESS;  // Persist brightness across sleep

// Auto-sleep inactivity tracking
#define AUTO_SLEEP_TIMEOUT_MS  600000UL  // 10 minutes
RTC_DATA_ATTR unsigned long last_activity_time = 0;

// MQTT Configuration
#define MQTT_SERVER_PI "192.168.8.247"
#define MQTT_PORT 1883
#define WIFI_CONNECT_ATTEMPT_TIMEOUT_MS 10000
// Timer-wake check-in only. Must stay above real association time: below it,
// a cube that can reach the AP but associates slowly re-sleeps every cycle and
// wake.sh can never reach it. 3x the ~1s a static-IP association is expected
// to take; the "wifi assoc" debug line below is how that gets confirmed.
#define KEEPALIVE_WIFI_TIMEOUT_MS 3000UL
#define WIFI_RETRY_INTERVAL_MS 5000
#define MQTT_RECONNECT_DELAY_MS 5000
#define MQTT_SOCKET_TIMEOUT_S 2
#define MQTT_CONNECTION_TIMEOUT_MS 1000

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

int8_t rgb_pins[] = {25, 26, 33, 13, 27, 14};
int8_t bgr_pins[] = {33, 26, 25, 14, 27, 13};

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

struct NfcWorkerResult {
  ISO15693ErrorCode read_result;
  uint8_t card_id[NFCID_LENGTH];
  uint32_t read_us;
  uint32_t recovery_us;
  bool recovery_attempted;
  bool recovery_succeeded;
};

QueueHandle_t nfc_result_queue = nullptr;
TaskHandle_t nfc_worker_handle = nullptr;

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
static int compiled_cube_id = -1;
static int applied_slot = -1;
static uint32_t applied_generation = 0;
static bool authority_latched = false;
static bool slot_resolved = false;
static unsigned long assignment_wait_started = 0;
static String mac_nocolons;
static String boot_id;
static String mqtt_topic_assign;
static String mqtt_topic_device_nfc;
static char last_observation_published[NFCID_LENGTH * 2 + 1] = "";
static String mqtt_topic_presence;
static String mqtt_topic_liveness_response;
static const unsigned long ASSIGNMENT_WAIT_MS = 3000;
static RgbOrder current_rgb_order = RGB_ORDER_BGR;
const char* nfc_topic_out;
static bool wifi_connection_attempt_active = false;
static unsigned long wifi_connection_attempt_started = 0;
static unsigned long next_wifi_connection_attempt = 0;

// Animation
char last_neighbor_id[NFCID_LENGTH * 2 + 1] = "INIT";  // last raw NFC value published to /nfc
char last_right_published[8] = "INIT";                  // last value published to /right
unsigned long last_nfc_publish_time = 0;

// Pre-allocated MQTT topics
String mqtt_topic_cube;
String mqtt_topic_cube_nfc;
String mqtt_topic_game_nfc;
String mqtt_topic_echo;
String mqtt_topic_cube_right;  // publishes neighbor cube index to cube/right/<id>
String mqtt_topic_cube_proximity;  // publishes 0-100 closeness to cube/<id>/proximity
int published_proximity = -1;      // -1 forces the next poll to publish
// Topics whose retained delete has not been accepted yet. The topic name is the
// only handle on the stale value, so it is held rather than dropped.
//
// Several can be outstanding at once from a single rebinding: binding a slot
// queues a delete for the topic being left, and resolving as a reader queues
// another for the one just bound. One pending slot would let the second
// overwrite the first while the broker is refusing writes, which is the case
// that strands a record indefinitely.
static constexpr size_t PROXIMITY_PENDING_CLEARS = 4;
String mqtt_topic_proximity_pending_clear[PROXIMITY_PENDING_CLEARS];

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
int nfc_reset_count = 0;

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
    setupDisplay();
    letter_animation.duration(ANIMATION_DURATION_MS);
    letter_animation.scale(ANIMATION_SCALE);

    // Allocate image buffers. Failure is fatal.
    image = image1 = new uint16_t[PIXEL_COUNT];
    previous_image = image2 = new uint16_t[PIXEL_COUNT];
    memset(image1, 0, PIXEL_COUNT * sizeof(uint16_t));
    memset(image2, 0, PIXEL_COUNT * sizeof(uint16_t));
  }

  void setupDisplay() {
    display_config.clkphase = false;
    display_config.double_buff = true;
    
    int8_t* rgb = current_rgb_order == RGB_ORDER_BGR ? bgr_pins : rgb_pins;
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

  void setSlotRotation(int slot) {
    rotation = (slot <= 6) ? 2 : 0;
    led_display->setRotation(rotation);
    is_dirty = true;
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

    // setFont(NULL) shifts the cursor up 6px when a custom font was active, so it
    // must run before setCursor or the two buffers disagree on the y position.
    led_display->setTextSize(1);
    led_display->setFont(NULL);
    led_display->setTextColor(RED, BLACK);

    // Write the same text to both DMA buffers so it survives subsequent flips.
    led_display->setCursor(1, y_pos);
    led_display->print(message);
    led_display->flipDMABuffer();
    led_display->setCursor(1, y_pos);
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

// Set by detectSensorMode() when stage 2 rejects a reader; published from
// subscribeSlotTopics() once mqtt_topic_cube exists. RTC_DATA_ATTR for the
// same reason as cached_sensor_mode below: detectSensorMode() only runs once
// per power session, so a wake that used plain RAM here would republish an
// empty report and this diagnostic would go stale until the next power cycle.
RTC_DATA_ATTR static char sensor_probe_report[64] = "";

// Survives deep sleep, cleared by a power cycle. The cable cannot change while
// the cube is powered, and setup() re-runs on every timer wake, so probing once
// per power session is both sufficient and necessary: stage 2 drives four lines
// that are open-drain sensors on the other board.
RTC_DATA_ATTR SensorMode cached_sensor_mode = SENSOR_MODE_UNKNOWN;

static SensorMode detectSensorMode() {
  uint8_t high_mask = 0;
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(HALL_ID_PINS[i], INPUT_PULLDOWN);
  }
  delay(2);  // let the ~45k internal pulldown settle against stray capacitance
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(HALL_ID_PINS[i]) == HIGH) high_mask |= (1 << i);
  }
  if (!shouldRunActiveProbe(high_mask)) {
    return SENSOR_MODE_MAGNETS;
  }

  // No open-drain sensors on the connector, so driving it is safe.
  initializeNfcReader();
  SPI.begin(SCK, miso_pin, MOSI, SS);
  nfc_reader->begin();
  nfc_reader->reset();

  uint8_t version[2] = {0, 0};
  uint8_t die[16] = {0};
  nfc_reader->readEEprom(PRODUCT_VERSION, version, sizeof(version));
  nfc_reader->readEEprom(DIE_IDENTIFIER, die, sizeof(die));
  if (pn5180ReaderPresent(version, die, sizeof(die))) {
    return SENSOR_MODE_NFC;
  }

  // Only slot 1's reader was measured. A different production lot reports a
  // different version and lands here, so record what was read rather than
  // leaving it to be guessed from a cube that says "magnets".
  // subscribeSlotTopics() publishes this once there is a topic to publish to.
  snprintf(sensor_probe_report, sizeof(sensor_probe_report),
           "ver=%02X%02X die=%02X%02X%02X%02X",
           version[0], version[1], die[0], die[1], die[2], die[3]);
  return SENSOR_MODE_MAGNETS;
}

// ============= Network Functions =============
uint8_t getCubeIpOctet() {
  String mac_address = WiFi.macAddress();
  const CubeMacEntry* entry = findCubeEntry(mac_address.c_str());
  if (!entry) {
    Serial.print("FATAL: MAC not in cube table: ");
    Serial.println(mac_address);
    while (true) {
      delay(1000);
    }
  }
  int cube_id = entry->cube_id;
  current_rgb_order = entry->rgb_order;
  compiled_cube_id = cube_id;

  // Configure pins based on cube ID
  configurePins(cube_id);
  // "probed" vs "cached" is the difference between a reading of the connector
  // and a reading of RTC memory. A cable swapped without a power cycle reports
  // the old mode, and only this line says which one you are looking at.
  bool probed = cached_sensor_mode == SENSOR_MODE_UNKNOWN;
  if (probed) {
    cached_sensor_mode = detectSensorMode();
  }
  sensor_mode = cached_sensor_mode;
  Serial.printf("sensor_mode: %s (%s)%s%s\n",
                sensorModeIsMagnets() ? "magnets" : "nfc",
                probed ? "probed" : "cached",
                sensor_probe_report[0] ? " " : "", sensor_probe_report);
  initialiseNeighbourSensor();

  Serial.print("mac_address: ");
  Serial.println(mac_address);
  Serial.print("cube_id: ");
  Serial.println(compiled_cube_id);
  return entry->ip_octet;
}

void startWiFiConnectionAttempt() {
  Serial.print("Connecting to ");
  Serial.println(SSID_NAME_PORTABLE);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.begin(SSID_NAME_PORTABLE, WIFI_PASSWORD_PORTABLE);
  wifi_connection_attempt_started = millis();
  wifi_connection_attempt_active = true;
}

void serviceWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connection_attempt_active = false;
    next_wifi_connection_attempt = 0;
    return;
  }

  unsigned long now = millis();
  if (wifi_connection_attempt_active) {
    if (now - wifi_connection_attempt_started < WIFI_CONNECT_ATTEMPT_TIMEOUT_MS) {
      return;
    }

    Serial.println("WiFi connection attempt timed out");
    WiFi.disconnect();
    wifi_connection_attempt_active = false;
    next_wifi_connection_attempt = now + WIFI_RETRY_INTERVAL_MS;
    return;
  }

  if (next_wifi_connection_attempt == 0 ||
      static_cast<long>(now - next_wifi_connection_attempt) >= 0) {
    startWiFiConnectionAttempt();
  }
}

void setupWiFiConnection() {
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

  startWiFiConnectionAttempt();
  Serial.println("WiFi connection started; setup will continue offline");
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
  if (nfc_worker_handle != nullptr) {
    xTaskNotifyGive(nfc_worker_handle);
    return;
  }
  if (nfc_reader != nullptr) {
    nfc_reader->reset();
    nfc_reader->setupRF();
  }
}

void publishAutoSleepFlag() {
  mqtt_client.publish("cube/device/" + mac_nocolons + "/auto_sleep", "1", true);
  if (!mqtt_topic_cube.isEmpty()) {
    mqtt_client.publish(mqtt_topic_cube + "/auto_sleep", "1", true);
  }
  delay(100);  // Give MQTT time to flush before sleep
}

void enterSleepMode() {
  debugPrintln("Entering deep sleep mode...");
  // display_manager is null on a timer-wake check-in that never powered the
  // panel — skip the "sleep..." paint and its 2s dwell so that pulse stays cheap.
  if (display_manager != nullptr) {
    display_manager->displayDebugMessage("sleep...");
    delay(2000);
  }

  if (mqtt_client.isConnected()) {
    publishPresence("sleeping");
    // Settle before tearing the connection down. Deliberately no loop() here:
    // enterSleepMode() is reachable from the sleep_now subscribe callback, so
    // pumping the client would re-enter PubSubClient::loop() while it is still
    // dispatching -- and it would buy nothing, since publish() writes straight
    // to the socket rather than queueing.
    delay(100);
    mqtt_client.disconnect();
    // PubSubClient::disconnect() writes the DISCONNECT packet and immediately
    // stops the socket, so those bytes race the radio going down. When they
    // lose, the broker never sees a clean disconnect and ~22.5s later (1.5x
    // the k15 keep-alive PubSubClient defaults to) it publishes the retained
    // last will -- overwriting the correct "sleeping" record with "offline".
    // Seen on 2026-08-06: cube 3 published "sleeping" at 02:21:09 and the
    // broker logged "has exceeded timeout, disconnecting" at 02:21:32.
    delay(100);
  }

#ifdef BOARD_V6
  // Stop DMA and tri-state HUB75 pins to prevent backfeed through panel clamping diodes.
  // Hold all GPIO states through deep sleep so tri-stated pins don't float on power-down.
  // On a check-in re-sleep the panel was never powered and DMA never started, so there
  // is nothing to tear down — the pads are already Hi-Z after gpio_deep_sleep_hold_dis().
  if (display_manager != nullptr) {
    display_manager->shutdownForSleep();
  }
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

  sleep_start_time = millis();

  // Send debug via UDP
  char dbg[64];
  snprintf(dbg, sizeof(dbg), "sleeping for %lu seconds", sleep_interval_s);
  debugSend(dbg);
  Serial.printf("Will wake on Pin 0 release or every %lu seconds...\n", sleep_interval_s);
  Serial.flush();
  
  esp_deep_sleep_start();
}

class KeepAliveCheckInPorts : public WakeCheckInPorts {
 public:
  KeepAliveCheckInPorts() : mqtt_(tcp_) {
    StoredSlot stored = loadStoredSlot();
    device_topic_ = "cube/device/" + mac_nocolons + "/auto_sleep";
    slot_topic_ = stored.slot > 0
        ? "cube/" + String(stored.slot) + "/auto_sleep"
        : String("");
    status_topic_ = "cube/device/" + mac_nocolons + "/status";
    mqtt_.setServer(MQTT_SERVER_PI, MQTT_PORT);
    mqtt_.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  }

  // setupWiFiConnection() fired a non-blocking WiFi.begin(); WiFi may not be
  // associated yet, so give it until KEEPALIVE_WIFI_TIMEOUT_MS before treating
  // the check-in as a network failure.
  bool awaitWifi() override {
    unsigned long wifi_wait_start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifi_wait_start < KEEPALIVE_WIFI_TIMEOUT_MS) {
      delay(10);
    }
    char dbg[64];
    snprintf(dbg, sizeof(dbg), "wifi assoc %lums", millis() - wifi_wait_start);
    // Also to serial: a failed association is the case KEEPALIVE_WIFI_TIMEOUT_MS
    // has to be validated against, and it is the case UDP cannot report.
    debugSend(dbg);
    debugPrintln(dbg);
    if (WiFi.status() != WL_CONNECTED) {
      debugSend("wifi timeout on check-in");
      debugPrintln("wifi timeout on check-in");
      return false;
    }
    return true;
  }

  bool connectMqtt() override {
    String client_id = makeMqttClientId(WiFi.macAddress(), "-ka");
    if (!mqtt_.connect(client_id.c_str())) {
      debugSend("mqtt fail");
      return false;
    }
    debugSend("mqtt ok");
    return true;
  }

  bool hasSlotTopic() override { return !slot_topic_.isEmpty(); }

  // An empty retained topic delivers nothing at all, so elapsed time cannot
  // tell "no flag is set" from "the flag has not arrived yet". The keep-alive
  // publish doubles as a round-trip marker: it is sent after the flag
  // subscriptions, and the broker queues each subscription's retained message
  // as it processes that SUBSCRIBE, so on one TCP connection the marker coming
  // back proves any retained flag was already delivered. Returns false when it
  // does not come back -- the caller must not read silence as "no flag".
  bool readSleepFlags(SleepFlags* out) override {
    flags_ = {false, false};
    marker_seen_ = false;

    // IMPORTANT: Read payload BEFORE any publish() calls — PubSubClient reuses
    // its internal buffer for both incoming and outgoing messages, so
    // publishing inside the callback overwrites the payload bytes.
    mqtt_.setCallback([this](char* topic, byte* payload, unsigned int length) {
      bool requested = length == 1 && payload[0] == '1';
      if (device_topic_ == topic) {
        flags_.device_requests_sleep = requested;
      } else if (slot_topic_ == topic) {
        flags_.slot_requests_sleep = requested;
      } else if (status_topic_ == topic) {
        marker_seen_ = true;
      }
    });

    mqtt_.subscribe(status_topic_.c_str());
    mqtt_.subscribe(device_topic_.c_str());
    if (hasSlotTopic()) {
      mqtt_.subscribe(slot_topic_.c_str());
    }
    mqtt_.publish(status_topic_.c_str(), "keep-alive");

    unsigned long check_start = millis();
    while (true) {
      mqtt_.loop();
      delay(10);
      unsigned long elapsed = millis() - check_start;
      // KEEPALIVE_CHECKIN_WINDOW_MS is also the current-pulse dwell that keeps
      // a USB-C power bank from cutting off, so hold the radio up for the full
      // window even once the marker is back.
      if (elapsed >= KEEPALIVE_CHECKIN_WINDOW_MS && marker_seen_) break;
      if (elapsed >= KEEPALIVE_FLAG_READ_TIMEOUT_MS) break;
    }

    char dbg[64];
    snprintf(dbg, sizeof(dbg), "flags device=%d slot=%d confirmed=%d",
             flags_.device_requests_sleep, flags_.slot_requests_sleep,
             marker_seen_);
    debugSend(dbg);
    *out = flags_;
    return marker_seen_;
  }

  void clearSleepFlags() override {
    mqtt_.publish(device_topic_.c_str(), "", true);
    if (hasSlotTopic()) {
      mqtt_.publish(slot_topic_.c_str(), "", true);
    }
    delay(100);
    mqtt_.disconnect();
  }

  void enterSleep() override {
    mqtt_.disconnect();
    debugSend("sleep again");
    enterSleepMode();
  }

  void stayAwake() override {
    last_activity_time = millis();
    debugSend("WAKE FULL - staying awake");
    Serial.println("Waking fully - continuing setup");
  }

 private:
  WiFiClient tcp_;
  PubSubClient mqtt_;
  String device_topic_;
  String slot_topic_;
  String status_topic_;
  bool marker_seen_ = false;
  SleepFlags flags_ = {false, false};
};

void handleWakeUp() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  WakeReason reason = WAKE_REASON_OTHER;
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    reason = WAKE_REASON_TIMER;
    debugSend("timer wake");
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    reason = WAKE_REASON_BUTTON;
    debugPrintln("Woken by external signal (Pin 0 released)");
  } else {
    debugPrintln("Normal boot - staying awake");
  }

  KeepAliveCheckInPorts ports;
  runWakeCheckIn(reason, ports);
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


// Retried from loop() until the broker accepts it. Only one delete is tracked:
// a second rebinding during a sustained outage drops the earlier tombstone, and
// that is bounded because a cube that cannot publish is not producing new
// proximity values either, so the stale record can only predate the outage.
void flushProximityClears() {
  if (!mqtt_client.isConnected()) return;
  for (String& pending : mqtt_topic_proximity_pending_clear) {
    if (pending.isEmpty()) continue;
    if (mqtt_client.publish(pending, "", true)) {
      pending = "";
    }
  }
}

// Deletes the retained record rather than writing 0, which would be a standing
// "nothing near me" assertion from a cube that is no longer reporting at all.
// Invalidating the cache matters as much as the delete: without it a cube
// rebound to another slot whose closeness happens to match would publish
// nothing, and the new topic would stay empty.
void requestProximityClear(const String& topic) {
  if (topic.isEmpty()) return;
  flushProximityClears();

  for (const String& pending : mqtt_topic_proximity_pending_clear) {
    if (pending == topic) return;
  }
  for (String& pending : mqtt_topic_proximity_pending_clear) {
    if (pending.isEmpty()) {
      pending = topic;
      flushProximityClears();
      return;
    }
  }

  // Every slot taken means as many distinct topics have gone un-deleted as a
  // cube has bindings to give, so the oldest is the one whose slot is least
  // likely to be looked at again. Reaching here at all needs the broker to
  // reject writes across that many rebindings.
  for (size_t i = 1; i < PROXIMITY_PENDING_CLEARS; i++) {
    mqtt_topic_proximity_pending_clear[i - 1] = mqtt_topic_proximity_pending_clear[i];
  }
  mqtt_topic_proximity_pending_clear[PROXIMITY_PENDING_CLEARS - 1] = topic;
  flushProximityClears();
}

void clearRetainedProximity() {
  requestProximityClear(mqtt_topic_cube_proximity);
  mqtt_topic_cube_proximity = "";
  published_proximity = -1;
}

void subscribeSlotTopics() {
  // Retained, so the value outlives the slot it described. Cleared before the
  // topic is rebound, or a reassigned cube leaves the old slot reading as
  // permanently docked.
  clearRetainedProximity();

  mqtt_topic_cube = MQTT_TOPIC_PREFIX_CUBE + cube_identifier;
  mqtt_topic_cube_nfc = String(MQTT_TOPIC_PREFIX_CUBE) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_game_nfc = String(MQTT_TOPIC_PREFIX_GAME) + MQTT_TOPIC_PREFIX_NFC + cube_identifier;
  mqtt_topic_echo = createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_ECHO);
  mqtt_topic_cube_right = String(MQTT_TOPIC_PREFIX_CUBE) + String("right/") + cube_identifier;
  mqtt_topic_cube_proximity = mqtt_topic_cube + "/proximity";

  // Only publish version on first boot, not on wake from sleep
  if (is_first_boot) {
    mqtt_client.publish(createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_VERSION), GIT_VERSION, true);  // retained
  }

  // A cube waking from sleep still needs to report its mode, so this sits
  // outside the is_first_boot guard above.
  mqtt_client.publish(mqtt_topic_cube + "/sensor_mode",
                      sensorModeIsMagnets() ? "magnets" : "nfc", true);
  // Retained, so a report from a rejected probe outlives the cable swap that
  // fixes it -- a cube reading "nfc" would still carry "I probed and found
  // nothing" beside it. Publishing unconditionally clears the record when
  // there is nothing to report.
  mqtt_client.publish(mqtt_topic_cube + "/sensor_probe", sensor_probe_report,
                      true);

  // cube/device/{MAC}/nfc is retained, so a tag read before a cable swap
  // outlives the swap. The game server resolves neighbours from that topic, so
  // the record would be applied as a live neighbour for a cube that no longer
  // has a reader. An empty payload is how a cleared observation is already
  // expressed, so publishing one retires the record.
  if (sensorModeIsMagnets() && !mqtt_topic_device_nfc.isEmpty()) {
    mqtt_client.publish(mqtt_topic_device_nfc, "", true);
  }

  auto resetActivityTimer = []() { last_activity_time = millis(); };

  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "border_bottom_banner", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderBottomBannerCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "border_top_banner", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderTopBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/sleep_interval", handleSleepIntervalCommand);
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "string", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleStringCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleConsolidatedBorderCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_hline_bottom", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderBottomBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_hline_top", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderTopBannerCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_frame", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderFrameCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_height", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderLineHeightCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_left", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderVLineLeftCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_vline_right", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderVLineRightCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/font_size", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleFontSizeCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/flash", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleFlashCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/imagex", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleImageBinaryCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/letter", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleLetterCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/lock", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleLockCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/ping", [resetActivityTimer](const String& msg) { resetActivityTimer(); handlePingCommand(msg); });
#ifdef BOARD_V6
  mqtt_client.subscribe(mqtt_topic_cube + "/power_test", [resetActivityTimer](const String& msg) { resetActivityTimer(); handlePowerTestCommand(msg); });
#endif
  mqtt_client.subscribe(mqtt_topic_cube + "/reset", [resetActivityTimer](const String& msg) { resetActivityTimer(); handleResetCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_game_nfc, [resetActivityTimer](const String& msg) { resetActivityTimer(); handleNfcCommand(msg); });

  // Publish initial "no neighbor" state so game server sees all cubes on startup
  mqtt_client.publish(mqtt_topic_cube_nfc, "-", true);
  if (sensorModeIsMagnets()) {
    mqtt_client.publish(mqtt_topic_cube_right, "-", true);
    strncpy(last_right_published, "-", sizeof(last_right_published) - 1);
    last_right_published[sizeof(last_right_published) - 1] = '\0';
  } else {
    // cube/right is retained, so the last edge a hall board reported outlives
    // the swap back to a reader. The game server applies every cube/right
    // payload without consulting sensor_mode, so the broker replays that edge
    // on its next subscribe and a word can form around a neighbour that has
    // not existed since the cable changed.
    //
    // Empty, not "-": an empty payload deletes the retained record rather than
    // leaving a standing "no neighbour" assertion, and it is already how a
    // cleared edge is expressed. The NFC path re-announces its own observation
    // right after this -- last_observation_published is reset just before
    // subscribeSlotTopics() runs -- so clearing here cannot strand the edge
    // the observation path owns.
    mqtt_client.publish(mqtt_topic_cube_right, "", true);
    last_right_published[0] = '\0';
    // Nothing writes proximity outside the magnets loop, so a cube that reported
    // a docked neighbour and came back as a reader would keep asserting it.
    requestProximityClear(mqtt_topic_cube_proximity);
  }
}

bool slotIsResolved() {
  return slot_resolved && applied_slot > 0;
}

void publishPresence(const char* state) {
  if (mqtt_topic_presence.isEmpty()) {
    return;
  }
  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"protocol\":1,\"state\":\"%s\",\"boot_id\":\"%s\","
           "\"applied_slot\":%d,\"applied_generation\":%lu}",
           state, boot_id.c_str(), applied_slot,
           static_cast<unsigned long>(applied_generation));
  mqtt_client.publish(mqtt_topic_presence, payload, true);
}

void applySlot(int slot) {
  slot_resolved = true;
  applied_slot = slot;

  if (slot <= 0) {
    cube_identifier = "";
    mqtt_topic_cube = "";
    display_manager->displayDebugMessage("NO SLOT");
    debugSend("unassigned: idle");
    if (!mqtt_topic_device_nfc.isEmpty()) {
      mqtt_client.publish(mqtt_topic_device_nfc, "", true);
    }
    clearRetainedProximity();
    publishPresence("online");
    return;
  }

  cube_identifier = String(slot);
  display_manager->setSlotRotation(slot);
  subscribeSlotTopics();
  debugSend((String("slot ") + cube_identifier).c_str());
  publishPresence("online");
}

void handleAuthorityMarker(const String& message) {
  if (message.indexOf("\"authoritative\"") < 0 ||
      message.indexOf("true") < 0) {
    return;
  }
  if (!authority_latched) {
    authority_latched = true;
    latchAuthority();
    debugSend("authority latched");
  }
}

void handleAssignmentRecord(const String& message) {
  CubeAssignment assignment;
  AssignmentParseResult result =
      parseAssignmentRecord(message.c_str(), &assignment);
  if (!assignmentRecordIsActionable(result)) {
    debugSend(result == ASSIGNMENT_MISSING
                  ? "assignment missing: keeping current slot"
                  : "assignment malformed: keeping current slot");
    return;
  }
  int slot = resolveAssignedSlot(
      result, assignment.slot, authority_latched, compiled_cube_id);

  if (!slot_resolved) {
    applied_generation = assignment.generation;
    saveStoredSlot(slot, assignment.generation);
    applySlot(slot);
    return;
  }

  if (slot != applied_slot) {
    saveStoredSlot(slot, assignment.generation);
    debugSend("slot changed: rebooting");
    delay(200);
    ESP.restart();
  }
  applied_generation = assignment.generation;
}

void handleLivenessRequest(const String& nonce) {
  if (nonce.isEmpty() || !slotIsResolved()) {
    return;
  }
  char payload[224];
  snprintf(payload, sizeof(payload),
           "{\"protocol\":1,\"nonce\":\"%s\",\"boot_id\":\"%s\","
           "\"generation\":%lu,\"applied_slot\":%d}",
           nonce.c_str(), boot_id.c_str(),
           static_cast<unsigned long>(applied_generation), applied_slot);
  mqtt_client.publish(mqtt_topic_liveness_response, payload, false);
}

void onConnectionEstablished() {
  debugSend("MQTT connected");

  mqtt_topic_assign = String("cube/assign/") + mac_nocolons;
  mqtt_topic_device_nfc = String("cube/device/") + mac_nocolons + "/nfc";
  mqtt_topic_liveness_response =
      String("cube/device/") + mac_nocolons + "/liveness-response";

  mqtt_client.subscribe("cube/roster/authoritative", handleAuthorityMarker);
  mqtt_client.subscribe(mqtt_topic_assign, handleAssignmentRecord);
  mqtt_client.subscribe(
      String("cube/device/") + mac_nocolons + "/liveness-request",
      handleLivenessRequest);

  auto resetActivityTimer = []() { last_activity_time = millis(); };
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "brightness", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBrightnessCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "reboot", [resetActivityTimer](const String& msg) { resetActivityTimer(); handleRebootCommand(msg); });
  mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "sleep_now", handleSleepNowCommand);
  mqtt_client.subscribe("cube/resend", [](const String&) {
    // Re-announce what we see now. Publish-on-change alone would leave a
    // cleared record unrestored until the neighbor physically moved.
    last_observation_published[0] = '\0';
  });

  last_observation_published[0] = '\0';

  if (slotIsResolved()) {
    subscribeSlotTopics();
    publishPresence("online");
  } else if (!slot_resolved) {
    assignment_wait_started = millis();
  } else {
    publishPresence("online");
  }

  if (last_activity_time == 0) {
    last_activity_time = millis();
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

// ============= Hall Neighbor Functions =============
// Maps a 6-bit ID mask (bits P6 P5 P4 P3 P2 P1) to a neighbor cube id;
// 0 = invalid pattern. Ids match the NFC tag table (cube_tags.cpp): player 0
// is cubes 1-6, player 1 is cubes 11-16. Populate each cube's ID magnets with
// the pattern that decodes to its game id.
static uint8_t hallCubeIdForMask(uint8_t id_mask) {
  switch (id_mask & 0x3F) {
    case 0b000011: return 1;
    case 0b000101: return 2;
    case 0b001001: return 3;
    case 0b010001: return 4;
    case 0b100001: return 5;
    case 0b000110: return 6;
    case 0b001010: return 11;
    case 0b010010: return 12;
    case 0b100010: return 13;
    case 0b001100: return 14;
    case 0b010100: return 15;
    case 0b100100: return 16;
    default:       return 0;
  }
}

static HallPresenceTracker hall_presence;

// The tracker primes its baseline from its first sample, which is blind to a
// magnet that is already there: a cube that wakes docked subtracts the
// neighbour into its own baseline and reports no neighbour until it is pulled
// away for the ~32s the baseline needs to decay. Carrying the baseline across
// the wake removes the guess.
//
// RTC_NOINIT_ATTR, not RTC_DATA_ATTR: the bootloader re-initialises .rtc.data
// on every reset except a deep-sleep wake, so an OTA reboot lands docked with
// nothing saved -- measured on slot 1, which primed to 1954 against a true
// baseline of 1771. The noinit segment is left alone and survives a software
// reset too. Nothing initialises it on a cold boot, hence the magic word:
// unset reads as garbage rather than as zero.
#define PRESENCE_BASELINE_MAGIC 0x48414c42u  // "HALB"
#define PRESENCE_BASELINE_SAVE_RETRY_MS 1000
#define PRESENCE_BASELINE_MIN   100
#define PRESENCE_BASELINE_MAX   4000
RTC_NOINIT_ATTR static uint32_t saved_presence_magic;
RTC_NOINIT_ATTR static int32_t saved_presence_baseline;

static bool plausiblePresenceBaseline(int baseline) {
  return baseline >= PRESENCE_BASELINE_MIN && baseline <= PRESENCE_BASELINE_MAX;
}

// 0 tells the tracker to prime from its first sample. RTC first because it is
// current to the last poll; NVS is the cold-boot fallback, stale by however
// long the cube sat powered off but still taken with no magnet in range.
static int restoredPresenceBaseline() {
  if (saved_presence_magic == PRESENCE_BASELINE_MAGIC &&
      plausiblePresenceBaseline(saved_presence_baseline)) {
    return (int)saved_presence_baseline;
  }
  const int stored = loadPresenceBaseline();
  return plausiblePresenceBaseline(stored) ? stored : 0;
}

void setupHallSensors() {
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(HALL_ID_PINS[i], INPUT);
  }
  pinMode(HALL_PRESENCE_PIN, INPUT);
  hall_presence.begin({HALL_PRESENCE_DIRECTION,
                       HALL_PRESENCE_ON_DELTA,
                       HALL_PRESENCE_OFF_DELTA,
                       HALL_PRESENCE_FAST_SHIFT,
                       HALL_PRESENCE_BASE_SHIFT,
                       HALL_PRESENCE_BASE_INTERVAL_MS},
                      restoredPresenceBaseline());

  Serial.println(F("Hall neighbor sensors initialized"));
}

// Returns the neighbor's cube id, or 0 for no/invalid neighbor.
uint8_t readHallNeighborId() {
  bool presence_active = hall_presence.update(analogRead(HALL_PRESENCE_PIN), millis());

  if (!presence_active) {
    return 0;  // presence magnet absent -> no neighbor seated
  }
  uint8_t id_mask = 0;
  for (uint8_t i = 0; i < 6; i++) {
    if (digitalRead(HALL_ID_PINS[i]) == HALL_ID_ACTIVE_LEVEL) {
      id_mask |= (1 << i);
    }
  }
  if (__builtin_popcount(id_mask) != 2) {
    return 0;  // reject anything that isn't exactly two ID magnets
  }
  return hallCubeIdForMask(id_mask);  // 0 = invalid weight-2 pattern
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

void nfcWorkerTask(void* /*parameter*/) {
  constexpr uint32_t NFC_RETRY_DELAY_MS = 50;
  constexpr uint32_t NFC_RECOVERY_BACKOFF_MS = 5000;
  bool manual_reset_requested = false;

  for (;;) {
    NfcWorkerResult worker_result = {};

    unsigned long read_start = micros();
    worker_result.read_result = readNfcCard(worker_result.card_id);
    worker_result.read_us = micros() - read_start;

    if (manual_reset_requested || worker_result.read_us > 100000UL) {
      worker_result.recovery_attempted = true;
      unsigned long recovery_start = micros();
      nfc_reader->reset();
      worker_result.recovery_succeeded = nfc_reader->setupRF();
      worker_result.recovery_us = micros() - recovery_start;
    }

    xQueueOverwrite(nfc_result_queue, &worker_result);

    uint32_t delay_ms =
      worker_result.recovery_attempted && !worker_result.recovery_succeeded
        ? NFC_RECOVERY_BACKOFF_MS
        : NFC_RETRY_DELAY_MS;
    // Wake immediately for a reset, then carry it into the next iteration.
    manual_reset_requested =
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms)) > 0;
  }
}

bool startNfcWorker() {
  nfc_result_queue = xQueueCreate(1, sizeof(NfcWorkerResult));
  if (nfc_result_queue == nullptr) {
    Serial.println(F("ERROR: failed to create NFC result queue"));
    return false;
  }

  BaseType_t task_created = xTaskCreatePinnedToCore(
    nfcWorkerTask,
    "nfc-worker",
    4096,
    nullptr,
    1,
    &nfc_worker_handle,
    ARDUINO_RUNNING_CORE
  );
  if (task_created != pdPASS) {
    vQueueDelete(nfc_result_queue);
    nfc_result_queue = nullptr;
    nfc_worker_handle = nullptr;
    Serial.println(F("ERROR: failed to create NFC worker"));
    return false;
  }
  return true;
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
      else if (!slotIsResolved() &&
               (strcmp(udpBuffer, "timing") == 0 ||
                strcmp(udpBuffer, "diag") == 0 ||
                strcmp(udpBuffer, "chip") == 0 ||
                strcmp(udpBuffer, "temp") == 0)) {
        const char* marker = slot_resolved ? "unassigned" : "unresolved";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)marker, strlen(marker));
        udp.endPacket();
      }
      // Check if message is "timing" - return cube_id:avg_loop_time_us
      else if (slotIsResolved() && strcmp(udpBuffer, "timing") == 0) {
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
      else if (slotIsResolved() && strcmp(udpBuffer, "diag") == 0) {
        char diagStr[320];
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
          "%s|fw=%s|mac=%s|loop=%lu|mqtt=%lu|disp=%lu|udp=%lu|nfc=%lu|nfc_max=%lu|nfc_resets=%d|letter_avg=%lu|letter_max=%lu|letter_n=%d|rssi=%d|samples=%d|uptime_ms=%lu",
          cube_identifier.c_str(), fw_board, WiFi.macAddress().c_str(), avg_total, avg_mqtt, avg_display, avg_udp, avg_nfc,
          nfc_read_max_us, nfc_reset_count, avg_letter_interval, max_letter_interval, letter_interval_count,
          WiFi.RSSI(), section_timing_count, millis());

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)diagStr, strlen(diagStr));
        udp.endPacket();

        // Reset auto-sleep timer - active diagnostics should keep cube awake
        last_activity_time = millis();

        // Reset accumulators after reading
        section_timing_accum = {0, 0, 0, 0, 0};
        section_timing_count = 0;
        letter_interval_accum = 0;
        letter_interval_count = 0;
        max_letter_interval = 0;
        nfc_read_max_us = 0;
      }
      // Check if message is "chip" - return ESP32 chip info
      else if (slotIsResolved() && strcmp(udpBuffer, "chip") == 0) {
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        char chipStr[128];
        snprintf(chipStr, sizeof(chipStr),
          "%s|model=%d|cores=%d|revision=%d|features=%lu",
          cube_identifier.c_str(), chip_info.model, chip_info.cores,
          chip_info.revision, chip_info.features);

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)chipStr, strlen(chipStr));
        udp.endPacket();

        Serial.printf("Sent chip info to %s:%d: %s\n",
                      udp.remoteIP().toString().c_str(), udp.remotePort(), chipStr);
      }
      // Check if message is "temp" - return cube_id:temperature_celsius
      else if (slotIsResolved() && strcmp(udpBuffer, "temp") == 0) {
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

  // A timer wake is a keep-alive check-in: stay dark and skip display init until
  // handleWakeUp() decides we are actually waking. First boot and button wakes
  // light the panel immediately.
  const bool is_timer_wake = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);

  mqtt_client.enableDebuggingMessages(true);
  mqtt_client.setMaxPacketSize(11999);
  Serial.printf("memory available: %d\n", ESP.getFreeHeap());
  mqtt_client.enableDebuggingMessages(false);
  mqtt_client.setMqttConnectionTimeout(MQTT_CONNECTION_TIMEOUT_MS);
  mqtt_client.setMqttReconnectionAttemptDelay(MQTT_RECONNECT_DELAY_MS);
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
  // Release holds set in enterSleepMode(). A timer-wake check-in keeps the
  // TPS22975 (and HUB75 panel) off so the wake draws only WiFi current; the
  // panel is powered later, in the full-wake path, once we commit to waking.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(POWER_SWITCH_PIN);
  pinMode(POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(POWER_SWITCH_PIN, is_timer_wake ? LOW : HIGH);

  // Initialize Hall effect sensor on GPIO36
#if defined(HALL_SENSOR_ENABLED)
  pinMode(HALL_SENSOR_PIN, INPUT);
#elif defined(HALL_SENSOR_ANALOG)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(HALL_SENSOR_PIN, INPUT);
#endif
#endif
  
  // Initialize WiFi and get cube identifier
  debugPrintln("setting up wifi...");
  setupWiFiConnection();
  debugPrintln("wifi done");

  mac_nocolons = removeColonsFromMac(WiFi.macAddress());
  char boot_id_buf[9];
  snprintf(boot_id_buf, sizeof(boot_id_buf), "%08X", esp_random());
  boot_id = boot_id_buf;
  mqtt_topic_presence =
      String("cube/device/") + mac_nocolons + "/presence";
  StoredSlot stored = loadStoredSlot();
  authority_latched = stored.authority_latched;
  static String last_will_payload =
      String("{\"protocol\":1,\"state\":\"offline\",\"boot_id\":\"") +
      boot_id + "\",\"applied_slot\":" + String(stored.slot) +
      ",\"applied_generation\":" + String(stored.generation) + "}";
  mqtt_client.enableLastWillMessage(
      mqtt_topic_presence.c_str(), last_will_payload.c_str(), true);

  String cube_id = String(compiled_cube_id);

  // A power cycle is someone picking the cube up, and it gets an answer before
  // the check-in below can send it back to sleep. Without this a cold boot that
  // finds a retained auto_sleep flag never reaches any display code, so a
  // working cube is indistinguishable from dead hardware — which is exactly how
  // a stale cube/N/auto_sleep once read as bad firmware.
  //
  // Power-on only, which is narrower than is_first_boot: that covers every
  // non-deep-sleep reset, so a brownout or watchdog on a stored cube would
  // light the panel, dwell 2s on "sleep..." and tear DMA down again on each
  // glitch -- the battery burn runWakeCheckIn() re-reads the flag to avoid.
  // esp_reset_reason() separates the two, and an EN-pin reset reports
  // ESP_RST_POWERON, so a bench reset still exercises this path.
  //
  // A timer wake stays dark: skipping the panel is what makes the keep-alive
  // pulse cheap, and the rail is still held off up here. On a power-on the rail
  // is already up and setupWiFiConnection() above gave it time to settle, so no
  // settle delay is needed.
  if (is_first_boot && esp_reset_reason() == ESP_RST_POWERON) {
    display_manager = new DisplayManager(cube_id);
    display_manager->clearDebugDisplay();
    display_manager->displayDebugMessage(GIT_TIMESTAMP);
  }

  // Decide whether this wake is a keep-alive check-in or a real wake. On a
  // check-in that stays asleep this re-enters deep sleep and never returns, so
  // the debug paints below are skipped; enterSleepMode() paints "sleep..." and
  // tears the panel down when the block above has already built it.
  handleWakeUp();

  // Reaching here means we are fully waking: first boot, button wake, or a
  // check-in whose auto_sleep flag was cleared.
#ifdef BOARD_V6
  // Power the panel now. On a timer wake the rail was held off above, so raise
  // it and let the 5V rail settle before I2S DMA starts driving the panel. On a
  // button/first boot the rail was raised early and WiFi setup already gave it
  // time to come up.
  if (is_timer_wake) {
    digitalWrite(POWER_SWITCH_PIN, HIGH);
    delay(POWER_RAIL_SETTLE_MS);
  }
#endif

  debugSend("setup: continuing normally");

  // Already built above on a first boot; a timer or button wake arrives here
  // with nothing on the panel.
  if (display_manager == nullptr) {
    display_manager = new DisplayManager(cube_id);
    display_manager->clearDebugDisplay();
    display_manager->displayDebugMessage(GIT_TIMESTAMP);
  }
  delay(DISPLAY_STARTUP_DELAY_MS);
  display_manager->displayDebugMessage((String("wake:") + String(wakeup_reason)).c_str());
  Serial.println(cube_id);
  static String client_name = makeMqttClientId(WiFi.macAddress(), "");
  Serial.println(client_name);
  mqtt_client.setMqttClientName(client_name.c_str());
  char ipDisplay[64];
  snprintf(ipDisplay, sizeof(ipDisplay), "%d",
    WiFi.localIP()[3]);
  display_manager->displayDebugMessage(ipDisplay);

  debugPrintln(WiFi.macAddress().c_str());

  if (sensorModeIsMagnets()) {
    debugPrintln("setting up hall neighbor sensors...");
    setupHallSensors();
    display_manager->displayDebugMessage("hall id");
  } else {
    // Self-test: check BUSY pin state before init (should be LOW)
    pinMode(pn5180_busy_pin, INPUT);
    bool busy_before_init = digitalRead(pn5180_busy_pin);

    debugPrintln("setting up nfc reader...");
    setupNfcReader();
    debugPrintln("nfc reader done");

    // Self-test: timed NFC read
    uint8_t test_card_id[NFCID_LENGTH];
    unsigned long nfc_test_start = micros();
    readNfcCard(test_card_id);
    unsigned long nfc_test_us = micros() - nfc_test_start;

    char nfc_test_result[32];
    if (busy_before_init) {
      snprintf(nfc_test_result, sizeof(nfc_test_result), "nfc:BUSY!");
    } else if (nfc_test_us > 100000UL) {
      snprintf(nfc_test_result, sizeof(nfc_test_result), "nfc:SLOW %lums", (nfc_test_us + 500) / 1000);
    } else {
      snprintf(nfc_test_result, sizeof(nfc_test_result), "nfc %lums", (nfc_test_us + 500) / 1000);
    }
    display_manager->displayDebugMessage(nfc_test_result);

    if (!startNfcWorker()) {
      display_manager->displayDebugMessage("nfc task err");
    }
  }

  debugPrintln("setting up udp...");
  setupUDP(); // Add UDP setup

  debugPrintln(F("Setup Complete"));
}

void loop() {
  loop_start_time = micros();

  static bool last_hall_present = true;

  serviceWiFiConnection();

  unsigned long section_start = micros();
  mqtt_client.loop();
  unsigned long mqtt_end = micros();
  unsigned long mqtt_us = mqtt_end - section_start;

  // Outside the magnets branch: a cube that resolved as a reader is exactly the
  // one whose stale proximity needs deleting, and it never enters that branch.
  flushProximityClears();

  if (!slot_resolved && assignment_wait_started != 0 &&
      millis() - assignment_wait_started >= ASSIGNMENT_WAIT_MS) {
    assignment_wait_started = 0;
    StoredSlot stored = loadStoredSlot();
    int fallback = stored.slot > 0 ? stored.slot : compiled_cube_id;
    int slot = resolveAssignedSlot(
        ASSIGNMENT_MISSING, -1, authority_latched, fallback);
    applied_generation = stored.generation;
    saveStoredSlot(slot, stored.generation);
    applySlot(slot);
  }

  if (last_activity_time > 0 &&
      (millis() - last_activity_time > AUTO_SLEEP_TIMEOUT_MS)) {
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

  unsigned long nfc_us = 0;
  if (!sensorModeIsMagnets()) {
    NfcWorkerResult worker_result;
    if (slotIsResolved() && nfc_result_queue != nullptr &&
        xQueueReceive(nfc_result_queue, &worker_result, 0) == pdTRUE) {
      uint8_t* card_id = worker_result.card_id;
      ISO15693ErrorCode read_result = worker_result.read_result;
      nfc_us = worker_result.read_us + worker_result.recovery_us;

      if (worker_result.recovery_attempted) {
        nfc_reset_count++;
        Serial.printf(
          "NFC recovery %s: read=%lu us recovery=%lu us\n",
          worker_result.recovery_succeeded ? "succeeded" : "failed",
          worker_result.read_us,
          worker_result.recovery_us
        );
      }

      // Always publish NFC tag IDs (needed for nfc_control_daemon).
      // Only gate neighbor observations on hall sensor state.
      bool hall_allows_neighbor = !HAS_HALL_SENSOR || last_hall_present || HAS_HALL_ANALOG;
      bool hall_says_present = HAS_HALL_SENSOR && last_hall_present;
      char neighbor_id[NFCID_LENGTH * 2 + 1] = "";

      if (read_result == ISO15693_EC_OK) {
        convertNfcIdToHexString(card_id, NFCID_LENGTH, neighbor_id);
        if (strcmp(neighbor_id, last_neighbor_id) != 0) {
          debugPrintln(F("New card"));
          unsigned long publish_start = millis();
          bool success = mqtt_client.publish(mqtt_topic_cube_nfc, neighbor_id, true);
          unsigned long publish_end = millis();
          Serial.printf("[%lu] MQTT publish took %lu ms - payload: %s - success: %d\n", publish_end, publish_end - publish_start, neighbor_id, success);
          if (success) {
            strncpy(last_neighbor_id, neighbor_id, sizeof(last_neighbor_id) - 1);
            last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
          }
        }
      } else if (read_result == EC_NO_CARD) {
        // /nfc reflects raw NFC reads with no debouncing (debug-only topic).
        if (strcmp(last_neighbor_id, "-") != 0) {
          debugPrintln(F("No card detected"));
          unsigned long publish_start = millis();
          bool success = mqtt_client.publish(mqtt_topic_cube_nfc, "-", true);
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

      // Resolution moved to the server: publish the raw tag keyed by MAC and let
      // the roster decide which slot wears it. cube/right is no longer published
      // from this path. The gating is unchanged -- "-" still requires both
      // sensors to agree, which is what stops an NFC flake breaking a word.
      if (slotIsResolved()) {
        NfcObservationAction action = decideNfcObservation(
            read_result == ISO15693_EC_OK, read_result == EC_NO_CARD,
            hall_allows_neighbor, hall_says_present, neighbor_id,
            last_observation_published);
        if (action != NFC_OBS_NONE) {
          const char* tag = (action == NFC_OBS_TAG) ? neighbor_id : "-";
          char payload[160];
          buildObservationPayload(boot_id.c_str(), tag, payload, sizeof(payload));
          if (mqtt_client.publish(mqtt_topic_device_nfc, payload, true)) {
            strncpy(last_observation_published, tag,
                    sizeof(last_observation_published) - 1);
            last_observation_published[sizeof(last_observation_published) - 1] = '\0';
          }
        }
      }

      if (nfc_us > nfc_read_max_us) {
        nfc_read_max_us = nfc_us;
      }
    }
  } else {
    // Hall 2-of-6 neighbor decode: poll ~1 kHz, debounce, publish the neighbor
    // cube id to cube/right/<sender> exactly as the NFC path does.
    if (slotIsResolved()) {
      static unsigned long last_hall_poll = 0;
      static uint8_t candidate_id = 0;
      static int candidate_count = 0;
      static uint8_t stable_id = 0xFF;  // sentinel forces first real publish
    
      // For debugging raw ID sensors U1-U6
      static uint8_t candidate_raw = 0;
      static int candidate_raw_count = 0;
      static uint8_t stable_raw = 0xFF;

      static unsigned long last_presence_publish = 0;
      static int published_presence_delta = 0;
      static bool published_presence_active = false;
      static bool presence_ever_published = false;

      if (current_time - last_hall_poll >= HALL_POLL_INTERVAL_MS) {
        last_hall_poll = current_time;
      
        uint8_t raw = 0;
        for (uint8_t i = 0; i < 6; i++) {
          if (digitalRead(HALL_ID_PINS[i]) == HALL_ID_ACTIVE_LEVEL) {
            raw |= (1 << i);
          }
        }
      
        if (raw == candidate_raw) {
          if (candidate_raw_count < HALL_DEBOUNCE_READS) candidate_raw_count++;
        } else {
          candidate_raw = raw;
          candidate_raw_count = 1;
        }
      
        if (candidate_raw_count >= HALL_DEBOUNCE_READS && candidate_raw != stable_raw) {
          stable_raw = candidate_raw;
          char raw_buf[7];
          for (int i = 0; i < 6; i++) {
            raw_buf[i] = (stable_raw & (1 << i)) ? '1' : '0';
          }
          raw_buf[6] = '\0';
          if (mqtt_client.isConnected()) {
            mqtt_client.publish(mqtt_topic_cube + "/hall_debug", raw_buf, true);
          }
        }

        uint8_t id = readHallNeighborId();
        if (id == candidate_id) {
          if (candidate_count < HALL_DEBOUNCE_READS) candidate_count++;
        } else {
          candidate_id = id;
          candidate_count = 1;
        }
        if (candidate_count >= HALL_DEBOUNCE_READS && candidate_id != stable_id) {
          char buf[8];
          if (candidate_id > 0) {
            snprintf(buf, sizeof(buf), "%d", candidate_id);
          } else {
            strcpy(buf, "-");  // no/invalid neighbor
          }

          // stable_id may only advance once the broker holds this value, otherwise
          // a change decided while MQTT is down is never sent: reconnecting
          // republishes a retained "-" and resets last_right_published, and this
          // block would no longer see a difference to publish.
          if (strcmp(buf, last_right_published) == 0) {
            stable_id = candidate_id;
          } else if (mqtt_client.isConnected() &&
                     mqtt_client.publish(mqtt_topic_cube_right, buf, true)) {
            strncpy(last_right_published, buf, sizeof(last_right_published) - 1);
            last_right_published[sizeof(last_right_published) - 1] = '\0';
            stable_id = candidate_id;
            Serial.printf("Hall neighbor -> %s\n", buf);
          }
        }

        // readHallNeighborId() above fed the tracker this sample, so the
        // accessors describe the reading the id decision was just made on.
        const int presence_delta = hall_presence.delta();
        const bool presence_state = hall_presence.active();

        static int32_t proximity_filter = 0;
        static bool proximity_primed = false;
        if (!proximity_primed) {
          proximity_filter = (int32_t)presence_delta << HALL_PROXIMITY_SHIFT;
          proximity_primed = true;
        } else {
          proximity_filter += presence_delta - (proximity_filter >> HALL_PROXIMITY_SHIFT);
        }
        const int proximity = hallPresenceCloseness(
            (int)(proximity_filter >> HALL_PROXIMITY_SHIFT), HALL_PRESENCE_ON_DELTA);

        static unsigned long last_proximity_publish = 0;
        // The endpoints are exact: 0 and 100 must land even if the last publish was
        // within the deadband, or an animation never fully arrives or clears.
        const bool proximity_changed =
            published_proximity < 0 ||
            ((proximity == 0 || proximity == 100) ? proximity != published_proximity
                                                  : abs(proximity - published_proximity) >=
                                                        HALL_PROXIMITY_MIN_CHANGE);
        if (proximity_changed &&
            current_time - last_proximity_publish >= HALL_PROXIMITY_INTERVAL_MS &&
            mqtt_client.isConnected()) {
          char proximity_buf[8];
          snprintf(proximity_buf, sizeof(proximity_buf), "%d", proximity);
          if (mqtt_client.publish(mqtt_topic_cube_proximity, proximity_buf, true)) {
            last_proximity_publish = current_time;
            published_proximity = proximity;
          }
        }
        saved_presence_baseline = hall_presence.baseline();
        saved_presence_magic = PRESENCE_BASELINE_MAGIC;

        static int nvs_presence_baseline = loadPresenceBaseline();
        static unsigned long last_presence_save_attempt = 0;
        // stable_raw holds its 0xFF sentinel until the ID lines have debounced, and
        // an unknown mask must not read as an undocked one.
        //
        // The cache only advances on a confirmed write, so a failed one is retried
        // rather than assumed: dropping the seed silently costs a cold boot, which
        // is the whole point of storing it. Retries are spaced because this runs at
        // the poll rate and a durably unavailable NVS would otherwise be hammered.
        if (stable_raw != 0xFF &&
            current_time - last_presence_save_attempt >= PRESENCE_BASELINE_SAVE_RETRY_MS &&
            shouldSavePresenceBaseline(stable_raw, presence_state, saved_presence_baseline,
                                       nvs_presence_baseline)) {
          last_presence_save_attempt = current_time;
          if (savePresenceBaseline(saved_presence_baseline)) {
            nvs_presence_baseline = saved_presence_baseline;
          }
        }
        const bool presence_changed =
            !presence_ever_published || presence_state != published_presence_active ||
            abs(presence_delta - published_presence_delta) >= HALL_PRESENCE_PUBLISH_MIN_CHANGE;

        if (presence_changed &&
            current_time - last_presence_publish >= HALL_PRESENCE_PUBLISH_INTERVAL_MS &&
            mqtt_client.isConnected()) {
          char presence_buf[96];
          snprintf(presence_buf, sizeof(presence_buf),
                   "delta=%d on=%d off=%d dist=%d drop=%d base=%d raw=%d active=%d",
                   presence_delta, HALL_PRESENCE_ON_DELTA, HALL_PRESENCE_OFF_DELTA,
                   hallPresenceDistance(presence_delta, HALL_PRESENCE_ON_DELTA),
                   hallPresenceDistance(HALL_PRESENCE_OFF_DELTA, HALL_PRESENCE_ON_DELTA),
                   hall_presence.baseline(), hall_presence.filtered(), presence_state);
          if (mqtt_client.publish(mqtt_topic_cube + "/hall_presence", presence_buf, true)) {
            last_presence_publish = current_time;
            published_presence_delta = presence_delta;
            published_presence_active = presence_state;
            presence_ever_published = true;
          }
        }
      }
    }
  }

  // Track Hall sensor state and log connect/disconnect via MQTT
#ifdef HALL_SENSOR_ENABLED
  if (slotIsResolved()) {
    static unsigned long last_hall_check = 0;
    if (current_time - last_hall_check >= HALL_SENSOR_CHECK_INTERVAL_MS) {
      last_hall_check = current_time;
      bool hall_present = (digitalRead(HALL_SENSOR_PIN) == LOW);

      if (hall_present != last_hall_present) {
        last_hall_present = hall_present;
        const char* status = hall_present ? HALL_SENSOR_STATUS_CONNECTED : HALL_SENSOR_STATUS_DISCONNECTED;
        mqtt_client.publish(mqtt_topic_cube + "/hall_sensor", status, true);
        Serial.printf("Hall sensor %s\n", status);

        // On hall connect, if NFC still remembers a tag from before, force the
        // next gated NFC read to re-announce it rather than skip it as
        // unchanged, so the observation republishes via cube/device/{MAC}/nfc
        // instead of sitting silent while NFC re-acquires. The server
        // resolves the tag now, not this firmware.
        if (hall_present && strcmp(last_neighbor_id, "-") != 0) {
          last_observation_published[0] = '\0';
        }
      }
    }
  }
#endif

#ifdef HALL_SENSOR_ANALOG
  if (slotIsResolved()) {
    static unsigned long last_hall_check = 0;
    static int last_hall_value = -1;
    if (current_time - last_hall_check >= HALL_SENSOR_CHECK_INTERVAL_MS) {
      last_hall_check = current_time;
      int hall_value = analogRead(HALL_SENSOR_PIN);

      if (hall_value != last_hall_value) {
        last_hall_value = hall_value;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", hall_value);
        mqtt_client.publish(mqtt_topic_cube + "/hall_analog", buf, true);
        Serial.printf("Hall analog: %d\n", hall_value);
      }
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
// force rebuild
