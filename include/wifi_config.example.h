// Copy this file to wifi_config.h (which git ignores) and fill it in. The pet app uses
// WiFi once at start-up to get the time, so the pet knows how long it was switched off.
#pragma once

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// POSIX timezone, used for the pet's bedtime. This one is the UK (GMT/BST).
#define PET_TIMEZONE "GMT0BST,M3.5.0/1,M10.5.0"
