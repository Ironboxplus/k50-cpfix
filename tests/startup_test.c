// SPDX-License-Identifier: GPL-2.0
#include "check.h"
#include "cpfix_startup.h"

/* Measurements at 321687.993522 in klog_nofast.txt, lines 24547-24558. */
static struct cpfix_start_sample failing_start(void)
{
	struct cpfix_start_sample s = {
		.state = 1, .mode = 1, .pd_type = 4, .valid = CP_START_VALID,
		.converter = {0x10, 0x10}, .vbus = 7991, .cp_vbat = 3225,
		.bms_vbat = 3718, .request = 7816, .min_request = 3600,
		.max_request = 11000, .low_gap = 400, .high_gap = 600,
		.init_count = 1,
	};
	return s;
}

int main(void)
{
	struct cpfix_start_sample s = failing_start();
	unsigned int bit;
	int failures = 0, i;
	EQ(cpfix_start_decide(&s), CP_START_DOWN, "hardware ERRORHI overrides stale BMS window");
	s.state = 2; s.enabled = 3; s.cp_vbat = 3182;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "321688.338: enabled but not switching");
	s.state = 3; s.vbus = 10965; s.request = 10796;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "321767: never continue the unloaded ramp");
	s.converter[0] = s.converter[1] = 0x18;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "soft-start timeout");
	/* Positive control: the later startup at 321770.088 really switches. */
	s.converter[0] = s.converter[1] = 0x04;
	s.cp_vbat = 3563; s.bms_vbat = 3573; s.vbus = 7698;
	EQ(cpfix_start_decide(&s), CP_START_KEEP, "working pump allowed to regulate");
	/* A high source request can be valid when the cable carries current. */
	s.request = 10800; s.vbus = 8500; s.cp_vbat = 4000;
	EQ(cpfix_start_decide(&s), CP_START_KEEP, "no universal 2VBAT+600 request cap");
	for (bit = 1; bit <= 32; bit <<= 1) {
		s.valid = CP_START_VALID & ~bit;
		EQ(cpfix_start_decide(&s), CP_START_RECOVER, "read failure bit %u", bit);
	}
	s.valid = CP_START_VALID; s.request_failed = 1;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "failed PD request must exit regulation");
	s.request_failed = 0; s.pd_type = 0;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "PD reset");
	s.pd_type = 4; s.converter[1] = 0;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "one pump stops switching");
	s.state = 2; s.converter[0] = 0; s.enable_count = 1;
	EQ(cpfix_start_decide(&s), CP_START_WAIT, "pending startup cannot enter TUNE");
	s.enable_count = 4;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "pending startup has a deadline");
	s = failing_start(); s.converter[1] = 0x20;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "contradictory input-window states");
	s = failing_start(); s.converter[0] = s.converter[1] = 0;
	s.vbus = 7000; s.request = 6900;
	EQ(cpfix_start_decide(&s), CP_START_READY, "fresh CP window allows startup");
	s.cp_vbat = 0;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "invalid ADC");
	s = failing_start(); s.request = 3600;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "do not request below lower bound");
	s = failing_start(); s.converter[0] = s.converter[1] = 0x20; s.request = 11000;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "do not request above APDO maximum");
	s = failing_start(); s.init_count = 39;
	EQ(cpfix_start_decide(&s), CP_START_RECOVER, "bounded voltage search");
	s = failing_start(); s.mode = 2;
	EQ(cpfix_start_decide(&s), CP_START_KEEP, "bypass is outside this verified repair");
	s.mode = 0;
	EQ(cpfix_start_decide(&s), CP_START_KEEP, "4:1 is outside this verified repair");
	s.mode = 1; s.state = 4;
	EQ(cpfix_start_decide(&s), CP_START_KEEP, "never interfere with vendor EXIT");
	s = failing_start(); s.init_count = 0; s.cp_vbat = 3642;
	EQ(cpfix_start_decide(&s), CP_START_FIRST, "first request uses a valid fresh sample");
	EQ(cpfix_start_first_mv(&s), 7684, "entry setpoint from pump-side VBAT");
	s.max_request = 7600;
	EQ(cpfix_start_first_mv(&s), 7600, "entry obeys source maximum");
	s.cp_vbat = 1400;
	EQ(cpfix_start_first_mv(&s), 3600, "entry obeys vendor lower bound");
	EQ(cpfix_start_failure(&failures), 1, "first failure uses vendor HOLD recovery");
	EQ(cpfix_start_failure(&failures), 1, "second failure uses vendor HOLD recovery");
	for (i = 0; i < 100; i++)
		EQ(cpfix_start_failure(&failures), 2, "persistent fault ends retry loop %d", i);
	EQ(failures, 3, "retry counter saturates");
	/* Coarse offline plant: source follows request with a 175mV offset,
	 * hardware window is 400..600mV above 2*CP_VBAT. This is a convergence
	 * check, not a claim to model the SC8551 analogue thresholds or the battery. */
	s = failing_start();
	for (i = 0; i < 40; i++) {
		enum cpfix_start_action a;
		s.vbus = s.request + 175;
		s.converter[0] = s.converter[1] = s.vbus >= 2*s.cp_vbat+600 ? 0x10 :
			s.vbus <= 2*s.cp_vbat+400 ? 0x20 : 0;
		a = cpfix_start_decide(&s);
		if (a == CP_START_READY)
			break;
		EQ(a, CP_START_DOWN, "initial ERRORHI moves toward the startup window");
		s.request -= 40;
		s.init_count++;
	}
	LE(i, 38, "startup reaches a valid window before vendor timeout");
	s.state = 2; s.enabled = 3; s.converter[0] = s.converter[1] = 0x04;
	EQ(cpfix_start_decide(&s), CP_START_KEEP, "converged startup can enter regulation");
	return check_report();
}
