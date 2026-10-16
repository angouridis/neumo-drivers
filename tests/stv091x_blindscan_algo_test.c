#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void scan_constants_restore_wide_search_policy(void)
{
	assert(STV091X_BLINDSCAN_STEP_KHZ == 1000);
	assert(STV091X_BLINDSCAN_SEARCH_RANGE_HZ == 60000000);
	assert(STV091X_BLINDSCAN_VENDOR_DEMOD_TIMEOUT_MS == 500);
	assert(STV091X_BLINDSCAN_VENDOR_FEC_TIMEOUT_MS == 50);
	assert(STV091X_BLINDSCAN_LOCK_TIMEOUT_MS == 550);
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
	assert(STV091X_BLINDSCAN_VENDOR_POST_LOCK_TUNER_BW_HZ == 48000000);
	assert(STV091X_BLINDSCAN_VENDOR_AFTER_LOCK_SR_QUANT_HZ == 200000);
}

static void vendor_next_frequency_matches_windows_status_codes(void)
{
	/* asperity 0: bw/3000 - 1000 */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 0, 24000, 72000000, 0, 950000, 0) == 973000);
	/* asperity 1: skip acquire, full edge jump */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 1, 21500, 72000000, 0, 950000, 0) == 971500);
	/* asperity 2 lock fail phase 0: half edge jump */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 2, 20000, 72000000, 0, 970000, 0) == 960000);
	/* asperity 2 lock ok: past half occupied BW from lock */
	assert(stv091x_blindscan_vendor_next_frequency(
		       950000, 2, 2000, 72000000, 1, 1296234,
		       1000833) == 1296909);
}

static void vendor_fail_zigzag_skips_edge_and_preserves_jump(void)
{
	struct stv091x_blindscan_traversal trav;

	stv091x_blindscan_traversal_init(&trav, 950000);
	assert(trav.probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_EDGE);

	/* First asperity-2 fail: store J/2, arm mode-2 acquire */
	stv091x_blindscan_vendor_apply_result(
		&trav, 2, 500, 72000000, 0, 950000, 0);
	assert(trav.vendor_frequency_khz == 950250);
	assert(trav.stored_jump_khz == 250);
	assert(trav.fail_phase == 1);
	assert(trav.probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_ACQUIRE);

	/* tbs6903.sys phase-1 fail: advance by exact stored jump */
	stv091x_blindscan_vendor_apply_result(
		&trav, 2, trav.stored_jump_khz, 72000000, 0,
		trav.vendor_frequency_khz, 0);
	assert(trav.vendor_frequency_khz == 950500);
	assert(trav.fail_phase == 0);
	assert(trav.probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_EDGE);
}

static void vendor_medium_jump_fail_keeps_windows_half_advance(void)
{
	struct stv091x_blindscan_traversal trav;

	stv091x_blindscan_traversal_init(&trav, 950000);
	stv091x_blindscan_vendor_apply_result(
		&trav, 2, 8000, 72000000, 0, 950000, 0);
	assert(trav.vendor_frequency_khz == 954000);
	assert(trav.stored_jump_khz == 4000);
	assert(trav.fail_phase == 1);
	stv091x_blindscan_vendor_apply_result(
		&trav, 2, trav.stored_jump_khz, 72000000, 0,
		trav.vendor_frequency_khz, 0);
	assert(trav.vendor_frequency_khz == 958000);
	assert(trav.probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_EDGE);
}

static void vendor_asperity1_arms_sticky_mode2_acquire(void)
{
	struct stv091x_blindscan_traversal trav;

	stv091x_blindscan_traversal_init(&trav, 950000);
	stv091x_blindscan_vendor_apply_result(
		&trav, 1, 21500, 72000000, 0, 950000, 0);
	assert(trav.vendor_frequency_khz == 971500);
	assert(trav.stored_jump_khz ==
	       STV091X_BLINDSCAN_VENDOR_STICKY_JUMP_KHZ);
	assert(trav.fail_phase == 2);
	assert(trav.probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_ACQUIRE);

	/* tbs6903.sys phase-2 fail after asperity-1: no advance, back to edge */
	stv091x_blindscan_vendor_apply_result(
		&trav, 2, trav.stored_jump_khz, 72000000, 0,
		trav.vendor_frequency_khz, 0);
	assert(trav.vendor_frequency_khz == 971500);
	assert(trav.fail_phase == 0);
	assert(trav.probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_EDGE);
}

