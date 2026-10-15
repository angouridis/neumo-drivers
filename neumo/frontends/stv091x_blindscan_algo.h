/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STV091X_BLINDSCAN_ALGO_H_
#define _STV091X_BLINDSCAN_ALGO_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef int32_t s32;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define STV091X_BLINDSCAN_STEP_KHZ		1000
#define STV091X_BLINDSCAN_INIT_SR		1000000
#define STV091X_BLINDSCAN_SEARCH_RANGE_HZ	60000000
#define STV091X_BLINDSCAN_LOCK_TIMEOUT_MS	500
#define STV091X_BLINDSCAN_IQ_THRESHOLD		30
#define STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ	500000
#define STV091X_BLINDSCAN_VENDOR_EDGE_SEARCH_RANGE_HZ	500000
#define STV091X_BLINDSCAN_VENDOR_SETTLE_MS	10
#define STV091X_BLINDSCAN_VENDOR_ACQUISITION_RANGE_HZ	16000000

enum stv091x_blindscan_phase {
	STV091X_BLINDSCAN_VENDOR,
	STV091X_BLINDSCAN_FALLBACK,
};

struct stv091x_blindscan_traversal {
	enum stv091x_blindscan_phase phase;
	s32 vendor_frequency_khz;
	s32 fallback_frequency_khz;
};

struct stv091x_blindscan_lock {
	s32 frequency_khz;
	u32 symbol_rate;
};

enum stv091x_blindscan_modulation {
	STV091X_BLINDSCAN_MOD_UNKNOWN,
	STV091X_BLINDSCAN_MOD_QPSK,
	STV091X_BLINDSCAN_MOD_8PSK,
	STV091X_BLINDSCAN_MOD_16APSK,
	STV091X_BLINDSCAN_MOD_32APSK,
};

enum stv091x_blindscan_modulation
stv091x_blindscan_modulation_from_modcod(u32 modcod);

s32 stv091x_blindscan_after_lock_frequency(s32 frequency_khz,
					    u32 symbol_rate,
					    u32 scan_span_hz);
s32 stv091x_blindscan_failed_frequency(s32 frequency_khz, u32 scan_span_hz,
					int half_step);
s32 stv091x_blindscan_vendor_after_lock_frequency(s32 frequency_khz,
						   u32 symbol_rate);
void stv091x_blindscan_traversal_init(
	struct stv091x_blindscan_traversal *traversal,
	s32 start_frequency_khz);
s32 stv091x_blindscan_transition_to_fallback(
	struct stv091x_blindscan_traversal *traversal);
int stv091x_blindscan_lock_is_duplicate(
	const struct stv091x_blindscan_lock *candidate_locks,
	u32 num_candidate_locks, s32 frequency_khz, u32 symbol_rate);
int stv091x_blindscan_add_unique_lock(
	struct stv091x_blindscan_lock *candidate_locks,
	u32 *num_candidate_locks, u32 capacity,
	s32 frequency_khz, u32 symbol_rate);

#endif
