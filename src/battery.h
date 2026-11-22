#pragma once
#include <Arduino.h>

// LiPo on GPIO 35 through a 1:2 divider (T5 V2.3; measured 2128 mV at the pin
// for a ~4.26 V battery on USB charge).
constexpr int BATTERY_PIN = 35;
constexpr int BATTERY_EMPTY_MV = 3300;  // below this: stop updating, protect the cell

int batteryMillivolts();
int batteryPercent(int mv);