static void vendor_phase2_fail_does_not_skip_midband_cluster(void)
{
	struct stv091x_blindscan_traversal trav;
	s32 freq;
	int i;

	/*
	 * Reproducing the 11228–11475 H hole: asperity-1 → mode2 fail must
	 * not accumulate +2/+23 MHz hops that leap past real carriers.
	 */
	stv091x_blindscan_traversal_init(&trav, 1119000);
	freq = 1119000;
	for (i = 0; i < 5; i++) {
		stv091x_blindscan_vendor_apply_result(
			&trav, 1, 5000, 72000000, 0, freq, 0);
		freq = trav.vendor_frequency_khz;
		stv091x_blindscan_vendor_apply_result(
			&trav, 2, trav.stored_jump_khz, 72000000, 0, freq, 0);
		assert(trav.vendor_frequency_khz == freq);
		assert(trav.probe_mode ==
		       STV091X_BLINDSCAN_VENDOR_PROBE_EDGE);
		freq = trav.vendor_frequency_khz;
	}
	/* Only asperity-1 advances (5 * 5000), never miss-sized skips */
	assert(trav.vendor_frequency_khz == 1119000 + 5 * 5000);
}

static void vendor_acquisition_range_matches_windows_pad(void)
{
	assert(stv091x_blindscan_vendor_acquisition_range_hz(0) == 24000000);
	assert(stv091x_blindscan_vendor_acquisition_range_hz(2000) ==
	       26000000);
	assert(stv091x_blindscan_vendor_acquisition_range_hz(500) ==
	       24500000);
	assert(stv091x_blindscan_vendor_acquisition_range_hz(21500) ==
	       40000000);
}

static void vendor_acquire_lo_shifts_when_jump_exceeds_half_range(void)
{
	/* jump 16M < half of 40M → stay at center */
	assert(stv091x_blindscan_vendor_acquire_frequency_khz(
		       1119000, 16000) == 1119000);
	/* jump 24M, range capped 40M, half=20M → shift +4M */
	assert(stv091x_blindscan_vendor_acquire_frequency_khz(
		       1119000, 24000) == 1123000);
	/* jump 22M → shift +2M so edge sits on CFRUP */
	assert(stv091x_blindscan_vendor_acquire_frequency_khz(
		       1119000, 22000) == 1121000);
}

static void vendor_lock_advances_by_half_occupied_bandwidth(void)
{
	/* 135 * floor(4798000/200000) = 135*23 = 3105 */
	assert(stv091x_blindscan_vendor_after_lock_frequency(
		       1220376, 4798000) == 1223481);
	assert(stv091x_blindscan_vendor_after_lock_frequency(
		       1100000, 1000000) == 1100675);
	/* 27.5 Msps: 135 * floor(27500000/200000) = 135*137 */
	assert(stv091x_blindscan_vendor_after_lock_frequency(
		       1100000, 27500000) == 1100000 + 135 * 137);
	/* Near-prior duplicate path: scan_center += SR_kHz (floored at 1000) */
	assert(stv091x_blindscan_vendor_duplicate_frequency(
		       1100000, 1000000) == 1101000);
	assert(stv091x_blindscan_vendor_duplicate_frequency(
		       1100000, 25000000) == 1125000);
	/* Accumulates from scan center, not lock freq — avoids F+1000 spin */
	assert(stv091x_blindscan_vendor_duplicate_frequency(
		       1100675, 1000000) == 1101675);
}

static void duplicate_suppression_preserves_nearby_one_msps_carriers(void)
{
	struct stv091x_blindscan_lock candidate_lock = {
		.frequency_khz = 1100000,
		.symbol_rate = 1000000,
	};

	/* tbs6903.sys: duplicate only when |Δf| < 1000 (strict) */
	assert(stv091x_blindscan_lock_is_duplicate(&candidate_lock, 1,
						  1100599, 1000000));
	assert(stv091x_blindscan_lock_is_duplicate(&candidate_lock, 1,
						  1100999, 1000000));
	assert(!stv091x_blindscan_lock_is_duplicate(&candidate_lock, 1,
						   1101000, 1000000));
}

