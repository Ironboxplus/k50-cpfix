// SPDX-License-Identifier: GPL-2.0
/* Host unit test for kernel/cpfix_policy.h: cc -std=c11 -Wall -Werror -I kernel tests/policy_test.c */
#include <stdio.h>

#include "cpfix_policy.h"

static int passed, failed;

#define EQ(got, want, ...)                                                   \
	do {                                                                 \
		long long g_ = (got), w_ = (want);                           \
		if (g_ == w_) {                                              \
			passed++;                                            \
		} else {                                                     \
			failed++;                                            \
			printf("FAIL %s:%d: %s = %lld, want %lld  (", __FILE__, \
			       __LINE__, #got, g_, w_);                      \
			printf(__VA_ARGS__);                                 \
			printf(")\n");                                       \
		}                                                            \
	} while (0)

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
	/* Ranges and defaults from PLAN §2.3 and the P7 addendum. */
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

int main(void)
{
	test_param_table();
	test_clamp_boundaries();
	test_high_soc();
	test_hysteresis();
	test_staleness();
	test_vote_rewrite();
	test_fcc_cap();
	printf("passed %d failed %d total %d\n", passed, failed, passed + failed);
	return failed ? 1 : 0;
}
