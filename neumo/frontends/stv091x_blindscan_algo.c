// SPDX-License-Identifier: GPL-2.0
#include "stv091x_blindscan_algo.h"

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
