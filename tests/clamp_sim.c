// SPDX-License-Identifier: GPL-2.0
/*
 * Host simulation of the vendor tune loop with the P7 clamp (PLAN, P7 v2 and its revision).
 * cc -std=c11 -Wall -Wextra -Werror -I kernel -I tests tests/clamp_sim.c
 *
 * Source models (all mV, mA, mOhm):
 *   follow:  Vsrc = request; the charge pump and battery draw
 *            ibus = (Vsrc - ibus * R_path - 2 * Vbat) / (4 * R_bat)
 *   limited: as follow, but the source holds ibus at I_lim and its voltage
 *            Vsrc sags onto the load line.
 * vbus is what the master pump measures: Vsrc - ibus * R_path, optionally with
 * measurement noise. Tune: request +100 mV while ibus is under the tune limit,
 * -20 mV above limit + 100, after the clamp request <= vbus + ibus * r / 1000.
 */
#include "check.h"
#include "cpfix_policy.h"

#define VBAT 4000
#define R_BAT 115
#define VBUS_LIMIT 12000	/* DT max_vbus */
#define ROUND_MS 500
#define R_MAX 1000	/* cable_res_mohm default */
#define MARGIN 150	/* cable_margin_mohm default */

struct source {
	int r_path, i_lim;	/* i_lim 0: follows the request */
	int noise;		/* measurement noise: ibus +-30 mA, vbus +-10 mV */
};

struct sample {
	int vsrc, vbus, ibus;
};

static struct sample measure(const struct source *src, int req)
{
	struct sample m;
	int i = (req - 2 * VBAT) * 1000 / (4 * R_BAT + src->r_path);

	if (i < 0)
		i = 0;
	m.vsrc = req;
	if (src->i_lim && i > src->i_lim) {
		i = src->i_lim;
		m.vsrc = 2 * VBAT + i * (4 * R_BAT + src->r_path) / 1000;
	}
	m.ibus = i;
	m.vbus = m.vsrc - i * src->r_path / 1000;
	return m;
}

static unsigned int rng = 1;

static int noise(int amp)
{
	rng ^= rng << 13;	/* xorshift32 */
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return (int)(rng % (unsigned int)(2 * amp + 1)) - amp;
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
	int req;
	unsigned long t;
	struct sample last;	/* true values of the last round */
	int max_ibus, reached, min_ibus_after, cuts_after;
	int max_excess;		/* max(req - vbus - ibus * (r_true + margin) / 1000), measured */
	int max_r;		/* largest clamp resistance used */
};

static void run_init(struct run *r, struct strategy *s)
{
	r->req = 2 * VBAT + 200;	/* entry request */
	r->t = 1000;
	r->max_ibus = r->reached = r->cuts_after = 0;
	r->min_ibus_after = 1 << 30;
	r->max_excess = -(1 << 30);
	r->max_r = 0;
	cpfix_clamp_reset(&s->st);
}

/* One TUNE iteration; returns the true values of this round. */
static struct sample round_once(struct run *r, struct strategy *s, const struct source *src,
				int tune_lim)
{
	struct sample m = measure(src, r->req);
	int vbus = m.vbus, ibus = m.ibus, rr, bound, step, excess;

	if (src->noise) {
		ibus += noise(30);
		vbus += noise(10);
	}
	rr = strategy_r(s, r->req, vbus, ibus, r->t);
	if (rr > r->max_r)
		r->max_r = rr;
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

	if (m.ibus > r->max_ibus)
		r->max_ibus = m.ibus;
	if (r->reached && m.ibus < r->min_ibus_after)
		r->min_ibus_after = m.ibus;
	if (m.ibus >= tune_lim)
		r->reached = 1;
	excess = r->req - vbus - ibus * (src->r_path + MARGIN) / 1000;
	if (excess > r->max_excess)
		r->max_excess = excess;
	r->last = m;
	r->t += ROUND_MS;
	return m;
}

