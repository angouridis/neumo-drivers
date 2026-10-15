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

#define STV091X_BLINDSCAN_STEP_KHZ		1000
#define STV091X_BLINDSCAN_INIT_SR		1000000
#define STV091X_BLINDSCAN_SEARCH_RANGE_HZ	60000000
#define STV091X_BLINDSCAN_LOCK_TIMEOUT_MS	500
#define STV091X_BLINDSCAN_IQ_THRESHOLD		30

s32 stv091x_blindscan_after_lock_frequency(s32 frequency_khz,
					    u32 symbol_rate,
					    u32 scan_span_hz);
s32 stv091x_blindscan_failed_frequency(s32 frequency_khz, u32 scan_span_hz,
					int half_step);

#endif
