#pragma once
#include <stddef.h>
#include <stdint.h>
// Named fields mirror those used from the real SDK symbol; no target layout assumptions.
struct rmt_data_t { uint32_t duration0, level0, duration1, level1; };
enum { RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1 };
bool rmtInit(int, int, int, uint32_t);
bool rmtTransmitCompleted(int);
bool rmtWriteAsync(int, rmt_data_t *, size_t);
