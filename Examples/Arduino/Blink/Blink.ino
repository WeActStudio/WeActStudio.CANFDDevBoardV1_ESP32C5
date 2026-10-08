#include <Arduino.h>
#include "LedStrip.h"

#define PIN_DATA 27
// How many leds in your strip?
#define NUM_LEDS 1

LedStrip led(PIN_DATA,NUM_LEDS);

void setup() {
  Serial.begin(115200);
  Serial.println("ws2812 init");

  led.init();
  led.setBrightness(50);
}

void loop() {
  Serial.println("ws2812 show");
  led.setColor(0,255,0,0);
  led.show();
  delay(500);

  led.setColor(0,0,255,0);
  led.show();
  delay(500);

  led.setColor(0,0,0,255);
  led.show();
  delay(500);
}
