/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STV091X_BLINDSCAN_ALGO_H_
#define _STV091X_BLINDSCAN_ALGO_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef int32_t s32;
typedef uint32_t u32;
#endif

#define STV091X_BLINDSCAN_STEP_KHZ		2000
#define STV091X_BLINDSCAN_INIT_SR		1000000
#define STV091X_BLINDSCAN_SEARCH_RANGE_HZ	2000000
#define STV091X_BLINDSCAN_LOCK_TIMEOUT_MS	500
#define STV091X_BLINDSCAN_IQ_THRESHOLD		30

s32 stv091x_blindscan_next_grid_frequency(s32 frequency_khz,
					   s32 grid_start_khz);
s32 stv091x_blindscan_after_lock_frequency(s32 frequency_khz,
					    u32 symbol_rate,
					    s32 grid_start_khz,
					    u32 search_range_hz);

#endif
