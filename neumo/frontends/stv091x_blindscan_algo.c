// SPDX-License-Identifier: GPL-2.0
#include "stv091x_blindscan_algo.h"

s32 stv091x_blindscan_next_frequency(s32 frequency_khz, int locked,
				      u32 symbol_rate)
{
	s32 step = STV091X_BLINDSCAN_STEP_KHZ;

	if (locked && symbol_rate >= STV091X_BLINDSCAN_INIT_SR)
		step = 135 * (symbol_rate / 1000) / 200;

	return frequency_khz + step;
}
