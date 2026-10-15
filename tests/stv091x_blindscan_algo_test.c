#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void two_mhz_spectrum_grid_finds_carrier_valleys(void)
{
	const s32 samples[] = {
		0, 0, 0, -2000, -2100, -2200, -2100, -2000, 0, 0,
		-1800, -1900, -1800, -1700, 0, 0
	};
	int candidates[4] = { -1, -1, -1, -1 };
	int count;

	count = stv091x_blindscan_detect_valleys(
		samples, 16, 1, 500, 1000, 2, 8, candidates, 4);

	assert(count == 2);
	assert(candidates[0] == 5);
	assert(candidates[1] == 12);
}

static void flat_spectrum_has_no_candidates(void)
{
	const s32 samples[] = { 100, 120, 80, 110, 90, 100 };
	int candidates[2] = { -1, -1 };

	assert(stv091x_blindscan_detect_valleys(
		       samples, 6, 1, 500, 1000, 1, 5, candidates, 2) == 0);
}

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

	count = stv091x_blindscan_detect_valleys(
		samples, 96, 4, 500, 1000, 2, 50, candidates, 8);

	assert(count == 1);
	assert(candidates[0] >= 42 && candidates[0] <= 46);
}

static void real_11597mhz_carrier_is_centered(void)
{
	const s32 samples[] = {
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

	count = stv091x_blindscan_detect_valleys(
		samples, sizeof(samples) / sizeof(samples[0]),
		4, 500, 1000, 2, 50, candidates, 8);

	assert(count == 1);
	frequency = 11550 + candidates[0] * 2;
	assert(frequency >= 11596 && frequency <= 11604);
}

static void adjacent_carriers_with_narrow_guard_are_both_detected(void)
{
	s32 samples[52] = { 0 };
	int candidates[8];
	int count;
	int i;

	for (i = 8; i <= 23; i++)
		samples[i] = -2200;
	for (i = 26; i <= 43; i++)
		samples[i] = -2400;

	count = stv091x_blindscan_detect_valleys(
		samples, 52, 4, 500, 1000, 2, 50, candidates, 8);

	assert(count == 2);
	assert(candidates[0] >= 14 && candidates[0] <= 17);
	assert(candidates[1] >= 33 && candidates[1] <= 36);
}

static void scan_constants_restore_wide_search_policy(void)
{
	assert(STV091X_BLINDSCAN_SPECTRUM_STEP_KHZ == 2000);
	assert(STV091X_BLINDSCAN_SPECTRUM_BW_KHZ == 4000);
	assert(STV091X_BLINDSCAN_CANDIDATE_RANGE_HZ == 10000000);
	assert(STV091X_BLINDSCAN_STEP_KHZ == 1000);
	assert(STV091X_BLINDSCAN_SEARCH_RANGE_HZ == 60000000);
}

static void candidate_acquisition_uses_configured_max_symbol_rate(void)
{
	assert(stv091x_blindscan_candidate_symbol_rate(45000000) ==
	       45000000);
	assert(stv091x_blindscan_candidate_symbol_rate(0) ==
	       45000000);
	assert(stv091x_blindscan_candidate_symbol_rate(500000) ==
	       45000000);
	assert(stv091x_blindscan_candidate_symbol_rate(60000000) ==
	       45000000);
}

static void implausibly_short_candidate_list_uses_fallback(void)
{
	assert(!stv091x_blindscan_candidate_list_usable(
		4, 10700000, 11700000));
	assert(stv091x_blindscan_candidate_list_usable(
		20, 10700000, 11700000));
	assert(stv091x_blindscan_candidates_need_fallback(12, 0));
	assert(!stv091x_blindscan_candidates_need_fallback(12, 1));
}

static void first_failed_probe_advances_half_scan_span(void)
{
	assert(stv091x_blindscan_failed_frequency(
		       950000, STV091X_BLINDSCAN_SEARCH_RANGE_HZ, 1) ==
	       980000);
}

static void later_failed_probe_advances_full_scan_span(void)
{
	assert(stv091x_blindscan_failed_frequency(
		       980000, STV091X_BLINDSCAN_SEARCH_RANGE_HZ, 0) ==
	       1040000);
}

static void lock_moves_next_window_past_carrier(void)
{
	s32 next = stv091x_blindscan_after_lock_frequency(
		1220376, 4798000, STV091X_BLINDSCAN_SEARCH_RANGE_HZ);

	assert(next == 1253614);
}

static void modcod_classifies_dvb_s2_modulation_family(void)
{
	assert(stv091x_blindscan_modulation_from_modcod(1) ==
	       STV091X_BLINDSCAN_MOD_QPSK);
	assert(stv091x_blindscan_modulation_from_modcod(11) ==
	       STV091X_BLINDSCAN_MOD_QPSK);
	assert(stv091x_blindscan_modulation_from_modcod(12) ==
	       STV091X_BLINDSCAN_MOD_8PSK);
	assert(stv091x_blindscan_modulation_from_modcod(17) ==
	       STV091X_BLINDSCAN_MOD_8PSK);
	assert(stv091x_blindscan_modulation_from_modcod(18) ==
	       STV091X_BLINDSCAN_MOD_16APSK);
	assert(stv091x_blindscan_modulation_from_modcod(23) ==
	       STV091X_BLINDSCAN_MOD_16APSK);
	assert(stv091x_blindscan_modulation_from_modcod(24) ==
	       STV091X_BLINDSCAN_MOD_32APSK);
	assert(stv091x_blindscan_modulation_from_modcod(28) ==
	       STV091X_BLINDSCAN_MOD_32APSK);
	assert(stv091x_blindscan_modulation_from_modcod(0) ==
	       STV091X_BLINDSCAN_MOD_UNKNOWN);
	assert(stv091x_blindscan_modulation_from_modcod(29) ==
	       STV091X_BLINDSCAN_MOD_UNKNOWN);
}

int main(void)
{
	two_mhz_spectrum_grid_finds_carrier_valleys();
	flat_spectrum_has_no_candidates();
	broad_valley_is_detected_at_its_center();
	real_11597mhz_carrier_is_centered();
	adjacent_carriers_with_narrow_guard_are_both_detected();
	scan_constants_restore_wide_search_policy();
	candidate_acquisition_uses_configured_max_symbol_rate();
	implausibly_short_candidate_list_uses_fallback();
	first_failed_probe_advances_half_scan_span();
	later_failed_probe_advances_full_scan_span();
	lock_moves_next_window_past_carrier();
	modcod_classifies_dvb_s2_modulation_family();
	puts("stv091x candidate gate and fallback policy tests passed");
	return 0;
}
