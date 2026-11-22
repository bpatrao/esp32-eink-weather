// Pin probe: prints ADC1 voltages once, then reports any input pin that changes
// level (to find buttons). A reset shows up as a fresh boot banner.
#include <Arduino.h>
const int adcPins[] = {32, 33, 34, 35, 36, 39};
const int inPins[] = {0, 12, 19, 21, 22, 25, 26, 27, 32, 33, 34, 36, 37, 38, 39};
int last[40];
void setup() {
  Serial.begin(115200);
  for (int p : adcPins) Serial.printf("GPIO%d=%umV ", p, analogReadMilliVolts(p));
  Serial.println();
  for (int p : inPins) { pinMode(p, p >= 34 ? INPUT : INPUT_PULLUP); }
  delay(10);
  for (int p : inPins) last[p] = digitalRead(p);
  Serial.println("PROBE READY - press buttons");
}
void loop() {
  for (int p : inPins) {
    int v = digitalRead(p);
    if (v != last[p]) { Serial.printf("GPIO%d -> %s\n", p, v ? "HIGH" : "LOW"); last[p] = v; }
  }
  delay(5);
}
