#include <Arduino.h>

#define HALL_PIN 23

void setup() {
  Serial.begin(115200);
  pinMode(HALL_PIN, INPUT_PULLUP);
}

void loop() {
  int val = digitalRead(HALL_PIN);
  Serial.println(val == LOW ? "NEAR (magnet detected)" : "FAR");
  delay(200);
}
