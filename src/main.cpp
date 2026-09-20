#include "cube_utilities.h"
#include "hall_presence.h"
#include "sensor_mode.h"
#include "cube_slot_store.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <EspMQTTClient.h>
#include <PN5180ISO15693.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <secrets.h>
#include "font.h"
#include "esp_system.h"
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

// Which neighbour sensor this board carries, fixed when it is flashed. The
// hall board supplies the ID sensors and the presence tap together, so the
// flag that enables presence selects the magnet neighbour path.
#ifdef HALL_SENSOR_ANALOG
static constexpr SensorMode sensor_mode = SENSOR_MODE_MAGNETS;
#else
static constexpr SensorMode sensor_mode = SENSOR_MODE_NFC;
#endif

static bool sensorModeIsMagnets() { return sensor_mode == SENSOR_MODE_MAGNETS; }

// Function to configure pins based on board type (compile-time)
void configurePins() {
#ifdef BOARD_V6
  miso_pin = 34;
  pn5180_busy_pin = 35;
  Serial.printf("38-pin board - MISO=%d, PN5180_BUSY=%d\n", miso_pin, pn5180_busy_pin);
#else
  miso_pin = 39;
  pn5180_busy_pin = 36;
  Serial.printf("socket board - MISO=%d, PN5180_BUSY=%d\n", miso_pin, pn5180_busy_pin);
#endif

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


#define BORDER_LINE_COUNT 4

// Pin Definitions
#define PN5180_NSS 32
#define PN5180_RST 17

// Display Settings
#define BIG_COL 10
#define BIG_TEXT_SIZE 1
#define BRIGHTNESS 255
#define HIGHLIGHT_TIME_MS 2000
#define PRINT_DEBUG true

// Timing Constants
#define ANIMATION_DURATION_MS 1000
#define ANIMATION_SCALE 100
#define BORDER_ANIMATION_DURATION_MS 1000
// MQTT can dispatch a small batch of topology updates before the display draws
// its next frame. Let the final update in that frame replace the target while
// preserving the original border as the animation's start state.
#define BORDER_TARGET_REPLACE_WINDOW_MS 16
#define DISPLAY_STARTUP_DELAY_MS 600

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

// 2-of-6 Hall-sensor neighbor ID decode, an alternative to the PN5180 NFC
// neighbor path. See cubes/docs/hall_sensor_replacement_design.md.
// Six ID sensor GPIOs, reusing the PN5180 connector pins per the design's pin
// table. Order is P1..P6, mapping to id_mask bits 0..5.
static const uint8_t HALL_ID_PINS[6] = {32, 17, 23, 18, 34, 35};
#define HALL_PRESENCE_PIN 36        // existing v6 hall tap (GPIO36, input-only)
// DRV5055 analog presence sensor. Thresholds are deltas from a tracked baseline, not
// absolute ADC values; see hall_presence.h.
#define HALL_PRESENCE_DIRECTION        1    // +1: presence magnet drives the reading up
// Measured across slots 11-16 on 2026-09-18, docked and separated, with the
// baselines primed from a clean reading:
//   docked deflection   94 .. 240   on a cold docking of the whole row; a single
//                                   badly seated pass read 11->12 down at 63, so
//                                   seating alone moves a pair by ~40
//   idle excursion      up to 11    (worst slot 15; was up to 34 at fast_shift 3)
//
// The gap between the two thresholds is what stops a cube parked at the edge of
// the zone from flipping in and out -- with NFC that chatter made cubes flash and
// play sounds with no cause a player could see, and hysteresis is the whole
// reason there is a presence sensor in front of the ID sensors at all.
//
// A stationary cube chatters when noise can carry it above ON and later below
// OFF, which needs ON - OFF < 2 * excursion. At 11 counts of excursion, the
// 55-count band is comfortably stable. A partial 15/16 alignment reaches about
// 45 counts, while the weakest seated pair measured 98, leaving the threshold
// clear of both states.
#define HALL_PRESENCE_ON_DELTA         70
#define HALL_PRESENCE_OFF_DELTA        15
#define HALL_PRESENCE_FAST_SHIFT       5
#define HALL_PRESENCE_BASE_SHIFT       7
#define HALL_PRESENCE_BASE_INTERVAL_MS 250  // baseline tau ~32s


// The 0..100 closeness of the neighbour, driving the presence bar and the
// candidate border preview. Smoothed harder than the detection path, which is
// deliberately quick so a docking cube latches promptly: at the ~1kHz poll a
// shift of 7 is roughly 128ms, slow enough that the +/-13 counts of ADC noise
// measured on slot 1 do not show as jitter.
#define HALL_PROXIMITY_SHIFT           7
// GH1230KSW ID sensors are open-drain with 10k pull-ups on the PCB: lines
// idle HIGH and a magnet pulls them LOW: an undocked cube reads hall_mask
// 00 in the diag response, so HIGH is the no-magnet level.
#define HALL_ID_ACTIVE_LEVEL LOW
#define HALL_POLL_INTERVAL_MS 1     // ~1 kHz polling; each digitalRead is ~us
#define HALL_DEBOUNCE_READS 8       // consecutive identical reads to confirm (~8 ms)

// Sleep state management
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

// NFC State

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
static String cube_identifier;
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
// A tag right at the edge of NFC range flickers rapidly; this holds a
// repeatedly-flipping reconnect back for confirmation. See
// cube_utilities.h's NFC_CHATTER_* comment for the design.
static NfcChatterState nfc_chatter_state;
static String mqtt_topic_presence;
static String mqtt_topic_liveness_response;
static const unsigned long ASSIGNMENT_WAIT_MS = 3000;
static RgbOrder current_rgb_order = RGB_ORDER_BGR;
static bool wifi_connection_attempt_active = false;
static unsigned long wifi_connection_attempt_started = 0;
static unsigned long next_wifi_connection_attempt = 0;

// Animation
char last_neighbor_id[NFCID_LENGTH * 2 + 1] = "INIT";  // last raw NFC value read
char last_right_published[8] = "INIT";                  // last value published to /right

// Pre-allocated MQTT topics
String mqtt_topic_cube;
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
unsigned long letter_interval_accum = 0;
int letter_interval_count = 0;
unsigned long max_letter_interval = 0;
unsigned long nfc_read_max_us = 0;
int nfc_reset_count = 0;


// ============= DisplayManager Class =============
class DisplayManager {
private:
  MatrixPanel_I2S_DMA* led_display;
  uint8_t debug_line;
  unsigned long animation_start_time;
  long highlight_end_time;
  bool is_lock;
  uint8_t percent_complete;
  uint16_t current_letter_color;
  uint16_t vline_color_right;
  uint16_t vline_color_left;
  uint16_t hline_color_top;
  uint8_t presence_bar_height;
  unsigned long last_presence_bar_ms;
  uint16_t hline_color_bottom;
  uint16_t border_from_top, border_from_bottom, border_from_left, border_from_right;
  uint16_t pending_border_top, pending_border_bottom, pending_border_left, pending_border_right;
  unsigned long border_animation_start_time;
  bool border_animation_active, border_target_pending;
  char border_preview_side;
  unsigned long border_preview_start_time;
  unsigned long border_preview_until;
  //: How long the sink spends TRAVELLING, in ms. Below this the glyphs move;
  //: above it they have arrived and the settle tail rebounds three times.
  //:
  //: SET BY THE SERVER over `<cube>/rise_ms`, because the number depends on
  //: things this firmware cannot see: when the tile actually changes hands,
  //: and whether a remote match held the announcement for its revocation gate.
  //: The server solves it so the two glyphs trade dominance on exactly the
  //: millisecond the tile becomes the player's -- the same instant the screen
  //: hands over -- and sends the answer rather than the inputs.
  //:
  //: DEFAULTS TO THE STOCK 4/11, so a cube that is never told still animates
  //: correctly, just on the library curve it used before.
  uint16_t rise_ms;
  const GFXfont* current_font;
  uint8_t text_size;
  uint8_t rotation;
  bool is_dirty;
  char previous_letter;
  char current_letter;

  // `Ease::BounceOut`, re-proportioned so the travel segment lasts `rise_ms`
  // instead of the library's fixed 4/11 of the duration. Mirrors
  // `bounce_out` in cubes/src/game/landing_transition.py -- the two must agree
  // or the cube and the screen stop meaning the same thing.
  //
  // TWO PARTS. Below `rise` the glyphs travel, on the parabola (t/rise)^2;
  // above it they have arrived and the settle tail rebounds. The tail is
  // REMAPPED rather than rewritten: `u` carries [rise, 1] back onto the stock
  // [1/2.75, 1], so the three rebounds keep their shape and their relative
  // depths and merely occupy whatever time is left. Continuous at the seam by
  // construction -- at t == rise, u == 1/2.75, where the stock curve is 1.0.
  uint8_t bounceOut(unsigned long elapsed) const {
    if (elapsed >= ANIMATION_DURATION_MS) {
      return ANIMATION_SCALE;
    }
    const float n = 7.5625f, d = 2.75f;
    float t = (float)elapsed / (float)ANIMATION_DURATION_MS;
    float rise = (float)rise_ms / (float)ANIMATION_DURATION_MS;
    if (rise < 0.000001f) rise = 0.000001f;
    if (rise > 1.0f) rise = 1.0f;

    float v;
    if (t < rise) {
      float u = t / rise;
      v = u * u;
    } else {
      float u = 1.0f / d + (t - rise) / (1.0f - rise) * (1.0f - 1.0f / d);
      if (u < 2.0f / d) {
        u -= 1.5f / d;
        v = n * u * u + 0.75f;
      } else if (u < 2.5f / d) {
        u -= 2.25f / d;
        v = n * u * u + 0.9375f;
      } else {
        u -= 2.625f / d;
        v = n * u * u + 0.984375f;
      }
    }
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (uint8_t)(v * ANIMATION_SCALE + 0.5f);
  }

public:
  DisplayManager() : is_dirty(true),
                                debug_line(0),
                                animation_start_time(0), highlight_end_time(0), percent_complete(100),
                                current_letter_color(LETTER_COLOR), current_font(&Roboto_Mono_Bold_78),
                                text_size(1), is_lock(false),
                                vline_color_left(0), vline_color_right(0),
                                hline_color_top(0), presence_bar_height(0), last_presence_bar_ms(0),
                                hline_color_bottom(0),
                                border_from_top(0), border_from_bottom(0),
                                border_from_left(0), border_from_right(0),
                                pending_border_top(0), pending_border_bottom(0),
                                pending_border_left(0), pending_border_right(0),
                                border_animation_start_time(0), border_animation_active(false),
                                border_target_pending(false),
                                border_preview_side(0), border_preview_start_time(0),
                                border_preview_until(0),
                                previous_letter(' '), current_letter(' ') {
    // Player 0's panels are mounted upside down relative to player 1's. The
    // slot is not known yet at construction, so start where a player 0 cube
    // needs to be; applySlot() calls setSlotRotation() once the roster answers.
    rotation = 2;
    setupDisplay();
    rise_ms = (uint16_t)(ANIMATION_DURATION_MS * 4.0f / 11.0f);
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

    if (previous_letter != current_letter) {
      static uint8_t previous_percent_complete = -1;
      if (current_time - animation_start_time >= ANIMATION_DURATION_MS) {
        // complete animation
        previous_letter = current_letter;
        percent_complete = ANIMATION_SCALE;
        is_dirty = true;
      } 
      else {
        // animation in progress
        percent_complete = bounceOut(current_time - animation_start_time);
        if (percent_complete != previous_percent_complete) {
            previous_percent_complete = percent_complete;
            is_dirty = true;
        }
      }
    }
    if (border_animation_active) {
      if (current_time - border_animation_start_time >= BORDER_ANIMATION_DURATION_MS) {
        if (border_target_pending) {
          border_from_top = hline_color_top;
          border_from_bottom = hline_color_bottom;
          border_from_left = vline_color_left;
          border_from_right = vline_color_right;
          hline_color_top = pending_border_top;
          hline_color_bottom = pending_border_bottom;
          vline_color_left = pending_border_left;
          vline_color_right = pending_border_right;
          border_target_pending = false;
          border_animation_start_time = current_time;
          is_dirty = true;
        } else {
          border_animation_active = false;
        }
      } else {
        is_dirty = true;  // display-only redraw while the border interpolates
      }
    }
    if (border_preview_side) is_dirty = true;
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
    if (border_animation_active) {
      const float t = (float)(millis() - border_animation_start_time) / BORDER_ANIMATION_DURATION_MS;
      const float inv = 1.0f - t;
      const float p = 1.0f - inv * inv * inv * inv * inv;  // easeOutQuint
      drawAnimatedBorder(p);
      return;
    }
    drawBorders(true, true, hline_color_top);
    drawBorders(true, false, hline_color_bottom);
    drawBorders(false, true, vline_color_left);
    drawBorders(false, false, vline_color_right);
  }

  void setConsolidatedBorderTarget(uint16_t top, uint16_t bottom,
                                   uint16_t left, uint16_t right) {
    const unsigned long now = millis();
    const bool matches_current = top == hline_color_top &&
                                 bottom == hline_color_bottom &&
                                 left == vline_color_left &&
                                 right == vline_color_right;
    const bool matches_pending = top == pending_border_top &&
                                 bottom == pending_border_bottom &&
                                 left == pending_border_left &&
                                 right == pending_border_right;
    // MQTT retained deliveries and idle border refreshes can repeat a target.
    // A repeated target must not start another 600 ms redraw or replace an
    // animation that is already headed to that exact frame.
    if (matches_current ||
        (border_animation_active && border_target_pending && matches_pending)) {
      return;
    }
    if (border_animation_active) {
      if (now - border_animation_start_time <= BORDER_TARGET_REPLACE_WINDOW_MS) {
        hline_color_top = top;
        hline_color_bottom = bottom;
        vline_color_left = left;
        vline_color_right = right;
      } else {
        pending_border_top = top;
        pending_border_bottom = bottom;
        pending_border_left = left;
        pending_border_right = right;
        border_target_pending = true;
      }
      return;
    }
    border_from_top = hline_color_top;
    border_from_bottom = hline_color_bottom;
    border_from_left = vline_color_left;
    border_from_right = vline_color_right;
    hline_color_top = top;
    hline_color_bottom = bottom;
    vline_color_left = left;
    vline_color_right = right;
    border_animation_start_time = now;
    border_animation_active = true;
  }

  static bool isMiddleBorder(uint16_t top, uint16_t bottom, uint16_t left, uint16_t right) {
    return top && bottom && !left && !right;
  }
  static bool isEndBorder(uint16_t top, uint16_t bottom, uint16_t left, uint16_t right) {
    return top && bottom && (left != 0 || right != 0) && !(left != 0 && right != 0);
  }
  static bool isEmptyBorder(uint16_t top, uint16_t bottom, uint16_t left, uint16_t right) {
    return !top && !bottom && !left && !right;
  }
  void drawHorizontalFromMiddle(float p, uint16_t top, uint16_t bottom) {
    const int n = (int)(32 * p + .5f);
    for (uint8_t line = 0; line < BORDER_LINE_COUNT / 2; ++line) {
      if (top) { led_display->drawFastHLine(32 - n, line, n, top); led_display->drawFastHLine(32, line, n, top); }
      if (bottom) { const int y = PANEL_RES_Y - BORDER_LINE_COUNT / 2 + line; led_display->drawFastHLine(32 - n, y, n, bottom); led_display->drawFastHLine(32, y, n, bottom); }
    }
  }
  void drawEndPath(bool left, float p, uint16_t top, uint16_t bottom, uint16_t side) {
    const int travel = (int)(96 * p + .5f), horizontal = min(64, travel), vertical = max(0, travel - 64);
    for (uint8_t line = 0; line < BORDER_LINE_COUNT / 2; ++line) {
      const int ty = line, by = PANEL_RES_Y - BORDER_LINE_COUNT / 2 + line;
      const int sx = left ? 64 - horizontal : 0;
      if (top) led_display->drawFastHLine(sx, ty, horizontal, top);
      if (bottom) led_display->drawFastHLine(sx, by, horizontal, bottom);
      if (side && vertical) {
        const int x = left ? line : PANEL_RES_X - BORDER_LINE_COUNT / 2 + line;
        led_display->drawFastVLine(x, 0, vertical, side);
        led_display->drawFastVLine(x, PANEL_RES_Y - vertical, vertical, side);
      }
    }
  }
  void drawPreviewSideErasing(bool left, float p, uint16_t side) {
    // p=0 is a complete shared edge. As p grows, its two halves withdraw
    // from the centre to their endpoints; the following preview cycle snaps
    // straight back to the complete edge.
    const int half = (int)(PANEL_RES_Y / 2.0f * (1.0f - p) + .5f);
    for (uint8_t line = 0; line < BORDER_LINE_COUNT / 2; ++line) {
      const int x = left ? line : PANEL_RES_X - BORDER_LINE_COUNT / 2 + line;
      if (half) {
        led_display->drawFastVLine(x, 0, half, side);
        led_display->drawFastVLine(x, PANEL_RES_Y - half, half, side);
      }
      // A settled border can already occupy this edge. Mask its middle before
      // drawing the remaining halves so the withdrawal is visible in either
      // topology.
      const int gap = PANEL_RES_Y - 2 * half;
      if (gap) led_display->drawFastVLine(x, half, gap, BLACK);
    }
  }
  void drawAnimatedBorder(float p) {
    const bool from_empty = isEmptyBorder(border_from_top, border_from_bottom, border_from_left, border_from_right);
    const bool to_empty = isEmptyBorder(hline_color_top, hline_color_bottom, vline_color_left, vline_color_right);
    const bool from_middle = isMiddleBorder(border_from_top, border_from_bottom, border_from_left, border_from_right);
    const bool to_middle = isMiddleBorder(hline_color_top, hline_color_bottom, vline_color_left, vline_color_right);
    const bool from_end = isEndBorder(border_from_top, border_from_bottom, border_from_left, border_from_right);
    const bool to_end = isEndBorder(hline_color_top, hline_color_bottom, vline_color_left, vline_color_right);
    if (from_empty && to_middle) { drawHorizontalFromMiddle(p, hline_color_top, hline_color_bottom); return; }
    if (from_middle && to_empty) { drawHorizontalFromMiddle(1.0f - p, border_from_top, border_from_bottom); return; }
    if (from_empty && to_end) { const bool left = vline_color_left != 0; drawEndPath(left, p, hline_color_top, hline_color_bottom, left ? vline_color_left : vline_color_right); return; }
    if (from_end && to_empty) { const bool left = border_from_left != 0; drawEndPath(left, 1.0f - p, border_from_top, border_from_bottom, left ? border_from_left : border_from_right); return; }
    if (from_middle && to_end) { drawBorders(true, true, hline_color_top); drawBorders(true, false, hline_color_bottom); const bool left = vline_color_left != 0; drawEndPath(left, (64.0f + 32.0f * p) / 96.0f, 0, 0, left ? vline_color_left : vline_color_right); return; }
    if (from_end && to_middle) { drawBorders(true, true, border_from_top); drawBorders(true, false, border_from_bottom); const bool left = border_from_left != 0; drawEndPath(left, (64.0f + 32.0f * (1.0f - p)) / 96.0f, 0, 0, left ? border_from_left : border_from_right); return; }
    drawBorderFrameStatic();
  }
  void drawBorderFrameStatic() { drawBorders(true, true, hline_color_top); drawBorders(true, false, hline_color_bottom); drawBorders(false, true, vline_color_left); drawBorders(false, false, vline_color_right); }

  // Debug aid for the hall presence sensor: a green bar up the left edge whose
  // height is the neighbour's closeness, empty at 0 and full height when
  // seated. Nothing sets it on an NFC build, so it stays empty there.
  // Full panel height: the bar is read by eye, and 16 steps across 64px is twice
  // the resolution of 8 across 32.
  static const uint8_t PRESENCE_BAR_MAX = PANEL_RES_Y;
  // There is no way to repaint one edge on its own: the frame lives in a DMA
  // buffer that gets swapped whole, so any change to the bar costs a full redraw
  // of the letter and borders as well. A raw proximity value wanders constantly,
  // which turned a debug aid into a 30 FPS full-frame redraw. Coarse steps and a
  // floor on how often it may change cut that to a handful of redraws per
  // second, which is all a human can read off the bar anyway.
  static const uint8_t PRESENCE_BAR_STEP = 4;
  static const unsigned long PRESENCE_BAR_MIN_INTERVAL_MS = 250;

  void setPresencePercent(int percent, unsigned long now) {
    uint8_t height = percent <= 0    ? 0
                   : percent >= 100  ? PRESENCE_BAR_MAX
                   : (uint8_t)((percent * PRESENCE_BAR_MAX) / 100);
    height -= height % PRESENCE_BAR_STEP;
    if (height == presence_bar_height) {
      return;
    }
    // Empty and full are the two the eye is actually waiting for, so they land
    // immediately; everything between is a rate-limited approximation.
    const bool endpoint = (height == 0 || height == PRESENCE_BAR_MAX);
    if (!endpoint && now - last_presence_bar_ms < PRESENCE_BAR_MIN_INTERVAL_MS) {
      return;
    }
    last_presence_bar_ms = now;
    presence_bar_height = height;
    is_dirty = true;
  }

  void drawPresenceBar() {
    if (presence_bar_height == 0) {
      return;
    }
    led_display->drawFastVLine(0, PANEL_RES_Y - presence_bar_height,
                               presence_bar_height, GREEN);
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
        led_display->drawFastVLine(pos, 0, PANEL_RES_Y, color);
      }
    }
  }

  void handleFlashCommand(const String& message) {
    if (message.length() <= 0) {
      return;
    }
    debugPrintln("flashing due to /flash");
    highlight_end_time = millis() + HIGHLIGHT_TIME_MS;
    is_dirty = true;
  }

  void handleRiseMsCommand(const String& message) {
    if (message.length() <= 0) {
      return;
    }
    long value = message.toInt();
    // Clamped rather than rejected: a travel segment longer than the whole
    // animation degrades to "travels the entire time and never rebounds",
    // which is a worse-looking landing but still a correct one.
    if (value < 1) value = 1;
    if (value > ANIMATION_DURATION_MS) value = ANIMATION_DURATION_MS;
    rise_ms = (uint16_t)value;
    Serial.printf("[%lu] landing rise set to %u ms\n", millis(), rise_ms);
    // NOT is_dirty: this changes the shape of the NEXT landing, not anything
    // on screen right now. Marking dirty here would redraw the current frame
    // at a phase the running animation was not computed against.
  }

  void handleLockCommand(const String& message) {
    debugPrintln("locking due to /lock");
    is_lock = message.length() > 0 && message.charAt(0) == '1';
    Serial.println(is_lock);
    Serial.println(message);
    is_dirty = true;
  }

  void updateDisplay(unsigned long current_time) {
    if (!is_dirty) {
      return;
    }

    led_display->setFont(current_font);
    led_display->setTextSize(text_size);
    led_display->setRotation(rotation);

    if (current_letter != previous_letter) {
      drawLetter(100 + percent_complete, previous_letter, RED);
    }
    drawLetter(percent_complete, current_letter, current_letter_color);

    // Draw orientation indicator only when letter animation is complete
    if (percent_complete >= 100) {
      drawOrientationIndicator();
    }

    drawBorderFrame();
    drawBorderPreview(current_time);
    drawPresenceBar();
    led_display->flipDMABuffer();
    led_display->clearScreen();
    is_dirty = false;
  }

  void handleBorderPreviewCommand(const String& message) {
    // Protocol: E or W identifies the prospective shared edge.
    const char side = message.length() ? message.charAt(0) : 0;
    border_preview_side = (side == 'E' || side == 'W') ? side : 0;
    border_preview_start_time = millis();
    border_preview_until = border_preview_side ? border_preview_start_time + 1500 : 0;
    is_dirty = true;
  }

  void drawBorderPreview(unsigned long now) {
    if (!border_preview_side) return;
    if (now >= border_preview_until) { border_preview_side = 0; return; }
    const float t = (float)((now - border_preview_start_time) % BORDER_ANIMATION_DURATION_MS) / BORDER_ANIMATION_DURATION_MS;
    const float inv = 1.0f - t;
    const float p = 1.0f - inv * inv * inv * inv * inv;
    const bool left = border_preview_side == 'W';
    // Any settled vertical edge makes this cube part of a series rather than a
    // free endpoint. It sheds the prospective edge while a free endpoint grows
    // the complete three-sided end shape.
    const bool has_confirmed_edge = vline_color_left != 0 || vline_color_right != 0;
    if (has_confirmed_edge) {
      drawPreviewSideErasing(left, p, WHITE);
    } else {
      drawEndPath(left, p, WHITE, WHITE, WHITE);
    }
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

  void handleConsolidatedBorderCommand(const String& message) {
    // Protocol: "NSW:0xFF0000" or "N:0x00FF00" or ":0xFF0000" for all sides
    // Unmentioned sides are automatically cleared
    
    debugPrint("Consolidated border: ");
    debugPrintln(message.c_str());
    
    // Parse the message: directions:color
    int colonIndex = message.indexOf(':');
    if (colonIndex == -1) return; // Invalid format

    uint16_t top = 0, bottom = 0, left = 0, right = 0;
    
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
          top = color;
          break;
        case 'S': // South = bottom  
          bottom = color;
          break;
        case 'E': // East = right
          right = color;
          break;
        case 'W': // West = left
          left = color;
          break;
      }
    }
    
    setConsolidatedBorderTarget(top, bottom, left, right);
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

    last_message_time = current_time;

    Serial.printf("[%lu] MQTT letter '%s' delta=%lu ms\n", current_time, message.c_str(), time_since_last);
    
    if (previous_letter != current_letter) {
      previous_letter = current_letter;
    }
      
    if (message.length() > 0) {
      current_letter = message.charAt(0);
      animation_start_time = millis();
      current_font = &Roboto_Mono_Bold_78;  // Restore custom font for letter mode
      text_size = 1;  // Always use size 1 for letter mode
      is_dirty = true;
    }
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

