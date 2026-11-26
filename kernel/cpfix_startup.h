/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CPFIX_STARTUP_H
#define CPFIX_STARTUP_H

/* State and mode values verified in the target pd_cp_manager ELF. */
enum cpfix_start_action {
	CP_START_KEEP, CP_START_FIRST, CP_START_UP, CP_START_DOWN,
	CP_START_READY, CP_START_WAIT, CP_START_RECOVER
};

struct cpfix_start_sample {
	int state, mode, pd_type;
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

/* Baseline model for the initial red regression; not connected to kernel hooks. */
static inline enum cpfix_start_action
cpfix_start_decide(const struct cpfix_start_sample *s)
{
	int low, high;
	if (s->mode != 1 || s->state < 1 || s->state > 3)
		return CP_START_KEEP;
	if (s->state == 1) {
		if (!s->init_count)
			return CP_START_FIRST;
		low = 2 * s->bms_vbat + s->low_gap;
		high = 2 * s->bms_vbat + s->high_gap;
		return s->vbus <= low ? CP_START_UP :
			s->vbus >= high ? CP_START_DOWN : CP_START_READY;
	}
	if (s->state == 2 && s->enabled != 3)
		return CP_START_WAIT;
	return CP_START_KEEP;
}
#endif
