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

int stv091x_blindscan_detect_valleys(const s32 *samples, int num_samples,
				      int lag, s32 edge_threshold,
				      s32 depth_threshold, int min_width,
				      int max_width, int *indices,
				      int max_indices)
{
	int fall_start = -1;
	int fall_end = -1;
	int rise_start = -1;
	int rise_end = -1;
	int count = 0;
	int i;

	if (!samples || !indices || num_samples <= 2 * lag || lag <= 0 ||
	    edge_threshold <= 0 || max_indices <= 0)
		return 0;

	for (i = lag; i < num_samples; i++) {
		s32 delta = samples[i] - samples[i - lag];
		int edge_center = i - lag / 2;

		if (fall_start < 0) {
			if (delta <= -edge_threshold && edge_center >= 2 * lag) {
				fall_start = edge_center;
				fall_end = edge_center;
			}
			continue;
		}

		if (rise_start < 0) {
			if (delta <= -edge_threshold)
				fall_end = edge_center;
			if (delta >= edge_threshold) {
				rise_start = edge_center;
				rise_end = edge_center;
			} else if (edge_center - fall_start > max_width) {
				fall_start = -1;
				fall_end = -1;
			}
			continue;
		}

		if (delta >= edge_threshold)
			rise_end = edge_center;

		if (delta < edge_threshold || i == num_samples - 1) {
			int fall = (fall_start + fall_end) / 2;
			int rise = (rise_start + rise_end) / 2;
			int width = rise - fall;

			if (width >= min_width && width <= max_width &&
			    rise + lag < num_samples) {
				s32 valley = samples[fall];
				s32 left_shoulder;
				s32 right_shoulder;
				s32 shoulder;
				int left_start = fall_start - lag;
				int right_end = rise_end + lag;
				int j;

				if (left_start < 0)
					left_start = 0;
				if (right_end >= num_samples)
					right_end = num_samples - 1;
				for (j = fall + 1; j <= rise; j++)
					if (samples[j] < valley)
						valley = samples[j];

				left_shoulder = samples[left_start];
				for (j = left_start + 1; j <= fall; j++)
					if (samples[j] > left_shoulder)
						left_shoulder = samples[j];
				right_shoulder = samples[rise];
				for (j = rise + 1; j <= right_end; j++)
					if (samples[j] > right_shoulder)
						right_shoulder = samples[j];
				shoulder = left_shoulder < right_shoulder ?
					left_shoulder : right_shoulder;

				if (shoulder - valley >= depth_threshold) {
					indices[count++] = (fall + rise) / 2;
					if (count >= max_indices)
						return count;
				}
			}

			fall_start = -1;
			fall_end = -1;
			rise_start = -1;
			rise_end = -1;
		}
	}

	return count;
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