// Per-section timing diagnostics.
//
// These are per-ITERATION averages: the section total divided by main loop
// iterations. nfc_us in particular is not a per-read figure -- most iterations
// dequeue no NFC result at all, so it is diluted by an unknown factor.
// Measured against the duty cycle, two cubes spent 38% and 48% of wall-clock
// time inside NFC reads, which is tens of milliseconds per read, while the
// nfc= field reads ~0.12 ms. Anything thresholding on it is comparing against
// a number that does not mean microseconds per read.
struct SectionTiming {
  // 64-bit because `long` is 32 bits here, so a microsecond accumulator wraps
  // after 4295 s of accumulated work in that section, and these reset only on
  // a diag request. nfc_us wraps first: it runs at roughly 40% duty.
  uint64_t mqtt_us;
  uint64_t display_us;
  uint64_t udp_us;
  uint64_t nfc_us;
};
SectionTiming section_timing_accum = {0, 0, 0, 0};
int section_timing_count = 0;

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
  const CubeMacEntry* entry = findCubeEntry(mac_address.c_str());
  if (!entry) {
    Serial.print("FATAL: MAC not in cube table: ");
    Serial.println(mac_address);
    while (true) {
      delay(1000);
    }
  }
  current_rgb_order = entry->rgb_order;

  configurePins();
  Serial.printf("sensor_mode: %s (compiled)\n",
                sensorModeIsMagnets() ? "magnets" : "nfc");
  initialiseNeighbourSensor();

  Serial.print("mac_address: ");
  Serial.println(mac_address);
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
    device_topic_ = "cube/device/" + mac_nocolons + "/auto_sleep";
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

  // An empty retained topic delivers nothing at all, so elapsed time cannot
  // tell "no flag is set" from "the flag has not arrived yet". The keep-alive
  // publish doubles as a round-trip marker: it is sent after the flag
  // subscriptions, and the broker queues each subscription's retained message
  // as it processes that SUBSCRIBE, so on one TCP connection the marker coming
  // back proves any retained flag was already delivered. Returns false when it
  // does not come back -- the caller must not read silence as "no flag".
  bool readSleepFlag(bool* out) override {
    sleep_requested_ = false;
    marker_seen_ = false;

    // IMPORTANT: Read payload BEFORE any publish() calls — PubSubClient reuses
    // its internal buffer for both incoming and outgoing messages, so
    // publishing inside the callback overwrites the payload bytes.
    mqtt_.setCallback([this](char* topic, byte* payload, unsigned int length) {
      bool requested = length == 1 && payload[0] == '1';
      if (device_topic_ == topic) {
        sleep_requested_ = requested;
      } else if (status_topic_ == topic) {
        marker_seen_ = true;
      }
    });

    mqtt_.subscribe(status_topic_.c_str());
    mqtt_.subscribe(device_topic_.c_str());
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
    snprintf(dbg, sizeof(dbg), "sleep flag=%d confirmed=%d",
             sleep_requested_, marker_seen_);
    debugSend(dbg);
    *out = sleep_requested_;
    return marker_seen_;
  }

  void clearSleepFlag() override {
    mqtt_.publish(device_topic_.c_str(), "", true);
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
  String status_topic_;
  bool marker_seen_ = false;
  bool sleep_requested_ = false;
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


void subscribeSlotTopics() {
  mqtt_topic_cube = MQTT_TOPIC_PREFIX_CUBE + cube_identifier;
  mqtt_topic_echo = createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_ECHO);
  mqtt_topic_cube_right = String(MQTT_TOPIC_PREFIX_CUBE) + String("right/") + cube_identifier;

  // Only publish version on first boot, not on wake from sleep
  if (is_first_boot) {
    mqtt_client.publish(createMqttTopic(cube_identifier, MQTT_TOPIC_PREFIX_VERSION), GIT_VERSION, true);  // retained
  }

  // A cube waking from sleep still needs to report its mode, so this sits
  // outside the is_first_boot guard above.
  mqtt_client.publish(mqtt_topic_cube + "/sensor_mode",
                      sensorModeIsMagnets() ? "magnets" : "nfc", true);

  // cube/device/{MAC}/nfc is retained, so a tag read before a cable swap
  // outlives the swap. The game server resolves neighbours from that topic, so
  // the record would be applied as a live neighbour for a cube that no longer
  // has a reader. An empty payload is how a cleared observation is already
  // expressed, so publishing one retires the record.
  if (sensorModeIsMagnets() && !mqtt_topic_device_nfc.isEmpty()) {
    mqtt_client.publish(mqtt_topic_device_nfc, "", true);
  }

  auto resetActivityTimer = []() { last_activity_time = millis(); };

  mqtt_client.subscribe(mqtt_topic_cube + "/sleep_interval", handleSleepIntervalCommand);
  mqtt_client.subscribe(mqtt_topic_cube + "/border", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleConsolidatedBorderCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/border_preview", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleBorderPreviewCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/flash", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleFlashCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/letter", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleLetterCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/lock", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleLockCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/ping", [resetActivityTimer](const String& msg) { resetActivityTimer(); handlePingCommand(msg); });
#ifdef BOARD_V6
  mqtt_client.subscribe(mqtt_topic_cube + "/power_test", [resetActivityTimer](const String& msg) { resetActivityTimer(); handlePowerTestCommand(msg); });
#endif
  mqtt_client.subscribe(mqtt_topic_cube + "/reset", [resetActivityTimer](const String& msg) { resetActivityTimer(); handleResetCommand(msg); });
  mqtt_client.subscribe(mqtt_topic_cube + "/rise_ms", [resetActivityTimer](const String& msg) { resetActivityTimer(); display_manager->handleRiseMsCommand(msg); });

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
      result, assignment.slot, authority_latched, -1);

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

// Defined with the hall code below, which needs the tracker and its config.
static void recalibratePresence();

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
  mqtt_client.subscribe("cube/device/" + mac_nocolons + "/recalibrate",
                        [](const String&) { recalibratePresence(); });
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
// ============= Hall Neighbor Functions =============
// Maps a 6-bit ID mask (bits P6 P5 P4 P3 P2 P1) to a neighbor cube id;
// 0 = invalid pattern. Player 0 is cubes 1-6, player 1 is cubes 11-16, and
// each cube's ID magnets carry the pattern that decodes to its game id.
static uint8_t hallCubeIdForMask(uint8_t id_mask) {
  switch (id_mask & 0x3F) {
    // Player 1, measured with the cubes ordered A through F: each mask is what
    // the cube immediately to its left reports as hall_mask in its diag response.
    case 0b110000: return 11;  // P5+P6
    case 0b100010: return 12;  // P2+P6
    case 0b001100: return 13;  // P3+P4
    case 0b101000: return 14;  // P4+P6
    case 0b000101: return 15;  // P1+P3
    case 0b100100: return 16;  // P3+P6
    // Player 0 has no magnets fitted -- those boards still run NFC -- so these are
    // a free choice, taken from what player 1 left and preferring the pins that
    // are not GPIO 34/35. Fit magnets to match, or renumber these to match the
    // magnets, whichever comes first. P1+P2 is free after the Player 1 mapping,
    // so it avoids overlapping a physical Player 1 ID.
    case 0b000110: return 1;   // P2+P3
    case 0b001010: return 2;   // P2+P4
    case 0b010100: return 3;   // P3+P5
    case 0b011000: return 4;   // P4+P5
    case 0b100001: return 5;   // P1+P6
    case 0b000011: return 6;   // P1+P2
    default:       return 0;
  }
}

static HallPresenceTracker hall_presence;
// Last values the poll saw, for the diag response. A neighbour that is present
// but unreported can mean a missing magnet, a sensor reading nothing, or a
// baseline that never primed, and those need different fixes.
static uint8_t last_hall_id_mask = 0;
static int last_hall_presence_raw = 0;

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

// The write side of restoredPresenceBaseline(), called once per poll.
//
// RTC every time, because it costs a word and covers every reset. NVS almost
// never, because it is only the cold-boot seed and each write erases a sector.
//
// stable_mask is the debounced ID mask; 0xFF is its "not debounced yet"
// sentinel, and an unknown mask must not be read as an undocked one.
// shouldSavePresenceBaseline() decides the rest -- a seed is only worth keeping
// when nothing magnetic is in range.
static void storePresenceBaseline(uint8_t stable_mask, bool active,
                                  unsigned long now) {
  if (hall_presence.primed()) {
    saved_presence_baseline = hall_presence.baseline();
    saved_presence_magic = PRESENCE_BASELINE_MAGIC;
  } else {
    saved_presence_magic = 0;
  }

  // The cache only advances on a confirmed write, so a failed one is retried
  // rather than assumed: dropping the seed silently costs a cold boot, which is
  // the whole point of storing it. Retries are spaced because this runs at the
  // poll rate, and a durably unavailable NVS would otherwise be hammered.
  static int nvs_baseline = loadPresenceBaseline();
  static unsigned long last_attempt = 0;
  if (stable_mask == 0xFF) return;
  if (now - last_attempt < PRESENCE_BASELINE_SAVE_RETRY_MS) return;
  if (!shouldSavePresenceBaseline(stable_mask, active, saved_presence_baseline,
                                  nvs_baseline)) {
    return;
  }
  last_attempt = now;
  if (savePresenceBaseline(saved_presence_baseline)) {
    nvs_baseline = saved_presence_baseline;
  }
}

// Clears every stored reference so the tracker primes again from the current
// reading. A baseline taken while something was in range latches active_ and is
// then frozen by its own activation, and it reaches NVS in the moment between
// priming and latching -- at which point nothing short of this can shift it.
// Publish to cube/device/<mac>/recalibrate with no cube in range.
static void recalibratePresence() {
  saved_presence_magic = 0;
  saved_presence_baseline = 0;
  savePresenceBaseline(0);  // below PRESENCE_BASELINE_MIN, so it reads as absent
  hall_presence.begin({HALL_PRESENCE_DIRECTION,
                       HALL_PRESENCE_ON_DELTA,
                       HALL_PRESENCE_OFF_DELTA,
                       HALL_PRESENCE_FAST_SHIFT,
                       HALL_PRESENCE_BASE_SHIFT,
                       HALL_PRESENCE_BASE_INTERVAL_MS},
                      0);
  Serial.println(F("Presence baseline cleared; repriming"));
}

void setupHallSensors() {
  for (uint8_t i = 0; i < 6; i++) {
    // The GH1230KSW outputs are open-drain and the daughterboard carries a 10k
    // pull-up per sensor, so a line whose pull-up path opens floats -- and it
    // floats LOW, which reads as a magnet that is not there. Slot 16 lost P6 to
    // a bad joint that way and reported a neighbour with nothing beside it.
    //
    // GPIO 34 and above are input-only with no internal pull-up, so P5 and P6
    // cannot be given a fallback in firmware; the rest can, and then an open
    // pull-up path reads HIGH, which is "no magnet". Only a resistor on the main
    // board side of the connector can do the same for P5 and P6.
    const bool has_internal_pullup = HALL_ID_PINS[i] < 34;
    pinMode(HALL_ID_PINS[i], has_internal_pullup ? INPUT_PULLUP : INPUT);
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

// One sample of the six ID lines, LSB = P1.
uint8_t readHallIdMask() {
  uint8_t id_mask = 0;
  for (uint8_t i = 0; i < 6; i++) {
    if (digitalRead(HALL_ID_PINS[i]) == HALL_ID_ACTIVE_LEVEL) {
      id_mask |= (1 << i);
    }
  }
  return id_mask;
}

// Returns the neighbor's cube id, or 0 for no/invalid neighbor.
//
// Takes the mask rather than sampling it, so the id decision and the debounced
// mask the caller publishes describe the same instant. The tracker is fed on
// every call, including the ones where presence has not tripped -- those are
// the calls that keep its baseline from drifting onto a magnet.
uint8_t decodeHallNeighborId(uint8_t id_mask) {
  const int presence_raw = analogRead(HALL_PRESENCE_PIN);
  last_hall_id_mask = id_mask;
  last_hall_presence_raw = presence_raw;

  if (!hall_presence.update(presence_raw, millis(), id_mask)) {
    return 0;  // presence magnet absent -> no neighbor seated
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
      // Check if message is "timing" - return slot:avg_loop_time_us
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
        // snprintf truncates silently rather than telling you, so this is
        // sized for the worst case rather than the typical ~180 chars. Worst
        // case with every numeric field at its type maximum -- including three
        // 64-bit microsecond totals at 20 digits each -- is 505 bytes with the
        // NUL. 512 would fit with 7 bytes spare, which is not margin.
        char diagStr[640];
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
          "%s|fw=%s|mac=%s|loop=%lu|mqtt=%lu|disp=%lu|udp=%lu|nfc=%lu|nfc_max=%lu|nfc_resets=%d|letter_avg=%lu|letter_max=%lu|letter_n=%d|rssi=%d|samples=%d|uptime_ms=%lu"
          "|hall_mask=%02X|hall_raw=%d|hall_filt=%d|hall_base=%d"
          "|hall_delta=%d|hall_on=%d|hall_active=%d|hall_primed=%d",
          cube_identifier.c_str(), fw_board, WiFi.macAddress().c_str(), avg_total, avg_mqtt, avg_display, avg_udp, avg_nfc,
          nfc_read_max_us, nfc_reset_count, avg_letter_interval, max_letter_interval, letter_interval_count,
          WiFi.RSSI(), section_timing_count, millis(),
          last_hall_id_mask, last_hall_presence_raw, hall_presence.filtered(),
          hall_presence.baseline(), hall_presence.delta(),
          HALL_PRESENCE_ON_DELTA, hall_presence.active(),
          hall_presence.primed());

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)diagStr, strlen(diagStr));
        udp.endPacket();

        // Reset auto-sleep timer - active diagnostics should keep cube awake
        last_activity_time = millis();

        // Reset accumulators after reading
        section_timing_accum = {0, 0, 0, 0};
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
      // Check if message is "temp" - return slot:temperature_celsius
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
  // PubSubClient's 256-byte default covers the whole packet, not just the
  // payload, and the largest publish here is the liveness response: a 224-byte
  // buffer on a ~42-character topic, which reaches ~270 bytes with the fixed
  // header. 512 clears that with room for a longer nonce.
  mqtt_client.setMaxPacketSize(512);
  Serial.printf("memory available: %d\n", ESP.getFreeHeap());
  mqtt_client.enableDebuggingMessages(false);
  mqtt_client.setMqttConnectionTimeout(MQTT_CONNECTION_TIMEOUT_MS);
  mqtt_client.setMqttReconnectionAttemptDelay(MQTT_RECONNECT_DELAY_MS);
  mqtt_client.enableOTA();
  
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  Serial.printf("Model: %d, Cores: %d, Revision: %d\n", chip_info.model, chip_info.cores, chip_info.revision);

  
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

  // The presence tap is read with analogRead(), so the ADC needs configuring
  // before setupHallSensors() takes its first sample. That function sets the
  // pin mode itself.
#ifdef HALL_SENSOR_ANALOG
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
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


  // A power cycle is someone picking the cube up, and it gets an answer before
  // the check-in below can send it back to sleep. Without this a cold boot that
  // finds a retained auto_sleep flag never reaches any display code, so a
  // working cube is indistinguishable from dead hardware — which is exactly how
  // a stale auto_sleep flag once read as bad firmware.
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
    display_manager = new DisplayManager();
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
    display_manager = new DisplayManager();
    display_manager->clearDebugDisplay();
    display_manager->displayDebugMessage(GIT_TIMESTAMP);
  }
  delay(DISPLAY_STARTUP_DELAY_MS);
  display_manager->displayDebugMessage((String("wake:") + String(wakeup_reason)).c_str());
  static String client_name = makeMqttClientId(WiFi.macAddress(), "");
  Serial.println(client_name);
  mqtt_client.setMqttClientName(client_name.c_str());
  // loadStoredSlot() ran above. Nothing here waits on the roster, so an
  // authoritative assignment arriving later can still move the slot out from
  // under this line.
  char ipDisplay[64];
  formatBootIdentity(ipDisplay, sizeof(ipDisplay), stored.slot,
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

  serviceWiFiConnection();

  unsigned long section_start = micros();
  mqtt_client.loop();
  unsigned long mqtt_end = micros();
  unsigned long mqtt_us = mqtt_end - section_start;

  if (!slot_resolved && assignment_wait_started != 0 &&
      millis() - assignment_wait_started >= ASSIGNMENT_WAIT_MS) {
    assignment_wait_started = 0;
    StoredSlot stored = loadStoredSlot();
    int slot = resolveAssignedSlot(
        ASSIGNMENT_MISSING, -1, authority_latched, stored.slot);
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

      char neighbor_id[NFCID_LENGTH * 2 + 1] = "";

      if (read_result == ISO15693_EC_OK) {
        convertNfcIdToHexString(card_id, NFCID_LENGTH, neighbor_id);
        if (strcmp(neighbor_id, last_neighbor_id) != 0) {
          debugPrintln(F("New card"));
          strncpy(last_neighbor_id, neighbor_id, sizeof(last_neighbor_id) - 1);
          last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
        }
      } else if (read_result == EC_NO_CARD) {
        if (strcmp(last_neighbor_id, "-") != 0) {
          debugPrintln(F("No card detected"));
          strncpy(last_neighbor_id, "-", sizeof(last_neighbor_id) - 1);
          last_neighbor_id[sizeof(last_neighbor_id) - 1] = '\0';
        }
      } else {
        Serial.printf("NFC read failed with error code: %d\n", read_result);
      }

      // Resolution moved to the server: publish the raw tag keyed by MAC and let
      // the roster decide which slot wears it. cube/right is no longer published
      // from this path. applyNfcChatterGate() below is what stops a dropped
      // read breaking a word in play.
      if (slotIsResolved()) {
        NfcObservationAction action = decideNfcObservation(
            read_result == ISO15693_EC_OK, read_result == EC_NO_CARD,
            neighbor_id, last_observation_published);
        NfcChatterResult chatter_result = applyNfcChatterGate(
            nfc_chatter_state, action, read_result == ISO15693_EC_OK,
            neighbor_id, millis());
        action = chatter_result.action;
        nfc_chatter_state = chatter_result.state;
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


      if (current_time - last_hall_poll >= HALL_POLL_INTERVAL_MS) {
        last_hall_poll = current_time;
      
        const uint8_t raw = readHallIdMask();

      
        if (raw == candidate_raw) {
          if (candidate_raw_count < HALL_DEBOUNCE_READS) candidate_raw_count++;
        } else {
          candidate_raw = raw;
          candidate_raw_count = 1;
        }
      
        // Not a debug artifact: stable_raw gates the NVS baseline save and
        // picks the candidate for the border preview.
        if (candidate_raw_count >= HALL_DEBOUNCE_READS) {
          stable_raw = candidate_raw;
        }

        uint8_t id = decodeHallNeighborId(raw);
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

        // decodeHallNeighborId() above fed the tracker this sample, so the
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
        // Fed every poll rather than on the publish deadband below, so the bar
        // follows the sensor rather than the reporting rate.
        display_manager->setPresencePercent(proximity, current_time);

        // A debounced 2-of-6 Hall mask identifies the candidate before the
        // presence latch confirms a neighbour. Preview the prospective shared
        // edge on both cubes while it is near, but never alter /border.
        static uint8_t preview_candidate = 0;
        static unsigned long last_preview_publish = 0;
        const uint8_t stable_candidate =
            stable_raw != 0xFF && __builtin_popcount(stable_raw) == 2
                ? hallCubeIdForMask(stable_raw)
                : 0;
        const uint8_t wanted_preview =
            shouldPreviewHallCandidate(proximity, presence_state) ? stable_candidate : 0;
        if (wanted_preview != preview_candidate ||
            (wanted_preview && current_time - last_preview_publish >= 1000)) {
          if (mqtt_client.isConnected()) {
            mqtt_client.publish(mqtt_topic_cube + "/border_preview", wanted_preview ? "E" : "", false);
            if (preview_candidate) mqtt_client.publish(String(MQTT_TOPIC_PREFIX_CUBE) + String(preview_candidate) + "/border_preview", "", false);
            // The consolidated border protocol renders this physical shared
            // edge as east on both participants, including the endpoint.
            if (wanted_preview) mqtt_client.publish(String(MQTT_TOPIC_PREFIX_CUBE) + String(wanted_preview) + "/border_preview", "E", false);
            preview_candidate = wanted_preview;
            last_preview_publish = current_time;
          }
        }

        // Only once there is a reference to save. An unprimed tracker has no
        // baseline worth carrying across a reset, and writing the magic anyway
        // would resurrect a baseline that recalibratePresence() just cleared if
        // the cube reset inside the settle window.
        storePresenceBaseline(stable_raw, presence_state, current_time);
      }
    }
  }

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
