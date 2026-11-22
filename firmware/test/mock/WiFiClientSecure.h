#pragma once
#include "WiFi.h"
struct WiFiClientSecure : WiFiClient { void setInsecure() {} void setCACert(const char*) {} void setTimeout(int) {} int lastError(char* b, size_t n) { if (n) b[0] = 0; return 0; } void stop() {} };
