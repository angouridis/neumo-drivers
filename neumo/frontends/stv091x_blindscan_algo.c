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

int stv091x_blindscan_vendor_edge_on_ratio(u32 ratio, int level_is_min,
					    int *asperity,
					    int *waiting_for_fall,
					    s32 *edge_samples,
					    int *refill_window)
{
	*refill_window = 0;

	if (ratio > STV091X_BLINDSCAN_VENDOR_AGC2_RATIO && level_is_min) {
		/*
		 * tbs6903.sys 0x2EC48: arm asperity-1, refill window, then
		 * fall into the waiting-sample increment (0x2EC8F).
		 */
		*asperity = 1;
		*waiting_for_fall = 1;
		*edge_samples = 0;
		*refill_window = 1;
		if (++(*edge_samples) >= STV091X_BLINDSCAN_VENDOR_EDGE_WINDOW)
			return 1;
		return 0;
	}

	if (ratio > STV091X_BLINDSCAN_VENDOR_AGC2_RATIO) {
		/*
		 * tbs6903.sys 0x2EC60: asperity = waiting ? 1 : 2.
		 * If edge_samples == 1, treat as false alarm: clear state,
		 * refill window, and keep walking (0x2EC6A).
		 */
		*asperity = *waiting_for_fall ? 1 : 2;
		if (*edge_samples == 1) {
			*asperity = 0;
			*waiting_for_fall = 0;
			*edge_samples = 0;
			*refill_window = 1;
			return 0;
		}
		return 1;
	}

	if (*waiting_for_fall &&
	    ++(*edge_samples) >= STV091X_BLINDSCAN_VENDOR_EDGE_WINDOW)
		return 1;

	return 0;
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
	} else if (traversal->fail_phase == 1) {
		/* tbs6903.sys 0x1BF82: advance by full stored jump */
		jump = traversal->stored_jump_khz;
		if (jump < 1)
			jump = 1;
		next = center + jump;
		traversal->fail_phase = 0;
		traversal->probe_mode = STV091X_BLINDSCAN_VENDOR_PROBE_EDGE;
	} else {
		/*
		 * tbs6903.sys 0x1BF7C phase 2: no frequency advance, return
		 * to edge mode. Extra +2/+23 MHz hops here skipped the
		 * 11228–11475 H cluster (and nearby low-SR V TPs).
		 */
		next = center;
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

s32 stv091x_blindscan_vendor_acquire_frequency_khz(s32 center_khz,
						    s32 frequency_jump_khz)
{
	u32 range_hz =
		stv091x_blindscan_vendor_acquisition_range_hz(
			frequency_jump_khz);
	s32 half_khz = (s32)(range_hz / 2000);
	s32 jump_khz = frequency_jump_khz;

	if (jump_khz < 0)
		jump_khz = 0;
	if (jump_khz > half_khz)
		return center_khz + (jump_khz - half_khz);
	return center_khz;
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

	(void)symbol_rate;

	for (i = 0; i < num_candidate_locks; i++) {
		u32 distance_khz;

		distance_khz = candidate_locks[i].frequency_khz > frequency_khz ?
			candidate_locks[i].frequency_khz - frequency_khz :
			frequency_khz - candidate_locks[i].frequency_khz;

		/*
		 * tbs6903.sys 0x1C00D: cmp |Δf|, 1000; cmovl → duplicate only
		 * when strictly below 1000 kHz. Wide-SR SR/2000 windows were
		 * suppressing real neighbours a few MHz off a strong carrier.
		 */
		if (distance_khz < STV091X_BLINDSCAN_VENDOR_DUP_FLOOR_KHZ)
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
