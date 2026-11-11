#pragma once
// Just enough of the micro frontend for wakeword.h's feature code to be read by the PC compiler.
#include <cstddef>
#include <cstdint>
struct FrontendConfig {
  struct { size_t size_ms, step_size_ms; } window;
  struct { int num_channels; float lower_band_limit, upper_band_limit; } filterbank;
  struct { int smoothing_bits; float even_smoothing, odd_smoothing, min_signal_remaining; } noise_reduction;
  struct { int enable_pcan; float strength, offset; int gain_bits; } pcan_gain_control;
  struct { int enable_log, scale_shift; } log_scale;
};
struct FrontendState { int dummy; };
struct FrontendOutput { const uint16_t* values; size_t size; };
inline void FrontendFillConfigWithDefaults(FrontendConfig*) {}
inline int FrontendPopulateState(const FrontendConfig*, FrontendState*, int) { return 1; }
inline FrontendOutput FrontendProcessSamples(FrontendState*, const int16_t*, size_t, size_t* used) { *used = 0; return { nullptr, 0 }; }
