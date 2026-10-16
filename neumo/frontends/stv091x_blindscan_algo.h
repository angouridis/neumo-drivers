/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STV091X_BLINDSCAN_ALGO_H_
#define _STV091X_BLINDSCAN_ALGO_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef int32_t s32;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define STV091X_BLINDSCAN_STEP_KHZ		1000
#define STV091X_BLINDSCAN_INIT_SR		1000000
#define STV091X_BLINDSCAN_SEARCH_RANGE_HZ	60000000
/*
 * tbs6903.sys 0x2F341 blind step hardcodes DemodTimeout=500, FecTimeout=50
 * (not the STAPI 2000+500 table). False mode2 acquires must die in ~500 ms.
 */
#define STV091X_BLINDSCAN_VENDOR_DEMOD_TIMEOUT_MS	500
#define STV091X_BLINDSCAN_VENDOR_FEC_TIMEOUT_MS		50
#define STV091X_BLINDSCAN_LOCK_TIMEOUT_MS		\
	(STV091X_BLINDSCAN_VENDOR_DEMOD_TIMEOUT_MS + \
	 STV091X_BLINDSCAN_VENDOR_FEC_TIMEOUT_MS)
#define STV091X_BLINDSCAN_IQ_THRESHOLD		30
#define STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ	500000
#define STV091X_BLINDSCAN_VENDOR_EDGE_SEARCH_RANGE_HZ	500000
#define STV091X_BLINDSCAN_VENDOR_SETTLE_MS	10
#define STV091X_BLINDSCAN_VENDOR_ACQUISITION_RANGE_HZ	16000000
/* tbs6903.sys edge walk: ratio > 0x28, steps = 2*(tuner_bw/3e6), CFR from 0 up */
#define STV091X_BLINDSCAN_VENDOR_AGC2_RATIO	40
#define STV091X_BLINDSCAN_VENDOR_TUNER_BW_HZ	72000000
/* tbs6903.sys 0x2714C: post-lock retune uses (tuner_bw * 10) / 15 → 48 MHz */
#define STV091X_BLINDSCAN_VENDOR_POST_LOCK_TUNER_BW_HZ	48000000
#define STV091X_BLINDSCAN_VENDOR_ACQ_RANGE_PAD_KHZ	24000
#define STV091X_BLINDSCAN_VENDOR_ACQ_RANGE_MAX_HZ	40000000
/* tbs6903.sys 0x1C03D: advance = 135 * floor(SR_Hz / 200000) kHz */
#define STV091X_BLINDSCAN_VENDOR_AFTER_LOCK_SR_QUANT_HZ	200000
/* tbs6903.sys 0x1BEF4: sticky jump after asperity-1 */
#define STV091X_BLINDSCAN_VENDOR_STICKY_JUMP_KHZ	16000
/*
 * tbs6903.sys 0x1C00D: prior locks with |Δf| < 1000 kHz are the same TP.
 * Absolute 1 MHz only — not SR/2000 (that skipped neighbours of wide carriers).
 */
#define STV091X_BLINDSCAN_VENDOR_DUP_FLOOR_KHZ	1000
/* tbs6903.sys 0x2EAF4: rolling AGC2 window length */
#define STV091X_BLINDSCAN_VENDOR_EDGE_WINDOW	10
/* tbs6903.sys probe mode: 1 = run edge, 2 = skip edge and acquire */
#define STV091X_BLINDSCAN_VENDOR_PROBE_EDGE	1
#define STV091X_BLINDSCAN_VENDOR_PROBE_ACQUIRE	2

enum stv091x_blindscan_phase {
	STV091X_BLINDSCAN_VENDOR,
	STV091X_BLINDSCAN_FALLBACK,
};

struct stv091x_blindscan_traversal {
	enum stv091x_blindscan_phase phase;
	s32 vendor_frequency_khz;
	s32 fallback_frequency_khz;
	/* tbs6903.sys [demod+0x54] jump used for search range / fail zig-zag */
	s32 stored_jump_khz;
	/* tbs6903.sys edi: 0 = first fail, 1 = second fail, 2 = after asperity-1 */
	u8 fail_phase;
	/* tbs6903.sys edx: 1 = edge probe, 2 = skip-edge acquire */
	u8 probe_mode;
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
s32 stv091x_blindscan_vendor_duplicate_frequency(s32 frequency_khz,
						  u32 symbol_rate);
s32 stv091x_blindscan_vendor_failed_frequency(s32 center_khz);
s32 stv091x_blindscan_vendor_edge_steps(u32 tuner_bw_hz);
s32 stv091x_blindscan_vendor_miss_jump_khz(u32 tuner_bw_hz);
s32 stv091x_blindscan_vendor_edge_jump_khz(s32 step_index, int asperity,
					    s32 edge_samples);
/*
 * tbs6903.sys 0x2EC39 edge-decision step. Returns 1 when the walk should stop.
 * *refill_window is set when the rolling AGC buffer must be filled with the
 * current level (min-spike arm or edge_samples==1 false-alarm reset).
 */
int stv091x_blindscan_vendor_edge_on_ratio(u32 ratio, int level_is_min,
					    int *asperity,
					    int *waiting_for_fall,
					    s32 *edge_samples,
					    int *refill_window);
s32 stv091x_blindscan_vendor_next_frequency(s32 center_khz, int asperity,
					     s32 frequency_jump_khz,
					     u32 tuner_bw_hz, int locked,
					     s32 locked_frequency_khz,
					     u32 symbol_rate);
/*
 * tbs6903.sys 0x1BE9F aftermath: updates frequency, stored jump, fail_phase,
 * and probe_mode. asperity 0=miss, 1=filter-edge, 2=acquire attempted.
 */
void stv091x_blindscan_vendor_apply_result(
	struct stv091x_blindscan_traversal *traversal, int asperity,
	s32 frequency_jump_khz, u32 tuner_bw_hz, int locked,
	s32 locked_frequency_khz, u32 symbol_rate);
u32 stv091x_blindscan_vendor_acquisition_range_hz(s32 frequency_jump_khz);
/*
 * Edge walk only sweeps +CFR from the tuner LO. Blind CFRUP/LOW is
 * ±search_range/2 with search_range capped at 40 MHz, so a late asperity-2
 * (jump > 20 MHz) sits outside the acquisition window if we stay at center.
 * Shift the LO forward so the detected edge lands at CFRUP.
 */
s32 stv091x_blindscan_vendor_acquire_frequency_khz(s32 center_khz,
						    s32 frequency_jump_khz);
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
