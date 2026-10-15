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
#define STV091X_BLINDSCAN_SEARCH_RANGE_HZ	500000
#define STV091X_BLINDSCAN_LOCK_TIMEOUT_MS	500
#define STV091X_BLINDSCAN_IQ_THRESHOLD		30

s32 stv091x_blindscan_next_frequency(s32 frequency_khz, int locked,
				      u32 symbol_rate);

#endif
