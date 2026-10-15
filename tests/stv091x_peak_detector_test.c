#include <assert.h>
#include <stdio.h>

#include "stv091x_peak_detector.h"

static void broad_valley_is_detected_at_its_center(void)
{
	s32 samples[96];
	int candidates[8];
	int i;
	int count;

	for (i = 0; i < 96; i++)
		samples[i] = i * 5;
	for (i = 28; i < 36; i++)
		samples[i] -= (i - 27) * 400;
	for (i = 36; i < 52; i++)
		samples[i] -= 3200;
	for (i = 52; i < 60; i++)
		samples[i] -= (59 - i) * 400;

	count = stv091x_detect_valleys(samples, 96, 4, 500, 1000,
				       2, 50, candidates, 8);
	assert(count == 1);
	printf("broad valley candidate: %d\n", candidates[0]);
	assert(candidates[0] >= 42 && candidates[0] <= 46);
}

static void monotonic_tuner_slope_is_rejected(void)
{
	s32 samples[96];
	int candidates[8];
	int i;

	for (i = 0; i < 96; i++)
		samples[i] = i * 100;

	assert(stv091x_detect_valleys(samples, 96, 4, 500, 1000,
				      2, 50, candidates, 8) == 0);
}

static void real_11597mhz_carrier_is_centered(void)
{
	s32 samples[] = {
		-34265, -34085, -33895, -33825, -33885, -33932, -33865,
		-33815, -33819, -34009, -34159, -34335, -34505, -34652,
		-34859, -35202, -35562, -35822, -35942, -36135, -36305,
		-36465, -36579, -36692, -36685, -36702, -36675, -36762,
		-36712, -36649, -36479, -36282, -36155, -36102, -35875,
		-35679, -35439, -35379, -35282, -35142, -35022, -34919,
		-34789, -34739, -34645, -34629, -34515, -34395, -34189,
		-34189, -34169
	};
	int candidates[8];
	int count;
	int frequency;

	count = stv091x_detect_valleys(samples,
				       sizeof(samples) / sizeof(samples[0]),
				       4, 500, 1000, 2, 50, candidates, 8);
	assert(count == 1);
	frequency = 11550 + candidates[0] * 2;
	printf("real carrier candidate: %d MHz\n", frequency);
	assert(frequency >= 11596 && frequency <= 11604);
}

int main(void)
{
	broad_valley_is_detected_at_its_center();
	monotonic_tuner_slope_is_rejected();
	real_11597mhz_carrier_is_centered();
	puts("stv091x peak detector tests passed");
	return 0;
}
