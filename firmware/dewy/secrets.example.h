// Copy this file to secrets.h (same folder) and fill it in. secrets.h is never uploaded to GitHub.
#pragma once
#define WIFI_SSID       ""         // empty = set it up from a phone through the "Dewy-Setup" hotspot
#define WIFI_PASSWORD   ""
#define OTA_PASSWORD    ""         // updates over Wi-Fi from the Arduino IDE stay off until you set one

// The phone app's server (see cloud/README.md). Empty = no phone app.
#define DEWY_SERVER        ""      // like "https://dewy.yourname.workers.dev" (no slash at the end)
#define DEWY_REGISTER_KEY  ""      // the same text as the server's REGISTER_KEY secret
