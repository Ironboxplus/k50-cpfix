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
	return check_report();
}
