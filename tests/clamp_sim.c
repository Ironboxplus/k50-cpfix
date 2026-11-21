// SPDX-License-Identifier: GPL-2.0
/*
 * Host simulation of the vendor tune loop with the P7 clamp (PLAN, P7 v3).
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
#define HEADROOM 400	/* clamp_headroom_mv default */

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

/* clamp strategies: the module's clamp, or a static resistance */
struct strategy {
	const char *name;
	int static_r;	/* 0: the module's clamp */
	struct cpfix_clamp_state st;
};

static int strategy_r(struct strategy *s, int req, int vbus, int ibus, unsigned long t)
{
	if (s->static_r)
		return s->static_r;
	/* module clamp as of e2d0db3 (learned, cable_res 1000, margin 150) */
	return cpfix_clamp_step(&s->st, req, vbus, ibus, t, 1000, 150);
}

struct run {
	int req;
	unsigned long t;
	struct sample last;	/* true values of the last round */
	int max_ibus, reached, min_ibus_after, cuts_after;
	int max_excess;		/* max(req - vbus - ibus * r_true / 1000), measured values */
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
	bound = vbus + ibus * rr / 1000;	/* 32-bit, as the vendor's mul + smull */
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
	excess = r->req - vbus - ibus * src->r_path / 1000;
	if (excess > r->max_excess)
		r->max_excess = excess;
	r->last = m;
	r->t += ROUND_MS;
	return m;
}

/* A / B / E: following source; ibus must reach 1800 mA and stay near it. */
static int follow_ok(struct strategy *s, int r_path, int verbose)
{
	struct source src = { r_path, 0, 0 };
	struct run r;
	int i, ok;

	run_init(&r, s);
	for (i = 0; i < 80; i++)
		round_once(&r, s, &src, 1800);
	ok = r.max_ibus >= 1800 && r.min_ibus_after >= 1700;
	if (verbose)
		printf("info follow R=%d %s: max_ibus=%d min_after=%d cuts_after=%d -> %s\n",
		       r_path, s->name, r.max_ibus, r.reached ? r.min_ibus_after : -1,
		       r.cuts_after, ok ? "ok" : "fails");
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
		printf("info limited %s: max(req - vbus - ibus*r_true) = %d mV (limit %d); "
		       "recovery Vsrc %d -> %d (jump %d, limit %d), pump vbus %d -> %d (jump %d)\n",
		       s->name, r.max_excess, HEADROOM + 120, before.vsrc, after.vsrc, *jump,
		       HEADROOM + 150, before.vbus, after.vbus, after.vbus - before.vbus);
}

/* F: 2400 mA limit with measurement noise, 300 rounds; returns the max excess. */
static int noisy(struct strategy *s, unsigned int seed)
{
	struct source src = { 430, 2400, 1 };
	struct run r;
	int i;

	rng = seed ? seed : 1;
	run_init(&r, s);
	for (i = 0; i < 300; i++)
		round_once(&r, s, &src, 3000);
	return r.max_excess;
}

int main(void)
{
	struct strategy mod = { .name = "module" };
	struct strategy s350 = { .name = "static 350", .static_r = 350 };
	struct strategy s1000 = { .name = "static 1000", .static_r = 1000 };
	int excess, jump, fails, worst;
	unsigned int seed;

	EQ(follow_ok(&mod, 430, 1), 1, "A: 0.43 Ohm reaches 1800 mA");
	EQ(follow_ok(&mod, 130, 1), 1, "B: 0.13 Ohm reaches 1800 mA");
	EQ(follow_ok(&mod, 600, 1), 1, "E: 0.6 Ohm reaches 1800 mA");
	limited(&mod, &excess, &jump, 1);
	LE(excess, HEADROOM + 120, "C: request excess <= headroom + 120 mV");
	LE(jump, HEADROOM + 150, "D: recovery jump at the adapter <= headroom + 150 mV");

	fails = 0;
	worst = -(1 << 30);
	for (seed = 1; seed <= 1000; seed++) {
		excess = noisy(&mod, seed);
		if (excess > HEADROOM + 120)
			fails++;
		if (excess > worst)
			worst = excess;
	}
	printf("info noisy module over 1000 seeds: %d fail, worst excess %d (limit %d)\n",
	       fails, worst, HEADROOM + 120);
	EQ(fails, 0, "F: every one of 1000 noisy seeds keeps the excess <= headroom + 120 mV");

	/* negative controls */
	EQ(follow_ok(&s350, 430, 1), 0, "static 350 must fail on A");
	limited(&s1000, &excess, &jump, 1);
	EQ(excess > HEADROOM + 120, 1, "static 1000 must fail on C");
	EQ(jump > 1000, 1, "static 1000 recovery jump must exceed 1.0 V");

	/* informational, no assertion */
	follow_ok(&mod, 700, 1);

	return check_report();
}
