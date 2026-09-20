// PN5180 Test Jig - Tests PN5180 module using v1 board
//
// Connect PN5180 module to v1 board's CN1 header:
//   Pin 1: GND
//   Pin 2: BUSY  -> GPIO 36
//   Pin 3: SCK   -> GPIO 18
//   Pin 4: MISO  -> GPIO 39
//   Pin 5: MOSI  -> GPIO 23
//   Pin 6: NSS   -> GPIO 32
//   Pin 7: RST   -> GPIO 17
//   Pin 9: +5V
//
// Timing thresholds:
//   < 30ms  = Good
//   30-75ms = Fair
//   75-150ms = Marginal
//   > 150ms = Bad (stuck BUSY or dying PN5180)

#include <SPI.h>
#include <PN5180ISO15693.h>

PN5180ISO15693 nfc(32, 36, 17);  // NSS, BUSY, RST (v1 board pins)

// Timing stats
unsigned long minUs = 0xFFFFFFFF;
unsigned long maxUs = 0;
unsigned long totalUs = 0;
uint32_t readCount = 0;

void setup() {
  Serial.begin(115200);

  Serial.println("\n PN5180 Test Jig");
  Serial.println("================");

  // CRITICAL: SPI.begin() must come before nfc.begin()
  // to set custom MISO pin (GPIO 39 on v1)
  SPI.begin(18, 39, 23, -1);  // SCK, MISO, MOSI, SS

  nfc.begin();
  nfc.reset();
  nfc.setupRF();

  Serial.println("Ready - hold tag near coil\n");
}

void loop() {
  uint8_t uid[8];

  unsigned long start = micros();
  ISO15693ErrorCode result = nfc.getInventory(uid);
  unsigned long elapsed = micros() - start;

  // Update stats
  if (elapsed < minUs) minUs = elapsed;
  if (elapsed > maxUs) maxUs = elapsed;
  totalUs += elapsed;
  readCount++;

  if (result == ISO15693_EC_OK) {
    Serial.printf("TAG: %02X%02X... ", uid[0], uid[1]);
  } else if (result == EC_NO_CARD) {
    Serial.print(".");
  } else {
    Serial.printf("E:%02X ", result);
  }

  // Show timing every 10 reads
  if (readCount % 10 == 0) {
    unsigned long avgUs = totalUs / readCount;
    Serial.printf("\nTiming: min=%lums max=%lums avg=%lums n=%u\n",
      minUs / 1000, maxUs / 1000, avgUs / 1000, readCount);

    // Health indicator
    if (maxUs > 150000) {
      Serial.println("[BAD]");
    } else if (maxUs > 75000) {
      Serial.println("[MARGINAL]");
    } else if (maxUs > 30000) {
      Serial.println("[FAIR]");
    } else {
      Serial.println("[GOOD]");
    }
  }

  delay(500);
}
