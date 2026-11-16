#pragma once
#include <cstdlib>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_DMA 8
inline size_t g_freeInternal = 120000;   // tests change this to pretend RAM is short
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void* heap_caps_aligned_alloc(size_t, size_t n, int) { return malloc(n); }
inline size_t heap_caps_get_free_size(int) { return g_freeInternal; }
inline size_t heap_caps_get_largest_free_block(int) { return g_freeInternal; }
