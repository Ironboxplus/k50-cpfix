/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Pure decision logic of cpfix, shared by the module and tests/policy_test.c.
 * No kernel or libc headers: plain C types only, every function static inline.
 */
#ifndef CPFIX_POLICY_H
#define CPFIX_POLICY_H

/*
 * Module parameters: X(name, default, min, max, zero_ok).
 * zero_ok: 0 is a valid "off" value below min (entry_high_soc 0 = DT value,
 * fcc_cap_ma 0 = no cap); anything <= 0 then maps to 0.
 */
#define CPFIX_PARAMS(X)                                \
	X(enable, 1, 0, 1, 0)                          \
	X(entry_vbat_margin_mv, 90, 0, 250, 0)         \
	X(entry_high_soc, 0, 50, 95, 1)                \
	X(fast_ramp, 1, 0, 1, 0)                       \
	X(taper_reduce_ma, 500, 100, 1500, 0)          \
	X(ffc_any_pps, 1, 0, 1, 0)                     \
	X(thermal_override, 1, 0, 1, 0)                \
	X(thermal_override_ma, 12400, 2000, 12400, 0)  \
	X(batt_temp_hi, 420, 300, 460, 0)              \
	X(batt_temp_lo, 400, 300, 460, 0)              \
	X(fcc_cap_ma, 0, 500, 12400, 1)                \
	X(clamp_headroom_mv, 400, 0, 800, 0)           \
	X(fv_drop_max_mv, 0, 0, 100, 0)                \
	X(fv_ceiling_mv, 4440, 4000, 4440, 0)

#define CPFIX_TEMP_MAX_AGE_MS 10000	/* older battery temperature counts as hot */
#define CPFIX_BAND_MIN_GAP 10		/* batt_temp_lo <= batt_temp_hi - 10 (0.1 degC) */
#define CPFIX_FCC_CAP_MIN 500
#define CPFIX_FCC_CAP_MAX 12400

/* P7 tune clamp */
#define CPFIX_R_STOCK 350		/* vendor MAX_CABLE_RESISTANCE (mOhm) */
#define CPFIX_CLAMP_MIN_IBUS 50		/* mA, divisor floor */
#define CPFIX_CLAMP_R_MAX 40000		/* mOhm: 6200 mA x 40000 < 2^31 in the vendor mul */

static inline int cpfix_streq(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static inline int cpfix_clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static inline int cpfix_clamp_param(int v, int min, int max, int zero_ok)
{
	if (zero_ok && v <= 0)
		return 0;
	return cpfix_clamp(v, min, max);
}

/* SOC ceiling for entering the charge pump: the parameter, or the DT high_soc when 0. */
static inline int cpfix_high_soc(int param, int dt_high_soc)
{
	return param ? param : dt_high_soc;
}

/* Effective lower threshold: at least CPFIX_BAND_MIN_GAP below hi, whatever order they were set in. */
static inline int cpfix_band_lo(int hi, int lo)
{
	return lo > hi - CPFIX_BAND_MIN_GAP ? hi - CPFIX_BAND_MIN_GAP : lo;
}

/* Thermal override hysteresis: off at temp >= hi, on at temp <= lo, else unchanged. */
static inline int cpfix_thermal_next(int active, int temp, int hi, int lo)
{
	if (temp >= hi)
		return 0;
	if (temp <= cpfix_band_lo(hi, lo))
		return 1;
	return active;
}

static inline int cpfix_temp_fresh(int valid, unsigned long age_ms)
{
	return valid && age_ms <= CPFIX_TEMP_MAX_AGE_MS;
}

static inline int cpfix_thermal_effective(int active, int fresh)
{
	return active && fresh;
}

/*
 * Value a vote() call should carry. Only an enabled THERMAL_VOTER vote on the
 * FCC votable is touched, and only raised to override_ma, never lowered.
 * on = enable && thermal_override; thermal_active already includes freshness.
 */
static inline int cpfix_vote_value(int is_fcc, const char *client, int enabled, int val,
				   int on, int thermal_active, int override_ma)
{
	if (!is_fcc || !enabled || !on || !thermal_active)
		return val;
	if (!cpfix_streq(client, "THERMAL_VOTER"))
		return val;
	return val > override_ma ? val : override_ma;
}

/*
 * User FCC cap. applied is the value currently voted by CPFIX_USER_VOTER
 * (0 = no vote). Returns 1 when a vote must be cast, described by *en / *val.
 */
static inline int cpfix_cap_decide(int enable, int cap_ma, int applied, int *en, int *val)
{
	int want = enable && cap_ma > 0 ?
		cpfix_clamp(cap_ma, CPFIX_FCC_CAP_MIN, CPFIX_FCC_CAP_MAX) : 0;

	if (want == applied)
		return 0;
	*en = want != 0;
	*val = want;
	return 1;
}

/*
 * P8: subtrahend of the three fv votes (diff_fv_val, the cycle-count drop),
 * limited to drop_max. A smaller original value is kept.
 */
static inline int cpfix_fv_drop(int orig, int drop_max)
{
	return orig < drop_max ? orig : drop_max;
}

/* P6 on CHARGER_FV: no enabled voltage vote above ceiling_mv. */
static inline int cpfix_fv_vote_value(int is_fv, int enabled, int val, int on, int ceiling_mv)
{
	if (!is_fv || !enabled || !on)
		return val;
	return val < ceiling_mv ? val : ceiling_mv;
}

/*
 * P7 v3: resistance (mOhm) for the vendor tune clamp
 *   request <= master_cp_vbus + total_ibus * r / 1000
 * chosen so that the bound becomes vbus + ibus * 350 / 1000 + headroom_mv:
 * the stock clamp plus a fixed voltage headroom, independent of the current.
 * headroom_mv 0 is the stock clamp.
 */
static inline int cpfix_clamp_r(int ibus, int headroom_mv)
{
	int i = ibus > CPFIX_CLAMP_MIN_IBUS ? ibus : CPFIX_CLAMP_MIN_IBUS;

	return cpfix_clamp(CPFIX_R_STOCK + headroom_mv * 1000 / i, CPFIX_R_STOCK, CPFIX_CLAMP_R_MAX);
}

/* P10: input current (uA) of the HVCHG (QC2 9 V) branch of do_algorithm, stock 1600 mA. */
static inline int cpfix_hvchg_icl_ua(int icl_ma)
{
	(void)icl_ma;
	return 1600000;	/* stub until the tests have failed once */
}

#endif /* CPFIX_POLICY_H */
