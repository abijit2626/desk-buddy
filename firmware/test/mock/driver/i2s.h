#pragma once
// A pretend I2S driver for the PC tests. The microphone gives what g_micSource produces; the speaker keeps what it is given.
#include "Arduino.h"
#include <functional>
#include <vector>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_INTR_FLAG_LEVEL1 2
#define I2S_PIN_NO_CHANGE (-1)
enum i2s_port_t { I2S_NUM_0 = 0, I2S_NUM_1 = 1 };
enum i2s_mode_t { I2S_MODE_MASTER = 1, I2S_MODE_TX = 2, I2S_MODE_RX = 4 };
enum i2s_bits_per_sample_t { I2S_BITS_PER_SAMPLE_16BIT = 16, I2S_BITS_PER_SAMPLE_32BIT = 32 };
enum i2s_channel_fmt_t { I2S_CHANNEL_FMT_RIGHT_LEFT = 0, I2S_CHANNEL_FMT_ONLY_LEFT = 4 };
enum i2s_comm_format_t { I2S_COMM_FORMAT_STAND_I2S = 1 };
enum i2s_channel_t { I2S_CHANNEL_MONO = 1, I2S_CHANNEL_STEREO = 2 };
struct i2s_config_t { int mode; uint32_t sample_rate; int bits_per_sample, channel_format, communication_format, intr_alloc_flags, dma_buf_count, dma_buf_len; bool use_apll, tx_desc_auto_clear; };
struct i2s_pin_config_t { int mck_io_num, bck_io_num, ws_io_num, data_out_num, data_in_num; };
// tests: fill `out` with n 32-bit mic samples (already shifted left by 14, as the real chip's data would be)
inline std::function<void(int32_t* out, size_t n)> g_micSource = [](int32_t* out, size_t n) { for (size_t i = 0; i < n; i++) out[i] = 0; };
inline std::vector<int16_t> g_spoken; inline uint32_t g_spkRate = 0; inline int g_micReads = 0;
inline esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t*, int, void*) { return ESP_OK; }
inline esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t*) { return ESP_OK; }
inline esp_err_t i2s_zero_dma_buffer(i2s_port_t) { return ESP_OK; }
inline esp_err_t i2s_set_clk(i2s_port_t, uint32_t rate, uint32_t, i2s_channel_t) { g_spkRate = rate; return ESP_OK; }
inline esp_err_t i2s_read(i2s_port_t, void* dest, size_t size, size_t* got, TickType_t) { g_micReads++; g_micSource((int32_t*)dest, size / 4); *got = size; return ESP_OK; }
inline esp_err_t i2s_write(i2s_port_t, const void* src, size_t size, size_t* wr, TickType_t) { const int16_t* p = (const int16_t*)src; for (size_t i = 0; i + 1 < size / 2; i += 2) g_spoken.push_back(p[i]); *wr = size; return ESP_OK; }
