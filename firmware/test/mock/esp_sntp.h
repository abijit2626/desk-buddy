#pragma once
struct timeval;
inline void sntp_set_time_sync_notification_cb(void (*)(struct timeval*)) {}
