// SPDX-License-Identifier: GPL-2.0
#include "stv091x_blindscan_algo.h"

enum stv091x_blindscan_modulation
stv091x_blindscan_modulation_from_modcod(u32 modcod)
{
	if (modcod >= 1 && modcod <= 11)
		return STV091X_BLINDSCAN_MOD_QPSK;
	if (modcod >= 12 && modcod <= 17)
		return STV091X_BLINDSCAN_MOD_8PSK;
	if (modcod >= 18 && modcod <= 23)
		return STV091X_BLINDSCAN_MOD_16APSK;
	if (modcod >= 24 && modcod <= 28)
		return STV091X_BLINDSCAN_MOD_32APSK;

	return STV091X_BLINDSCAN_MOD_UNKNOWN;
}

s32 stv091x_blindscan_after_lock_frequency(s32 frequency_khz,
					    u32 symbol_rate,
					    u32 scan_span_hz)
{
	u32 carrier_edge_khz = STV091X_BLINDSCAN_STEP_KHZ;
	u32 window_radius_khz = scan_span_hz / 2000;

	if (symbol_rate >= STV091X_BLINDSCAN_INIT_SR)
		carrier_edge_khz = 135 * (symbol_rate / 1000) / 200;

	return frequency_khz + carrier_edge_khz + window_radius_khz;
}

s32 stv091x_blindscan_failed_frequency(s32 frequency_khz, u32 scan_span_hz,
					int half_step)
{
	u32 step_khz = scan_span_hz / 1000;

	if (half_step)
		step_khz /= 2;
	if (step_khz < STV091X_BLINDSCAN_STEP_KHZ)
		step_khz = STV091X_BLINDSCAN_STEP_KHZ;

	return frequency_khz + step_khz;
}

s32 stv091x_blindscan_vendor_after_lock_frequency(s32 frequency_khz,
						   u32 symbol_rate)
{
	/* tbs6903.sys 0x1C03D: freq += 135 * floor(SR_Hz / 200000) */
	return frequency_khz + (s32)(135 *
		(symbol_rate / STV091X_BLINDSCAN_VENDOR_AFTER_LOCK_SR_QUANT_HZ));
}

s32 stv091x_blindscan_vendor_duplicate_frequency(s32 frequency_khz,
						  u32 symbol_rate)
{
	/* tbs6903.sys 0x1C025: near prior lock → freq += SR_Hz/1000 */
	s32 advance_khz = (s32)(symbol_rate / 1000);

	if (advance_khz < STV091X_BLINDSCAN_VENDOR_DUP_FLOOR_KHZ)
		advance_khz = STV091X_BLINDSCAN_VENDOR_DUP_FLOOR_KHZ;
	return frequency_khz + advance_khz;
}

s32 stv091x_blindscan_vendor_failed_frequency(s32 center_khz)
{
	return center_khz +
		STV091X_BLINDSCAN_VENDOR_ACQUISITION_RANGE_HZ / 1000;
}

s32 stv091x_blindscan_vendor_edge_steps(u32 tuner_bw_hz)
{
	s32 steps = (s32)((tuner_bw_hz / 3000000) * 2);

	if (steps < 10)
		steps = 10;
	return steps;
}

s32 stv091x_blindscan_vendor_miss_jump_khz(u32 tuner_bw_hz)
{
	/*
	 * tbs6903.sys range loop status 0x11: freq += tuner_bw/3000 - 1000.
	 * For 72 MHz that is 23000 kHz (not the raw 48*500 walk span).
	 */
	s32 jump_khz = (s32)(tuner_bw_hz / 3000) - STV091X_BLINDSCAN_STEP_KHZ;

	if (jump_khz < STV091X_BLINDSCAN_STEP_KHZ)
		jump_khz = STV091X_BLINDSCAN_STEP_KHZ;
	return jump_khz;
}

s32 stv091x_blindscan_vendor_edge_jump_khz(s32 step_index, int asperity,
					    s32 edge_samples)
{
	s32 steps = step_index;

	if (asperity == 1)
		steps -= (edge_samples + 2) / 2;
	if (steps < 0)
		steps = 0;
	return steps * (STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ / 1000);
}

