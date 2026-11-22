#include "battery.h"

int batteryMillivolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(BATTERY_PIN);
  return sum / 16 * 2;  // undo the 1:2 divider
}

// Approximate LiPo discharge curve (resting voltage -> charge %).
int batteryPercent(int mv) {
  static const struct { int mv, pct; } curve[] = {
      {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50}, {3750, 40},
      {3700, 30},  {3650, 20}, {3600, 12}, {3500, 5},  {3300, 0},
  };
  if (mv >= curve[0].mv) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv >= curve[i].mv) {
      const auto &hi = curve[i - 1], &lo = curve[i];
      return lo.pct + (mv - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv);
    }
  }
  return 0;
}
