#pragma once
struct timeval;
inline void (*g_sntpCb)(struct timeval*) = nullptr;
inline void sntp_set_time_sync_notification_cb(void (*cb)(struct timeval*)) { g_sntpCb = cb; }
