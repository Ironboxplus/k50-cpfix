// SPDX-License-Identifier: GPL-2.0
/* Host unit test for kernel/cpfix_policy.h: cc -std=c11 -Wall -Werror -I kernel -I tests tests/policy_test.c */
#include "check.h"
#include "cpfix_policy.h"

struct param {
	const char *name;
	int def, min, max, zero_ok;
};

static const struct param params[] = {
#define X(name, def, min, max, zero_ok) { #name, def, min, max, zero_ok },
	CPFIX_PARAMS(X)
#undef X
};

static void test_param_table(void)
{
	/* Ranges and defaults from PLAN §2.3, the P7 addendum and the second revision. */
	static const struct param want[] = {
		{ "enable", 1, 0, 1, 0 },
		{ "entry_vbat_margin_mv", 90, 0, 250, 0 },
		{ "entry_high_soc", 0, 50, 95, 1 },
		{ "fast_ramp", 1, 0, 1, 0 },
		{ "taper_reduce_ma", 500, 100, 1500, 0 },
		{ "ffc_any_pps", 1, 0, 1, 0 },
		{ "thermal_override", 1, 0, 1, 0 },
		{ "thermal_override_ma", 12400, 2000, 12400, 0 },
		{ "batt_temp_hi", 420, 300, 460, 0 },
		{ "batt_temp_lo", 400, 300, 460, 0 },
		{ "fcc_cap_ma", 0, 500, 12400, 1 },
		{ "cable_res_mohm", 1000, 350, 1500, 0 },
		{ "cable_margin_mohm", 150, 50, 400, 0 },
		{ "fv_drop_max_mv", 0, 0, 100, 0 },
		{ "fv_ceiling_mv", 4440, 4000, 4440, 0 },
	};
	unsigned int i, j, n = sizeof(params) / sizeof(params[0]);

	EQ(n, sizeof(want) / sizeof(want[0]), "parameter count");
	for (i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		const struct param *p = 0;

		for (j = 0; j < n; j++)
			if (cpfix_streq(params[j].name, want[i].name))
				p = &params[j];
		EQ(p != 0, 1, "%s present", want[i].name);
		if (!p)
			continue;
		EQ(p->def, want[i].def, "%s default", p->name);
		EQ(p->min, want[i].min, "%s min", p->name);
		EQ(p->max, want[i].max, "%s max", p->name);
		EQ(p->zero_ok, want[i].zero_ok, "%s zero_ok", p->name);
		/* the default must survive its own clamp */
		EQ(cpfix_clamp_param(p->def, p->min, p->max, p->zero_ok), p->def, "%s default clamp", p->name);
	}
}

static void test_clamp_boundaries(void)
{
	unsigned int i;

	for (i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
		const struct param *p = &params[i];
		int lo = p->zero_ok ? 0 : p->min;

		EQ(cpfix_clamp_param(-1, p->min, p->max, p->zero_ok), lo, "%s -1", p->name);
		EQ(cpfix_clamp_param(0, p->min, p->max, p->zero_ok), lo, "%s 0", p->name);
		EQ(cpfix_clamp_param(p->min, p->min, p->max, p->zero_ok), p->min, "%s min", p->name);
		EQ(cpfix_clamp_param(p->max, p->min, p->max, p->zero_ok), p->max, "%s max", p->name);
		EQ(cpfix_clamp_param(p->max + 1, p->min, p->max, p->zero_ok), p->max, "%s max+1", p->name);
		if (p->min > 1)
			EQ(cpfix_clamp_param(p->min - 1, p->min, p->max, p->zero_ok), p->min, "%s min-1", p->name);
	}
	/* spot checks with literal values */
	EQ(cpfix_clamp_param(-1, 500, 12400, 1), 0, "fcc_cap -1");
	EQ(cpfix_clamp_param(1, 500, 12400, 1), 500, "fcc_cap 1");
	EQ(cpfix_clamp_param(12401, 500, 12400, 1), 12400, "fcc_cap 12401");
	EQ(cpfix_clamp_param(349, 350, 1500, 0), 350, "cable_res 349");
	EQ(cpfix_clamp_param(0, 350, 1500, 0), 350, "cable_res 0");
	EQ(cpfix_clamp_param(1501, 350, 1500, 0), 1500, "cable_res 1501");
	EQ(cpfix_clamp_param(251, 0, 250, 0), 250, "margin 251");
	EQ(cpfix_clamp_param(49, 50, 95, 1), 50, "high_soc 49");
	EQ(cpfix_clamp_param(96, 50, 95, 1), 95, "high_soc 96");
	EQ(cpfix_clamp_param(49, 50, 400, 0), 50, "cable_margin 49");
	EQ(cpfix_clamp_param(401, 50, 400, 0), 400, "cable_margin 401");
	EQ(cpfix_clamp_param(-1, 0, 100, 0), 0, "fv_drop -1");
	EQ(cpfix_clamp_param(101, 0, 100, 0), 100, "fv_drop 101");
	EQ(cpfix_clamp_param(3999, 4000, 4440, 0), 4000, "fv_ceiling 3999");
	EQ(cpfix_clamp_param(4441, 4000, 4440, 0), 4440, "fv_ceiling 4441");
	EQ(cpfix_clamp_param(4500, 4000, 4440, 0), 4440, "fv_ceiling never above DT fv");
}

