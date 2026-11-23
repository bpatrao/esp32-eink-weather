#pragma once
#include "config.h"

// Runs the setup access point + captive portal, and accepts firmware updates
// (HTTP upload at /update from a browser or `pio run -e ota`; see README). Never
// returns: it restarts the board after saving or updating, or deep-sleeps if
// nobody completes setup.
[[noreturn]] void runPortal(Settings& s);
