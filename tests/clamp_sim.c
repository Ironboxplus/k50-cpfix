// SPDX-License-Identifier: GPL-2.0
/*
 * Host simulation of the vendor tune loop with the P7 clamp (PLAN, second revision).
 * cc -std=c11 -Wall -Wextra -Werror -I kernel -I tests tests/clamp_sim.c
 *
 * Source models (all mV, mA, mOhm):
 *   follow:  Vsrc = request; the charge pump and battery draw
 *            ibus = (Vsrc - ibus * R_path - 2 * Vbat) / (4 * R_bat)
 *   limited: as follow, but the source holds ibus at I_lim and its voltage
 *            sags onto the load line.
 * vbus is what the master pump measures: Vsrc - ibus * R_path.
 * Tune: request +100 mV while ibus is under the tune limit, -20 mV above
 * limit + 100, then the clamp request <= vbus + ibus * r / 1000.
 */
#include "check.h"
#include "cpfix_policy.h"

#define VBAT 4000
#define R_BAT 115
#define VBUS_LIMIT 11000
#define ROUND_MS 500
#define R_MAX 1000	/* cable_res_mohm default */
#define MARGIN 150	/* cable_margin_mohm default */

struct source {
	int r_path, i_lim;	/* i_lim 0: follows the request */
};

static void measure(const struct source *src, int req, int *vbus, int *ibus)
{
	int i = (req - 2 * VBAT) * 1000 / (4 * R_BAT + src->r_path);

	if (i < 0)
		i = 0;
	if (src->i_lim && i > src->i_lim) {
		i = src->i_lim;
		*vbus = 2 * VBAT + i * 4 * R_BAT / 1000;
	} else {
		*vbus = req - i * src->r_path / 1000;
	}
	*ibus = i;
}

/* clamp strategies */
struct strategy {
	const char *name;
	int static_r;	/* 0: learned (cpfix_clamp_step) */
	struct cpfix_clamp_state st;
};

static int strategy_r(struct strategy *s, int req, int vbus, int ibus, unsigned long t)
{
	if (s->static_r)
		return s->static_r;
	return cpfix_clamp_step(&s->st, req, vbus, ibus, t, R_MAX, MARGIN);
}

struct run {
	int req, vbus, ibus;
	unsigned long t;
	int max_ibus, reached, min_ibus_after, cuts_after;
	int max_excess;		/* max(req - vbus - ibus * (r_true + margin) / 1000) */
};

static void run_init(struct run *r, struct strategy *s)
{
	r->req = 2 * VBAT + 200;	/* entry request */
	r->t = 1000;
	r->max_ibus = r->reached = r->cuts_after = 0;
	r->min_ibus_after = 1 << 30;
	r->max_excess = -(1 << 30);
	cpfix_clamp_reset(&s->st);
}

/* One TUNE iteration; returns the vbus measured this round. */
static int round_once(struct run *r, struct strategy *s, const struct source *src, int tune_lim)
{
	int vbus, ibus, rr, bound, step, excess;

	measure(src, r->req, &vbus, &ibus);
	rr = strategy_r(s, r->req, vbus, ibus, r->t);
	bound = vbus + ibus * rr / 1000;
	if (r->req > bound) {
		r->req = bound;
		if (r->reached)
			r->cuts_after++;
	}
	step = ibus < tune_lim ? 100 : ibus > tune_lim + 100 ? -20 : 0;
	r->req += step;
	if (r->req > VBUS_LIMIT)
		r->req = VBUS_LIMIT;

	if (ibus > r->max_ibus)
		r->max_ibus = ibus;
	if (r->reached && ibus < r->min_ibus_after)
		r->min_ibus_after = ibus;
	if (ibus >= tune_lim)
		r->reached = 1;
	excess = r->req - vbus - ibus * (src->r_path + MARGIN) / 1000;
	if (excess > r->max_excess)
		r->max_excess = excess;
	r->vbus = vbus;
	r->ibus = ibus;
	r->t += ROUND_MS;
	return vbus;
}

/* A / B: following source; ibus must reach 1800 mA and the clamp must not cut it back. */
static int follow_ok(struct strategy *s, int r_path, int verbose)
{
	struct source src = { r_path, 0 };
	struct run r;
	int i, ok;

	run_init(&r, s);
	for (i = 0; i < 80; i++)
		round_once(&r, s, &src, 1800);
	ok = r.max_ibus >= 1800 && r.cuts_after == 0 && r.min_ibus_after >= 1700;
	if (verbose)
		printf("info follow R=%d %s: max_ibus=%d cuts_after=%d min_after=%d r_learned=%d -> %s\n",
		       r_path, s->name, r.max_ibus, r.cuts_after,
		       r.reached ? r.min_ibus_after : -1, s->st.r_learned, ok ? "ok" : "fails");
	return ok;
}

/* C: source limits at 2400 mA; D: it follows again after 60 rounds. */
static void limited(struct strategy *s, int *c_ok, int *d_ok, int verbose)
{
	struct source src = { 430, 2400 }, back = { 430, 0 };
	struct run r;
	int i, v_before, v_after, jump;

	run_init(&r, s);
	for (i = 0; i < 60; i++)
		round_once(&r, s, &src, 3000);
	*c_ok = r.max_excess <= 120;
	v_before = r.vbus;
	v_after = round_once(&r, s, &back, 3000);
	jump = v_after - v_before;
	*d_ok = jump <= 1500;
	if (verbose)
		printf("info limited %s: max(req - vbus - ibus*(r_true+margin)) = %d mV (limit 120), "
		       "r_learned=%d, recovery vbus %d -> %d (jump %d, limit 1500)\n",
		       s->name, r.max_excess, s->st.r_learned, v_before, v_after, jump);
}

int main(void)
{
	struct strategy learned = { .name = "learned" };
	struct strategy s350 = { .name = "static 350", .static_r = 350 };
	struct strategy s1000 = { .name = "static 1000", .static_r = 1000 };
	struct strategy s600 = { .name = "static 600", .static_r = 600 };
	int c, d;

	EQ(follow_ok(&learned, 430, 1), 1, "A: 0.43 Ohm, learned clamp reaches 1800 mA");
	EQ(follow_ok(&learned, 130, 1), 1, "B: 0.13 Ohm, learned clamp reaches 1800 mA");
	limited(&learned, &c, &d, 1);
	EQ(c, 1, "C: request excess bounded by ibus * (r_true + margin) + 120 mV");
	EQ(d, 1, "D: recovery vbus jump <= 1.5 V");

	/* negative controls: the static clamps must fail where the plan says */
	EQ(follow_ok(&s350, 430, 1), 0, "static 350 must fail on A");
	limited(&s1000, &c, &d, 1);
	EQ(c, 0, "static 1000 must fail on C");

	/* informational, no assertion: a 0.6 Ohm path and the interim 600 mOhm setting */
	follow_ok(&learned, 600, 1);
	follow_ok(&s600, 430, 1);
	limited(&s600, &c, &d, 1);

	return check_report();
}
