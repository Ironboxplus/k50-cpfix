/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CPFIX_STARTUP_H
#define CPFIX_STARTUP_H

/* State and mode values verified in the target pd_cp_manager ELF. */
enum cpfix_start_action {
	CP_START_KEEP, CP_START_FIRST, CP_START_UP, CP_START_DOWN,
	CP_START_READY, CP_START_WAIT, CP_START_RECOVER
};

struct cpfix_start_sample {
	int state, mode, pd_type, bypass;
	unsigned int valid; /* bit 0 VBUS, 1 VBAT, 2/3 enable reads, 4/5 converter reads */
	unsigned int converter[2], enabled;
	int vbus, cp_vbat, bms_vbat;
	int request, min_request, max_request, low_gap, high_gap;
	int init_count, enable_count, request_failed;
};

#define CP_START_VALID 0x3fU
#define CP_SC_SWITCHING 0x04U
#define CP_SC_ERRORHI 0x10U
#define CP_SC_ERRORLO 0x20U
#define CP_SC_FAULT 0xcbU
#define CP_START_RETRIES 3

/* Decisions for the verified SC8551 2:1 path only. Never a running-voltage cap. */
static inline enum cpfix_start_action
cpfix_start_decide(const struct cpfix_start_sample *s)
{
	unsigned int status = s->converter[0] | s->converter[1];
	enum cpfix_start_action action;
	int low, high;
	if (s->mode != 1 || s->bypass || s->state < 1 || s->state > 3)
		return CP_START_KEEP;
	if (s->valid != CP_START_VALID || s->pd_type != 4 || s->request_failed ||
	    s->cp_vbat <= 0 || s->cp_vbat > 5118 || s->vbus < 0 || s->vbus > 15356 ||
	    s->converter[0] > 255 || s->converter[1] > 255 ||
	    (status & CP_SC_FAULT) ||
	    (status & (CP_SC_ERRORHI | CP_SC_ERRORLO)) == (CP_SC_ERRORHI | CP_SC_ERRORLO))
		return CP_START_RECOVER;
	if (s->state == 1) {
		if (s->enabled || (status & CP_SC_SWITCHING) || s->init_count >= 39 ||
		    s->min_request < 3600 || s->max_request > 15356 ||
		    s->max_request < s->min_request || s->low_gap < 0 ||
		    s->high_gap <= s->low_gap || s->high_gap > 2000)
			return CP_START_RECOVER;
		if (!s->init_count)
			return CP_START_FIRST;
		low = 2 * s->cp_vbat + s->low_gap;
		high = 2 * s->cp_vbat + s->high_gap;
		/* Hardware window status takes priority over asynchronously sampled ADCs. */
		if (status & CP_SC_ERRORHI)
			action = CP_START_DOWN;
		else if (status & CP_SC_ERRORLO)
			action = CP_START_UP;
		else
			action = s->vbus <= low ? CP_START_UP :
				s->vbus >= high ? CP_START_DOWN : CP_START_READY;
		if (s->request < s->min_request || s->request > s->max_request ||
		    (action == CP_START_UP && s->request > s->max_request - 40) ||
		    (action == CP_START_DOWN && s->request < s->min_request + 40))
			return CP_START_RECOVER;
		return action;
	}
	if (status & (CP_SC_ERRORHI | CP_SC_ERRORLO))
		return CP_START_RECOVER;
	if (s->enabled == 3 && (s->converter[0] & CP_SC_SWITCHING) &&
	    (s->converter[1] & CP_SC_SWITCHING))
		return CP_START_KEEP;
	return s->state == 2 && s->enable_count < 4 ? CP_START_WAIT : CP_START_RECOVER;
}

/* Called only after CP_START_FIRST validated the sample and bounds. */
static inline int cpfix_start_first_mv(const struct cpfix_start_sample *s)
{
	int v = 2 * s->cp_vbat + s->low_gap;
	return v < s->min_request ? s->min_request :
		v > s->max_request ? s->max_request : v;
}

/* Operands of the installed INIT_VBUS CMPs: second threshold is base+high_gap. */
static inline void cpfix_start_window_operands(enum cpfix_start_action action,
		int vbus, int high_gap, int *low, int *base)
{
	if (action == CP_START_UP)
		*low = vbus;
	else if (action == CP_START_DOWN || action == CP_START_READY) {
		*low = vbus - 1;
		*base = vbus + (action == CP_START_READY) - high_gap;
	}
}

/* Vendor HOLD=1 retries through pump disable and buck fallback; EXIT=2 stops. */
static inline int cpfix_start_failure(int *failures)
{
	if (*failures < CP_START_RETRIES)
		++*failures;
	return *failures < CP_START_RETRIES ? 1 : 2;
}
#endif