s32 stv091x_blindscan_vendor_next_frequency(s32 center_khz, int asperity,
					     s32 frequency_jump_khz,
					     u32 tuner_bw_hz, int locked,
					     s32 locked_frequency_khz,
					     u32 symbol_rate)
{
	struct stv091x_blindscan_traversal traversal;

	stv091x_blindscan_traversal_init(&traversal, center_khz);
	stv091x_blindscan_vendor_apply_result(
		&traversal, asperity, frequency_jump_khz, tuner_bw_hz,
		locked, locked_frequency_khz, symbol_rate);
	return traversal.vendor_frequency_khz;
}

void stv091x_blindscan_vendor_apply_result(
	struct stv091x_blindscan_traversal *traversal, int asperity,
	s32 frequency_jump_khz, u32 tuner_bw_hz, int locked,
	s32 locked_frequency_khz, u32 symbol_rate)
{
	s32 center = traversal->vendor_frequency_khz;
	s32 jump = frequency_jump_khz;
	s32 next = center;

	/*
	 * tbs6903.sys 0x1BE9F aftermath (edi=fail_phase, edx=probe_mode):
	 *  asperity 0 / empty (status 0x11): += bw/3000 - 1000; mode=1
	 *  asperity 1 (status 0x12): += J; sticky J=16000; phase=2; mode=2
	 *  asperity 2 lock ok: past half occupied BW; mode=1
	 *  asperity 2 fail phase 0: store J/2; += J/2; phase=1; mode=2
	 *  asperity 2 fail phase 1: += stored J; phase=0; mode=1
	 *  asperity 2 fail phase 2: no advance; phase=0; mode=1
	 */
	if (asperity <= 0) {
		next = center +
			stv091x_blindscan_vendor_miss_jump_khz(tuner_bw_hz);
		traversal->fail_phase = 0;
		traversal->probe_mode = STV091X_BLINDSCAN_VENDOR_PROBE_EDGE;
		traversal->sticky_fail_streak = 0;
	} else if (asperity == 1) {
		if (jump < STV091X_BLINDSCAN_STEP_KHZ)
			jump = STV091X_BLINDSCAN_STEP_KHZ;
		next = center + jump;
		traversal->stored_jump_khz =
			STV091X_BLINDSCAN_VENDOR_STICKY_JUMP_KHZ;
		traversal->fail_phase = 2;
		traversal->probe_mode =
			STV091X_BLINDSCAN_VENDOR_PROBE_ACQUIRE;
	} else if (locked) {
		next = stv091x_blindscan_vendor_after_lock_frequency(
			locked_frequency_khz, symbol_rate);
		traversal->fail_phase = 0;
		traversal->probe_mode = STV091X_BLINDSCAN_VENDOR_PROBE_EDGE;
		traversal->sticky_fail_streak = 0;
	} else if (traversal->fail_phase == 0) {
		/* tbs6903.sys 0x1BF46: store and advance by floor(J/2) */
		if (jump < 0)
			jump = 0;
		jump /= 2;
		if (jump < 1)
			jump = 1;
		traversal->stored_jump_khz = jump;
		next = center + jump;
		traversal->fail_phase = 1;
		traversal->probe_mode =
			STV091X_BLINDSCAN_VENDOR_PROBE_ACQUIRE;
		traversal->sticky_fail_streak = 0;
	} else if (traversal->fail_phase == 1) {
		/*
		 * tbs6903.sys 0x1BF82: advance by full stored jump.
		 * Robustness: micro stored jumps get a small forward hop so
		 * false edge=2 pairs cannot crawl forever, but keep the hop
		 * far below typical inter-TP spacing.
		 */
		jump = traversal->stored_jump_khz;
		if (jump <= STV091X_BLINDSCAN_VENDOR_SMALL_JUMP_KHZ)
			jump = STV091X_BLINDSCAN_VENDOR_CRAWL_ESCAPE_KHZ;
		else if (jump < 1)
			jump = 1;
		next = center + jump;
		traversal->fail_phase = 0;
		traversal->probe_mode = STV091X_BLINDSCAN_VENDOR_PROBE_EDGE;
		traversal->sticky_fail_streak = 0;
	} else {
		/*
		 * tbs6903.sys phase 2: no advance. Robustness: hop forward;
		 * after a few consecutive sticky fails, miss-hop out of the
		 * filter-edge storm so the scan does not look hung.
		 */
		if (traversal->sticky_fail_streak < 255)
			traversal->sticky_fail_streak++;
		if (traversal->sticky_fail_streak >=
		    STV091X_BLINDSCAN_VENDOR_STICKY_FAIL_MAX) {
			next = center +
				stv091x_blindscan_vendor_miss_jump_khz(
					tuner_bw_hz);
			traversal->sticky_fail_streak = 0;
		} else {
			next = center +
				STV091X_BLINDSCAN_VENDOR_CRAWL_ESCAPE_KHZ;
		}
		traversal->fail_phase = 0;
		traversal->probe_mode = STV091X_BLINDSCAN_VENDOR_PROBE_EDGE;
	}

	if (next < center)
		next = center + STV091X_BLINDSCAN_STEP_KHZ;

	traversal->vendor_frequency_khz = next;
}

