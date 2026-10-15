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
#define STV091X_BLINDSCAN_SPECTRUM_STEP_KHZ	2000
#define STV091X_BLINDSCAN_SPECTRUM_BW_KHZ	4000
#define STV091X_BLINDSCAN_CANDIDATE_RANGE_HZ	10000000

enum stv091x_blindscan_modulation {
	STV091X_BLINDSCAN_MOD_UNKNOWN,
	STV091X_BLINDSCAN_MOD_QPSK,
	STV091X_BLINDSCAN_MOD_8PSK,
	STV091X_BLINDSCAN_MOD_16APSK,
	STV091X_BLINDSCAN_MOD_32APSK,
};

enum stv091x_blindscan_modulation
stv091x_blindscan_modulation_from_modcod(u32 modcod);

int stv091x_blindscan_detect_valleys(const s32 *samples, int num_samples,
				      int lag, s32 edge_threshold,
				      s32 depth_threshold, int min_width,
				      int max_width, int *indices,
				      int max_indices);

s32 stv091x_blindscan_after_lock_frequency(s32 frequency_khz,
					    u32 symbol_rate,
					    u32 scan_span_hz);
s32 stv091x_blindscan_failed_frequency(s32 frequency_khz, u32 scan_span_hz,
					int half_step);

#endif