static void test_high_soc(void)
{
	EQ(cpfix_high_soc(0, 88), 88, "0 -> DT");
	EQ(cpfix_high_soc(70, 88), 70, "param wins");
	EQ(cpfix_high_soc(95, 88), 95, "param above DT");
}

static void test_hysteresis(void)
{
	/* 419 -> 420 -> 410 -> 400 -> 399 with hi 420 / lo 400, starting active */
	static const int temps[] = { 419, 420, 410, 400, 399 };
	static const int want[] = { 1, 0, 0, 1, 1 };
	int i, a = 1;

	for (i = 0; i < 5; i++) {
		a = cpfix_thermal_next(a, temps[i], 420, 400);
		EQ(a, want[i], "step %d temp %d", i, temps[i]);
	}
	/* starting inactive inside the band stays inactive until lo */
	EQ(cpfix_thermal_next(0, 410, 420, 400), 0, "inactive in band");
	EQ(cpfix_thermal_next(0, 300, 420, 400), 1, "cool turns on");
	/* lo is kept at least 10 below hi: hi 420, lo 415 acts as lo 410 */
	EQ(cpfix_band_lo(420, 415), 410, "lo pulled below hi-10");
	EQ(cpfix_band_lo(420, 400), 400, "lo kept");
	EQ(cpfix_thermal_next(0, 412, 420, 415), 0, "412 above effective lo 410");
	EQ(cpfix_thermal_next(0, 410, 420, 415), 1, "410 at effective lo");
	EQ(cpfix_thermal_next(1, 460, 460, 300), 0, "max hi");
}

static void test_staleness(void)
{
	EQ(cpfix_temp_fresh(1, 9900), 1, "9.9 s fresh");
	EQ(cpfix_temp_fresh(1, 10000), 1, "10.0 s fresh");
	EQ(cpfix_temp_fresh(1, 10100), 0, "10.1 s stale");
	EQ(cpfix_temp_fresh(0, 0), 0, "never read");
	EQ(cpfix_thermal_effective(1, 1), 1, "active and fresh");
	EQ(cpfix_thermal_effective(1, 0), 0, "stale cache counts as hot");
	EQ(cpfix_thermal_effective(0, 1), 0, "inactive");
}

static void test_vote_rewrite(void)
{
	const int ov = 12400;

	/* the only case that changes */
	EQ(cpfix_vote_value(1, "THERMAL_VOTER", 1, 3000, 1, 1, ov), 12400, "thermal fcc vote raised");
	/* raise only, never lower */
	EQ(cpfix_vote_value(1, "THERMAL_VOTER", 1, 3000, 1, 1, 2000), 3000, "override below vote");
	/* any missing condition leaves the value alone */
	EQ(cpfix_vote_value(0, "THERMAL_VOTER", 1, 3000, 1, 1, ov), 3000, "not fcc votable");
	EQ(cpfix_vote_value(1, "STEP_CHARGE_VOTER", 1, 3000, 1, 1, ov), 3000, "other client");
	EQ(cpfix_vote_value(1, "THERMAL_VOTER_X", 1, 3000, 1, 1, ov), 3000, "prefix client");
	EQ(cpfix_vote_value(1, "THERMAL_VOTE", 1, 3000, 1, 1, ov), 3000, "short client");
	EQ(cpfix_vote_value(1, 0, 1, 3000, 1, 1, ov), 3000, "null client");
	EQ(cpfix_vote_value(1, "THERMAL_VOTER", 0, 3000, 1, 1, ov), 3000, "vote disabled");
	EQ(cpfix_vote_value(1, "THERMAL_VOTER", 1, 3000, 0, 1, ov), 3000, "override off");
	EQ(cpfix_vote_value(1, "THERMAL_VOTER", 1, 3000, 1, 0, ov), 3000, "battery hot");
}

