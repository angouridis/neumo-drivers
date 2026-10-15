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
	return frequency_khz + (s32)(((u64)symbol_rate * 675) / 1000000);
}

void stv091x_blindscan_traversal_init(
	struct stv091x_blindscan_traversal *traversal,
	s32 start_frequency_khz)
{
	traversal->phase = STV091X_BLINDSCAN_VENDOR;
	traversal->vendor_frequency_khz = start_frequency_khz;
	traversal->fallback_frequency_khz = start_frequency_khz;
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

		if (candidate_locks[i].symbol_rate < smaller_symbol_rate)
			smaller_symbol_rate = candidate_locks[i].symbol_rate;
		distance_khz = candidate_locks[i].frequency_khz > frequency_khz ?
			candidate_locks[i].frequency_khz - frequency_khz :
			frequency_khz - candidate_locks[i].frequency_khz;

		if ((u64)distance_khz * 2000 <= smaller_symbol_rate)
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
