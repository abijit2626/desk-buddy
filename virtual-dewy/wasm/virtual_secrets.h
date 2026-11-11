// Stand-in for firmware/dewy/secrets.h in the virtual build. These are made-up values, not anybody's secrets:
// the build never opens or copies the real secrets.h. The server address ends in ".invalid", which can never
// resolve, and the mock server in the browser answers it without any network.
#pragma once
#define WIFI_SSID       ""
#define WIFI_PASSWORD   ""
#define OTA_PASSWORD    ""
#define DEWY_SERVER        "https://dewy.virtual.invalid"
#define DEWY_REGISTER_KEY  "virtual-register-key"