static void test_fcc_cap(void)
{
	int en = -1, val = -1;

	EQ(cpfix_cap_decide(1, 0, 0, &en, &val), 0, "0 with nothing voted: no vote");
	EQ(cpfix_cap_decide(1, 500, 0, &en, &val), 1, "500 votes");
	EQ(en, 1, "500 enabled");
	EQ(val, 500, "500 value");
	EQ(cpfix_cap_decide(1, 500, 500, &en, &val), 0, "unchanged: no vote");
	en = val = -1;
	EQ(cpfix_cap_decide(1, 0, 500, &en, &val), 1, "0 after 500 revokes");
	EQ(en, 0, "revoke disabled");
	EQ(val, 0, "revoke value");
	EQ(cpfix_cap_decide(1, 499, 0, &en, &val), 1, "499 votes");
	EQ(val, 500, "499 clamps to 500");
	EQ(cpfix_cap_decide(1, 20000, 0, &en, &val), 1, "20000 votes");
	EQ(val, 12400, "20000 clamps to 12400");
	en = val = -1;
	EQ(cpfix_cap_decide(0, 3000, 3000, &en, &val), 1, "enable=0 revokes");
	EQ(en, 0, "enable=0 disabled");
	EQ(cpfix_cap_decide(0, 3000, 0, &en, &val), 0, "enable=0 nothing voted");
}

static void test_fv(void)
{
	/* P8: diff_fv_val 100 limited to fv_drop_max_mv */
	EQ(cpfix_fv_drop(100, 0), 0, "drop removed");
	EQ(cpfix_fv_drop(100, 50), 50, "drop limited");
	EQ(cpfix_fv_drop(30, 50), 30, "smaller drop kept");
	EQ(cpfix_fv_drop(100, 100), 100, "max = stock");
	EQ(cpfix_fv_drop(0, 0), 0, "no drop");
	/* P6 on CHARGER_FV */
	EQ(cpfix_fv_vote_value(1, 1, 4450, 1, 4440), 4440, "JEITA 4450 capped");
	EQ(cpfix_fv_vote_value(1, 1, 4470, 1, 4440), 4440, "FFC 4440+30 compensation capped");
	EQ(cpfix_fv_vote_value(1, 1, 4340, 1, 4440), 4340, "lower vote untouched");
	EQ(cpfix_fv_vote_value(1, 1, 4440, 1, 4440), 4440, "equal");
	EQ(cpfix_fv_vote_value(1, 1, 4450, 1, 4000), 4000, "min ceiling");
	EQ(cpfix_fv_vote_value(0, 1, 4450, 1, 4440), 4450, "not the fv votable");
	EQ(cpfix_fv_vote_value(1, 0, 4450, 1, 4440), 4450, "disabled vote");
	EQ(cpfix_fv_vote_value(1, 1, 4450, 0, 4440), 4450, "cpfix off");
}