/* A / B / E: following source; ibus must reach 1800 mA and the clamp must not cut it back. */
static int follow_ok(struct strategy *s, int r_path, int verbose)
{
	struct source src = { r_path, 0, 0 };
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

/*
 * C: source limits at 2400 mA; D: it follows again after 60 rounds.
 * The recovery jump is taken at the adapter output (Vsrc), where the request
 * lands; at the pump input it is damped by the battery load line (printed too).
 */
static void limited(struct strategy *s, int *excess, int *jump, int verbose)
{
	struct source src = { 430, 2400, 0 }, back = { 430, 0, 0 };
	struct run r;
	struct sample before, after;
	int i;

	run_init(&r, s);
	for (i = 0; i < 60; i++)
		round_once(&r, s, &src, 3000);
	*excess = r.max_excess;
	before = r.last;
	after = round_once(&r, s, &back, 3000);
	*jump = after.vsrc - before.vsrc;
	if (verbose)
		printf("info limited %s: max(req - vbus - ibus*(r_true+margin)) = %d mV, r_learned=%d; "
		       "recovery Vsrc %d -> %d (jump %d), pump vbus %d -> %d (jump %d)\n",
		       s->name, r.max_excess, s->st.r_learned, before.vsrc, after.vsrc, *jump,
		       before.vbus, after.vbus, after.vbus - before.vbus);
}

/* F: 2400 mA limit with measurement noise, 300 rounds. Returns 1 when both bounds hold. */
static int noisy(struct strategy *s, unsigned int seed, int *max_r, int *max_excess)
{
	struct source src = { 430, 2400, 1 };
	struct run r;
	int i;

	rng = seed ? seed : 1;
	run_init(&r, s);
	for (i = 0; i < 300; i++)
		round_once(&r, s, &src, 3000);
	*max_r = r.max_r;
	*max_excess = r.max_excess;
	return r.max_r <= 430 + MARGIN + 50 && r.max_excess <= 120;
}

int main(void)
{
	struct strategy learned = { .name = "learned" };
	struct strategy s350 = { .name = "static 350", .static_r = 350 };
	struct strategy s1000 = { .name = "static 1000", .static_r = 1000 };
	struct strategy s600 = { .name = "static 600", .static_r = 600 };
	int excess, jump, max_r, ok, fails, worst_r, worst_x;
	unsigned int seed;

	EQ(follow_ok(&learned, 430, 1), 1, "A: 0.43 Ohm, learned clamp reaches 1800 mA");
	EQ(follow_ok(&learned, 130, 1), 1, "B: 0.13 Ohm, learned clamp reaches 1800 mA");
	EQ(follow_ok(&learned, 600, 1), 1, "E: 0.6 Ohm, learned clamp reaches 1800 mA");
	limited(&learned, &excess, &jump, 1);
	LE(excess, 120, "C: request excess bounded by ibus * (r_true + margin) + 120 mV");
	LE(jump, 600, "D: recovery jump of the learned clamp <= 0.6 V");

	ok = noisy(&learned, 12345, &max_r, &excess);
	printf("info noisy learned seed 12345: max r_eff=%d (limit %d), max excess=%d (limit 120)\n",
	       max_r, 430 + MARGIN + 50, excess);
	LE(max_r, 430 + MARGIN + 50, "F: r_eff <= r_true + margin + 50 under noise");
	LE(excess, 120, "F: request excess within the C bound under noise");

	/* how often F fails over seeds (information for the plan, no assertion) */
	fails = 0;
	worst_r = worst_x = 0;
	for (seed = 1; seed <= 1000; seed++) {
		if (!noisy(&learned, seed, &max_r, &excess))
			fails++;
		if (max_r > worst_r)
			worst_r = max_r;
		if (excess > worst_x)
			worst_x = excess;
	}
	printf("info noisy learned over 1000 seeds: %d fail, worst r_eff=%d, worst excess=%d\n",
	       fails, worst_r, worst_x);

	/* negative controls: the static clamps must fail where the plan says */
	EQ(follow_ok(&s350, 430, 1), 0, "static 350 must fail on A");
	limited(&s1000, &excess, &jump, 1);
	EQ(excess > 120, 1, "static 1000 must fail on C");
	EQ(jump > 1000, 1, "static 1000 recovery jump must exceed 1.0 V");

	/* informational, no assertion */
	follow_ok(&s600, 430, 1);
	limited(&s600, &excess, &jump, 1);
	follow_ok(&learned, 700, 1);

	return check_report();
}
