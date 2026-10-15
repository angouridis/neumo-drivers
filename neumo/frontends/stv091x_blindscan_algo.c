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

static s32 stv091x_blindscan_grid_align_at_or_after(s32 frequency_khz,
						     s32 grid_start_khz)
{
	s32 offset;

	if (frequency_khz < grid_start_khz)
		return grid_start_khz;

	offset = frequency_khz - grid_start_khz;
	if (offset % STV091X_BLINDSCAN_STEP_KHZ == 0)
		return frequency_khz;

	return grid_start_khz +
	       ((offset / STV091X_BLINDSCAN_STEP_KHZ) + 1) *
		       STV091X_BLINDSCAN_STEP_KHZ;
}

s32 stv091x_blindscan_next_grid_frequency(s32 frequency_khz,
					   s32 grid_start_khz)
{
	s32 aligned;

	if (frequency_khz < grid_start_khz)
		return grid_start_khz;

	aligned = stv091x_blindscan_grid_align_at_or_after(frequency_khz,
							    grid_start_khz);
	if (aligned == frequency_khz)
		return frequency_khz + STV091X_BLINDSCAN_STEP_KHZ;

	return aligned;
}

s32 stv091x_blindscan_after_lock_frequency(s32 frequency_khz,
					    u32 symbol_rate,
					    s32 grid_start_khz,
					    u32 search_range_hz)
{
	u32 occupied_half_width_khz;
	u32 acquisition_radius_khz;
	s32 minimum_next_khz;

	occupied_half_width_khz =
		(u32)((135ULL * symbol_rate) / (200ULL * 1000ULL));
	acquisition_radius_khz = search_range_hz / 2000;
	minimum_next_khz = frequency_khz + (s32)occupied_half_width_khz +
			   (s32)acquisition_radius_khz;

	return stv091x_blindscan_grid_align_at_or_after(minimum_next_khz,
							 grid_start_khz);
}