static void test_clamp_step(void)
{
	struct cpfix_clamp_state s;
	int r, i;

	/*
	 * Each request below stays within the previous bound
	 * (vbus + ibus * r_eff / 1000 + 20) unless a test wants a clamped round.
	 */
	cpfix_clamp_reset(&s);
	/* no sample yet: stock + margin */
	EQ(cpfix_clamp_step(&s, 8200, 8200, 0, 1000, 1000, 150), 500, "first hit");
	/* ibus below 500 mA: no learning */
	EQ(cpfix_clamp_step(&s, 8210, 8150, 400, 1500, 1000, 150), 500, "low ibus");
	EQ(s.n, 0, "nothing learned below 500 mA");
	/* gap 430 mV at 1000 mA: 430 mOhm learned, r_eff 580 */
	EQ(cpfix_clamp_step(&s, 8360, 7930, 1000, 2000, 1000, 150), 580, "learn 430");
	EQ(s.r_learned, 430, "r_learned");
	/* a larger sample does not raise the minimum */
	EQ(cpfix_clamp_step(&s, 8530, 7930, 1000, 2500, 1000, 150), 580, "larger sample");
	EQ(s.n, 2, "two samples");

	/* below stock: floor at 350 */
	cpfix_clamp_reset(&s);
	cpfix_clamp_step(&s, 8000, 8000, 0, 0, 1000, 150);
	EQ(cpfix_clamp_step(&s, 8020, 7890, 1000, 500, 1000, 150), 500, "130 mOhm floors at stock");
	/* hard upper bound cable_res_mohm */
	EQ(cpfix_clamp_step(&s, 8130, 8000, 1000, 1000, 400, 150), 400, "capped by cable_res");
	/* r_max at stock returns stock */
	EQ(cpfix_clamp_step(&s, 8130, 8000, 1000, 1500, 350, 400), 350, "cap at stock");

	/* a clamped round is not learned: last bound 7590 + 1000 * 580 / 1000 = 8170 */
	cpfix_clamp_reset(&s);
	cpfix_clamp_step(&s, 8000, 8000, 0, 0, 1500, 150);
	cpfix_clamp_step(&s, 8020, 7590, 1000, 500, 1500, 150);	/* learns 430 -> 580 */
	EQ(s.n, 1, "one sample");
	r = cpfix_clamp_step(&s, 8170 + 21, 7590, 1000, 1000, 1500, 150);
	EQ(s.n, 1, "request over last bound + 20: not sampled");
	EQ(r, 580, "r_eff unchanged");
	cpfix_clamp_step(&s, 8170 + 20, 7590, 1000, 1500, 1500, 150);
	EQ(s.n, 2, "within the 20 mV slack: sampled");

	/*
	 * P7 v2 revision: a clamped round is still learned when ibus rose by >= 30 mA
	 * and vbus by >= 10 mV since the last round (the source follows the request).
	 */
	cpfix_clamp_reset(&s);
	cpfix_clamp_step(&s, 8000, 8000, 0, 0, 1500, 150);
	EQ(cpfix_clamp_step(&s, 8020, 7700, 600, 500, 1500, 150), 683, "learn 533");
	/* bound 7700 + 600 * 683 / 1000 + 20 = 8129 */
	cpfix_clamp_step(&s, 8300, 7710, 630, 1000, 1500, 150);
	EQ(s.n, 2, "clamped, ibus +30 and vbus +10: sampled");
	EQ(s.r_learned, 533, "minimum kept");
	cpfix_clamp_step(&s, 8400, 7719, 700, 1500, 1500, 150);
	EQ(s.n, 2, "clamped, vbus +9: not sampled");
	cpfix_clamp_step(&s, 8400, 7740, 729, 2000, 1500, 150);
	EQ(s.n, 2, "clamped, ibus +29: not sampled");
	cpfix_clamp_step(&s, 8400, 7750, 759, 2500, 1500, 150);
	EQ(s.n, 3, "clamped, ibus +30 and vbus +10 again: sampled");
	cpfix_clamp_step(&s, 8400, 7740, 800, 3000, 1500, 150);
	EQ(s.n, 3, "clamped, vbus fell: not sampled");

	/* window: minimum of the last 8 samples, older ones age out (margin 400 keeps headroom) */
	cpfix_clamp_reset(&s);
	EQ(cpfix_clamp_step(&s, 8000, 8000, 0, 0, 1500, 400), 750, "stock + 400");
	cpfix_clamp_step(&s, 8020, 7620, 1000, 100, 1500, 400);	/* 400 */
	EQ(s.r_learned, 400, "first sample 400");
	for (i = 0; i < 7; i++)
		cpfix_clamp_step(&s, 8040, 7440, 1000, 200 + 100 * i, 1500, 400);	/* 600 */
	EQ(s.n, 8, "window full");
	EQ(s.r_learned, 400, "400 still in the window");
	cpfix_clamp_step(&s, 8040, 7440, 1000, 1000, 1500, 400);
	EQ(s.n, 8, "window stays at 8");
	EQ(s.r_learned, 600, "400 aged out");

	/* leaving TUNE: more than 5000 ms since the last hit resets the learning */
	EQ(cpfix_clamp_step(&s, 8000, 8000, 0, 1000 + 5000, 1500, 400), 1000, "5000 ms: kept");
	EQ(cpfix_clamp_step(&s, 8000, 8000, 0, 6000 + 5001, 1500, 400), 750, "5001 ms: reset");
	EQ(s.n, 0, "samples cleared");
	/* request not above vbus: nothing to learn */
	EQ(cpfix_clamp_step(&s, 8000, 8100, 1000, 11500, 1500, 400), 750, "negative gap");
	EQ(s.n, 0, "no sample from negative gap");
}

int main(void)
{
	test_param_table();
	test_clamp_boundaries();
	test_high_soc();
	test_hysteresis();
	test_staleness();
	test_vote_rewrite();
	test_fcc_cap();
	test_fv();
	test_clamp_step();
	return check_report();
}
