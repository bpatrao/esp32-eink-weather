#pragma once
#include "config.h"

// Runs the setup access point + captive portal. Never returns: it restarts
// the board after saving, or deep-sleeps if nobody completes setup.
[[noreturn]] void runPortal(Settings& s);
