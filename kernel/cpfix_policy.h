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
	X(cable_res_mohm, 1000, 350, 1500, 0)

#define CPFIX_TEMP_MAX_AGE_MS 10000	/* older battery temperature counts as hot */
#define CPFIX_BAND_MIN_GAP 10		/* batt_temp_lo <= batt_temp_hi - 10 (0.1 degC) */
#define CPFIX_FCC_CAP_MIN 500
#define CPFIX_FCC_CAP_MAX 12400

/* STUB: interface only, decisions land in the next commit (test-first). */
static inline int cpfix_streq(const char *a, const char *b) { (void)a; (void)b; return 0; }
static inline int cpfix_clamp(int v, int lo, int hi) { (void)lo; (void)hi; return v; }
static inline int cpfix_clamp_param(int v, int min, int max, int zero_ok) { (void)min; (void)max; (void)zero_ok; return v; }
static inline int cpfix_high_soc(int param, int dt_high_soc) { (void)dt_high_soc; return param; }
static inline int cpfix_band_lo(int hi, int lo) { (void)hi; return lo; }
static inline int cpfix_thermal_next(int active, int temp, int hi, int lo) { (void)temp; (void)hi; (void)lo; return active; }
static inline int cpfix_temp_fresh(int valid, unsigned long age_ms) { (void)age_ms; return valid; }
static inline int cpfix_thermal_effective(int active, int fresh) { (void)fresh; return active; }
static inline int cpfix_vote_value(int is_fcc, const char *client, int enabled, int val,
				   int on, int thermal_active, int override_ma)
{ (void)is_fcc; (void)client; (void)enabled; (void)on; (void)thermal_active; (void)override_ma; return val; }
static inline int cpfix_cap_decide(int enable, int cap_ma, int applied, int *en, int *val)
{ (void)enable; (void)cap_ma; (void)applied; (void)en; (void)val; return 0; }

#endif /* CPFIX_POLICY_H */
