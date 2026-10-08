#include "LedStrip.h"

#define PIN_DATA 27
// How many leds in your strip?
#define NUM_LEDS 1

LedStrip led(PIN_DATA,NUM_LEDS);

void setup() {
  // initialize serial communication at 115200 bits per second:
  Serial.begin(115200);

  //set the resolution to 12 bits (0-4095)
  analogReadResolution(12);

  led.init();
  led.setBrightness(10);
}

void loop() {
  // read the analog / millivolts value for pin 1:
  int analogValue = analogRead(1);
  int analogVolts = analogReadMilliVolts(1);
  int vin_Volts = analogVolts * ( 51 + 510 + 51 ) / 51;

  // print out the values you read:
  Serial.printf("ADC analog value = %d\n", analogValue);
  Serial.printf("ADC millivolts value = %d\n", analogVolts);
  Serial.printf("VIN millivolts value = %d\n", vin_Volts); 

  led.setColor(0,0,255,0);
  led.show();

  delay(100);  // delay in between reads for clear read from serial

  led.setColor(0,0,0,0);
  led.show();

  delay(100);
}
