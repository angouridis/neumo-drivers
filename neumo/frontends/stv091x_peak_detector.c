// SPDX-License-Identifier: GPL-2.0
#include "stv091x_peak_detector.h"

int stv091x_detect_valleys(const s32 *samples, int count, int lag,
			   s32 edge_threshold, s32 depth_threshold,
			   int min_width, int max_width,
			   int *candidates, int max_candidates)
{
	int fall_start = -1;
	int fall_end = -1;
	int rise_start = -1;
	int rise_end = -1;
	int found = 0;
	int i;

	if (!samples || !candidates || count <= 2 * lag || lag <= 0 ||
	    edge_threshold <= 0 || max_candidates <= 0)
		return 0;

	for (i = lag; i < count; i++) {
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

		if (delta < edge_threshold || i == count - 1) {
			int fall = (fall_start + fall_end) / 2;
			int rise = (rise_start + rise_end) / 2;
			int width = rise - fall;

			if (width >= min_width && width <= max_width &&
			    rise + lag < count) {
				s32 valley = samples[fall];
				s32 shoulder;
				int j;

				for (j = fall + 1; j <= rise; j++)
					if (samples[j] < valley)
						valley = samples[j];
				shoulder = samples[fall - lag];
				if (samples[rise + lag] < shoulder)
					shoulder = samples[rise + lag];

				if (shoulder - valley >= depth_threshold) {
					candidates[found++] = (fall + rise) / 2;
					if (found >= max_candidates)
						return found;
				}
			}

			fall_start = -1;
			fall_end = -1;
			rise_start = -1;
			rise_end = -1;
		}
	}

	return found;
}
