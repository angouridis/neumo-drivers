/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STV091X_PEAK_DETECTOR_H_
#define _STV091X_PEAK_DETECTOR_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef int32_t s32;
#endif

int stv091x_detect_valleys(const s32 *samples, int count, int lag,
			   s32 edge_threshold, s32 depth_threshold,
			   int min_width, int max_width,
			   int *candidates, int max_candidates);

#endif
