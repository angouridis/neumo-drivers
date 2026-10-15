#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void scan_constants_restore_wide_search_policy(void)
{
	assert(STV091X_BLINDSCAN_STEP_KHZ == 1000);
	assert(STV091X_BLINDSCAN_SEARCH_RANGE_HZ == 60000000);
	assert(STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ == 500000);
	assert(STV091X_BLINDSCAN_VENDOR_EDGE_SEARCH_RANGE_HZ == 500000);
	assert(STV091X_BLINDSCAN_VENDOR_SETTLE_MS == 10);
	assert(STV091X_BLINDSCAN_VENDOR_ACQUISITION_RANGE_HZ == 16000000);
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

static void vendor_phase_transitions_to_fallback_at_range_start(void)
{
	struct stv091x_blindscan_traversal traversal;

	stv091x_blindscan_traversal_init(&traversal, 950000);
	assert(traversal.phase == STV091X_BLINDSCAN_VENDOR);
	assert(traversal.vendor_frequency_khz == 950000);
	assert(stv091x_blindscan_transition_to_fallback(&traversal) ==
	       950000);
	assert(traversal.phase == STV091X_BLINDSCAN_FALLBACK);
	assert(traversal.fallback_frequency_khz == 950000);
}

static void vendor_miss_advances_by_acquisition_span(void)
{
	assert(stv091x_blindscan_vendor_failed_frequency(950000) == 966000);
}

static void vendor_edge_steps_match_windows_half_span(void)
{
	assert(stv091x_blindscan_vendor_edge_steps(72000000) == 48);
	assert(stv091x_blindscan_vendor_edge_steps(36000000) == 24);
	assert(stv091x_blindscan_vendor_miss_jump_khz(72000000) == 23000);
	assert(stv091x_blindscan_vendor_edge_jump_khz(20, 2, 0) == 10000);
	assert(stv091x_blindscan_vendor_edge_jump_khz(20, 1, 4) == 8500);
	assert(STV091X_BLINDSCAN_VENDOR_AGC2_RATIO == 40);
	assert(STV091X_BLINDSCAN_VENDOR_TUNER_BW_HZ == 72000000);
}

static void vendor_next_frequency_matches_windows_status_codes(void)
{
	/* asperity 0: bw/3000 - 1000 */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 0, 24000, 72000000, 0, 950000, 0) == 973000);
	/* asperity 1: skip acquire, full edge jump */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 1, 21500, 72000000, 0, 950000, 0) == 971500);
	/* asperity 2 lock fail: half edge jump */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 2, 20000, 72000000, 0, 970000, 0) == 960000);
	/* asperity 2 lock ok: past half occupied BW from lock */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 2, 2000, 72000000, 1, 1296234,
		       1000833) == 1296909);
}

static void vendor_acquisition_range_matches_windows_pad(void)
{
	assert(stv091x_blindscan_vendor_acquisition_range_hz(0) == 24000000);
	assert(stv091x_blindscan_vendor_acquisition_range_hz(2000) ==
	       26000000);
	assert(stv091x_blindscan_vendor_acquisition_range_hz(21500) ==
	       40000000);
}

static void vendor_lock_advances_by_half_occupied_bandwidth(void)
{
	assert(stv091x_blindscan_vendor_after_lock_frequency(
		       1220376, 4798000) == 1223614);
	assert(stv091x_blindscan_vendor_after_lock_frequency(
		       1100000, 1000000) == 1100675);
}

static void duplicate_suppression_preserves_nearby_one_msps_carriers(void)
{
	struct stv091x_blindscan_lock candidate_lock = {
		.frequency_khz = 1100000,
		.symbol_rate = 1000000,
	};

	assert(stv091x_blindscan_lock_is_duplicate(&candidate_lock, 1,
						  1100499, 1000000));
	assert(stv091x_blindscan_lock_is_duplicate(&candidate_lock, 1,
						  1100500, 1000000));
	assert(!stv091x_blindscan_lock_is_duplicate(&candidate_lock, 1,
						   1100501, 1000000));
}

static void duplicate_suppression_checks_all_candidate_locks(void)
{
	struct stv091x_blindscan_lock candidate_locks[] = {
		{ .frequency_khz = 1000000, .symbol_rate = 1000000 },
		{ .frequency_khz = 1200000, .symbol_rate = 4000000 },
	};

	assert(stv091x_blindscan_lock_is_duplicate(candidate_locks, 2,
						  1201500, 3000000));
	assert(!stv091x_blindscan_lock_is_duplicate(candidate_locks, 2,
						   1202001, 5000000));
}

static void duplicate_vendor_results_are_recorded_only_once(void)
{
	struct stv091x_blindscan_lock locks[3];
	u32 num_locks = 0;

	assert(stv091x_blindscan_add_unique_lock(
		       locks, &num_locks, 3, 1100000, 2000000) == 1);
	assert(num_locks == 1);
	assert(stv091x_blindscan_add_unique_lock(
		       locks, &num_locks, 3, 1100100, 2000000) == 0);
	assert(num_locks == 1);
	assert(stv091x_blindscan_add_unique_lock(
		       locks, &num_locks, 3, 1100601, 1000000) == 1);
	assert(num_locks == 2);
}

int main(void)
{
	scan_constants_restore_wide_search_policy();
	first_failed_probe_advances_half_scan_span();
	later_failed_probe_advances_full_scan_span();
	lock_moves_next_window_past_carrier();
	modcod_classifies_dvb_s2_modulation_family();
	vendor_phase_transitions_to_fallback_at_range_start();
	vendor_miss_advances_by_acquisition_span();
	vendor_edge_steps_match_windows_half_span();
	vendor_next_frequency_matches_windows_status_codes();
	vendor_acquisition_range_matches_windows_pad();
	vendor_lock_advances_by_half_occupied_bandwidth();
	duplicate_suppression_preserves_nearby_one_msps_carriers();
	duplicate_suppression_checks_all_candidate_locks();
	duplicate_vendor_results_are_recorded_only_once();
	puts("stv091x vendor blindscan policy tests passed");
	return 0;
}
