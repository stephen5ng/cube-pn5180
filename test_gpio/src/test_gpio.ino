// GPIO 35 Test - Tests if PN5180_BUSY pin (GPIO 35) is working
//
// Usage:
//   1. Upload to ESP32
//   2. Open Serial Monitor (115200 baud)
//   3. Connect GPIO 35 to GND/3.3V with jumper wire
//   4. Verify readings are correct

#define PIN_UNDER_TEST 35

void setup() {
  Serial.begin(115200);
  pinMode(PIN_UNDER_TEST, INPUT_PULLUP);

  Serial.println("\n GPIO 35 Test");
  Serial.println("=============");
  Serial.println("Connect GPIO 35 to: GND (read 0), 3.3V (read 1)\n");
}

void loop() {
  int reading = digitalRead(PIN_UNDER_TEST);
  Serial.printf("GPIO 35: %d %s\n", reading, reading == LOW ? "(GND)" : "(HIGH)");
  delay(100);
}