u32 stv091x_blindscan_vendor_acquisition_range_hz(s32 frequency_jump_khz)
{
	s32 jump_khz = frequency_jump_khz;
	u32 range_hz;

	if (jump_khz < 0)
		jump_khz = 0;
	/* tbs6903.sys 0x1F194: min((jump_kHz + 24000) * 1000, 40e6) */
	range_hz = (u32)(jump_khz + STV091X_BLINDSCAN_VENDOR_ACQ_RANGE_PAD_KHZ) *
		1000;
	if (range_hz > STV091X_BLINDSCAN_VENDOR_ACQ_RANGE_MAX_HZ)
		range_hz = STV091X_BLINDSCAN_VENDOR_ACQ_RANGE_MAX_HZ;
	if (range_hz < STV091X_BLINDSCAN_VENDOR_ACQUISITION_RANGE_HZ)
		range_hz = STV091X_BLINDSCAN_VENDOR_ACQUISITION_RANGE_HZ;
	return range_hz;
}

void stv091x_blindscan_traversal_init(
	struct stv091x_blindscan_traversal *traversal,
	s32 start_frequency_khz)
{
	traversal->phase = STV091X_BLINDSCAN_VENDOR;
	traversal->vendor_frequency_khz = start_frequency_khz;
	traversal->fallback_frequency_khz = start_frequency_khz;
	traversal->stored_jump_khz = 0;
	traversal->fail_phase = 0;
	traversal->probe_mode = STV091X_BLINDSCAN_VENDOR_PROBE_EDGE;
	traversal->sticky_fail_streak = 0;
}

s32 stv091x_blindscan_transition_to_fallback(
	struct stv091x_blindscan_traversal *traversal)
{
	traversal->phase = STV091X_BLINDSCAN_FALLBACK;
	return traversal->fallback_frequency_khz;
}

int stv091x_blindscan_lock_is_duplicate(
	const struct stv091x_blindscan_lock *candidate_locks,
	u32 num_candidate_locks, s32 frequency_khz, u32 symbol_rate)
{
	u32 i;

	for (i = 0; i < num_candidate_locks; i++) {
		u32 smaller_symbol_rate = symbol_rate;
		u32 distance_khz;
		u32 sr_window_khz;

		if (candidate_locks[i].symbol_rate < smaller_symbol_rate)
			smaller_symbol_rate = candidate_locks[i].symbol_rate;
		distance_khz = candidate_locks[i].frequency_khz > frequency_khz ?
			candidate_locks[i].frequency_khz - frequency_khz :
			frequency_khz - candidate_locks[i].frequency_khz;

		/*
		 * Wide carriers: within half symbol-rate spacing.
		 * Low-SR: tbs6903.sys also uses a fixed 1000 kHz floor so
		 * re-locks like 10957.7 / 10958.3 @1Msps collapse.
		 */
		sr_window_khz = smaller_symbol_rate / 2000;
		if (sr_window_khz < STV091X_BLINDSCAN_VENDOR_DUP_FLOOR_KHZ)
			sr_window_khz = STV091X_BLINDSCAN_VENDOR_DUP_FLOOR_KHZ;
		if (distance_khz <= sr_window_khz)
			return 1;
	}

	return 0;
}

int stv091x_blindscan_add_unique_lock(
	struct stv091x_blindscan_lock *candidate_locks,
	u32 *num_candidate_locks, u32 capacity,
	s32 frequency_khz, u32 symbol_rate)
{
	struct stv091x_blindscan_lock *lock;

	if (stv091x_blindscan_lock_is_duplicate(
		    candidate_locks, *num_candidate_locks,
		    frequency_khz, symbol_rate))
		return 0;
	if (*num_candidate_locks >= capacity)
		return -1;

	lock = &candidate_locks[(*num_candidate_locks)++];
	lock->frequency_khz = frequency_khz;
	lock->symbol_rate = symbol_rate;
	return 1;
}