static void duplicate_suppression_checks_all_candidate_locks(void)
{
	struct stv091x_blindscan_lock candidate_locks[] = {
		{ .frequency_khz = 1000000, .symbol_rate = 1000000 },
		{ .frequency_khz = 1200000, .symbol_rate = 4000000 },
	};
	struct stv091x_blindscan_lock wide_lock = {
		.frequency_khz = 1200000,
		.symbol_rate = 30000000,
	};

	assert(stv091x_blindscan_lock_is_duplicate(candidate_locks, 2,
						  1200500, 3000000));
	assert(!stv091x_blindscan_lock_is_duplicate(candidate_locks, 2,
						   1201000, 5000000));
	/*
	 * Must not use SR/2000: a 5 MHz neighbour of a 30 Msps carrier is a
	 * distinct TP (Windows absolute 1 MHz window).
	 */
	assert(!stv091x_blindscan_lock_is_duplicate(&wide_lock, 1,
						   1205000, 5000000));
}

static void vendor_edge_false_alarm_resets_when_samples_is_one(void)
{
	int asperity = 0;
	int waiting = 0;
	s32 edge_samples = 0;
	int refill = 0;

	/* Arm asperity-1 at min; same step bumps edge_samples to 1 */
	assert(!stv091x_blindscan_vendor_edge_on_ratio(
		50, 1, &asperity, &waiting, &edge_samples, &refill));
	assert(asperity == 1);
	assert(waiting == 1);
	assert(edge_samples == 1);
	assert(refill == 1);

	/* Next high ratio off-min with samples==1: Windows false-alarm reset */
	refill = 0;
	assert(!stv091x_blindscan_vendor_edge_on_ratio(
		50, 0, &asperity, &waiting, &edge_samples, &refill));
	assert(asperity == 0);
	assert(waiting == 0);
	assert(edge_samples == 0);
	assert(refill == 1);

	/* Real asperity-2: high ratio, not waiting, samples==0 → stop */
	refill = 0;
	assert(stv091x_blindscan_vendor_edge_on_ratio(
		50, 0, &asperity, &waiting, &edge_samples, &refill));
	assert(asperity == 2);
	assert(refill == 0);

	/* Real asperity-1 after waiting past samples==1 */
	asperity = 0;
	waiting = 0;
	edge_samples = 0;
	refill = 0;
	assert(!stv091x_blindscan_vendor_edge_on_ratio(
		50, 1, &asperity, &waiting, &edge_samples, &refill));
	assert(edge_samples == 1);
	/* Quiet step while waiting: samples → 2 */
	assert(!stv091x_blindscan_vendor_edge_on_ratio(
		10, 0, &asperity, &waiting, &edge_samples, &refill));
	assert(edge_samples == 2);
	assert(waiting == 1);
	/* High ratio off-min with samples!=1 → stop asperity 1 */
	assert(stv091x_blindscan_vendor_edge_on_ratio(
		50, 0, &asperity, &waiting, &edge_samples, &refill));
	assert(asperity == 1);
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
		       locks, &num_locks, 3, 1101000, 1000000) == 1);
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
	vendor_fail_zigzag_skips_edge_and_preserves_jump();
	vendor_medium_jump_fail_keeps_windows_half_advance();
	vendor_asperity1_arms_sticky_mode2_acquire();
	vendor_phase2_fail_does_not_skip_midband_cluster();
	vendor_acquisition_range_matches_windows_pad();
	vendor_acquire_lo_shifts_when_jump_exceeds_half_range();
	vendor_lock_advances_by_half_occupied_bandwidth();
	duplicate_suppression_preserves_nearby_one_msps_carriers();
	duplicate_suppression_checks_all_candidate_locks();
	duplicate_vendor_results_are_recorded_only_once();
	vendor_edge_false_alarm_resets_when_samples_is_one();
	puts("stv091x vendor blindscan policy tests passed");
	return 0;
}
