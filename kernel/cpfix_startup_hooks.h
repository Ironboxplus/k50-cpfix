/* SPDX-License-Identifier: GPL-2.0 */
/* Included by cpfix.c: handlers only inspect completed reads and CPU operands.
 * No I2C, sleeps, PD calls, or work scheduling in probe context. */
#include <linux/sched.h>
#include <linux/spinlock.h>
#include "cpfix_startup.h"

static DEFINE_RAW_SPINLOCK(cp_start_lock);
static struct {
	struct task_struct *owner;
	unsigned long pdpm, chip[2], stamp, healthy_since;
	struct cpfix_start_sample sample;
	enum cpfix_start_action action;
	bool collecting, apply, terminal, healthy;
	int failures, last_ret;
	bool request_failed;
	unsigned long scans, recoveries;
} cp_start;

static int cp_start_int(unsigned long base, unsigned int off)
{
	return READ_ONCE(*(int *)(base + off));
}

static unsigned long cp_start_ptr(unsigned long base, unsigned int off)
{
	return base ? READ_ONCE(*(unsigned long *)(base + off)) : 0;
}

static bool cp_start_owned(unsigned long pdpm)
{
	return cp_start.owner == current && cp_start.pdpm == pdpm;
}

/* pdm_main_sm+0x54: immediately after master charger_dev_get_vbus. */
static int cpfix_pre_start_begin(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq, base = r->regs[19];
	bool on = cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start.pdpm != base || (cp_start.terminal && cp_start_int(base, 0x218) == 0)) {
		cp_start.failures = 0;
		cp_start.terminal = false;
		cp_start.healthy = false;
	}
	if (cp_start_int(base, 0x218) == 0) {
		cp_start.request_failed = false;
		cp_start.last_ret = 0;
	}
	cp_start.owner = current;
	cp_start.pdpm = base;
	cp_start.chip[0] = cp_start_ptr(cp_start_ptr(base, 0x8), 0xe0);
	cp_start.chip[1] = cp_start_ptr(cp_start_ptr(base, 0x10), 0xe0);
	memset(&cp_start.sample, 0, sizeof(cp_start.sample));
	cp_start.sample.valid = (int)r->regs[0] == 0 ? 1 : 0;
	cp_start.action = CP_START_KEEP;
	cp_start.stamp = jiffies;
	cp_start.collecting = true;
	cp_start.apply = on && READ_ONCE(cp_startup_guard);
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

static int cp_start_read_result(struct kprobe *p, struct pt_regs *r, unsigned int bit)
{
	unsigned long irq;
	cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start_owned(r->regs[19]) && cp_start.collecting && (int)r->regs[0] == 0)
		cp_start.sample.valid |= bit;
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

static int cpfix_pre_start_vbat(struct kprobe *p, struct pt_regs *r)
{
	return cp_start_read_result(p, r, 2);
}
static int cpfix_pre_start_master(struct kprobe *p, struct pt_regs *r)
{
	return cp_start_read_result(p, r, 4);
}
static int cpfix_pre_start_slave(struct kprobe *p, struct pt_regs *r)
{
	return cp_start_read_result(p, r, 8);
}

/* sc8551_dump_important_regs+0x40: regmap_read(0x0a) returned in W0, data SP+4. */
static int cpfix_pre_start_converter(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq;
	int i;
	cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start.owner == current && cp_start.collecting) {
		for (i = 0; i < 2; i++) {
			if (!cp_start.chip[i] || cp_start.chip[i] != r->regs[19])
				continue;
			cp_start.sample.valid &= ~(16U << i);
			if ((int)r->regs[0] == 0) {
				cp_start.sample.converter[i] = READ_ONCE(*(u32 *)(r->sp + 4));
				cp_start.sample.valid |= 16U << i;
			}
		}
	}
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

/* State dispatch before its original jump table. EXIT executes ordinary vendor
 * pump-disable, adapter fallback and buck power-path restoration in process context. */
static int cpfix_pre_start_dispatch(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq, base = r->regs[19];
	struct cpfix_start_sample *s = &cp_start.sample;
	int max_dt, max_apdo, status;
	bool on = cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (!cp_start_owned(base))
		goto out;
	cp_start.collecting = false;
	cp_start.apply = cp_start.apply && on;
	s->state = cp_start_int(base, 0x218);
	s->mode = cp_start_int(base, 0x2d0);
	s->pd_type = cp_start_int(base, 0x220);
	s->bypass = READ_ONCE(*(u8 *)(base + 0x22a));
	s->vbus = cp_start_int(base, 0x23c);
	s->cp_vbat = cp_start_int(base, 0x240);
	s->bms_vbat = cp_start_int(base, 0x260);
	s->request = cp_start_int(base, 0x28c);
	s->enabled = !!READ_ONCE(*(u8 *)(base + 0x226)) |
		(!!READ_ONCE(*(u8 *)(base + 0x228)) << 1);
	s->init_count = cp_start_int(base, 0x2a4);
	s->enable_count = cp_start_int(base, 0x2ac);
	s->low_gap = cp_start_int(base, 0x29c);
	s->high_gap = cp_start_int(base, 0x2a0);
	max_dt = cp_start_int(base, 0x1a0);
	max_apdo = cp_start_int(base, 0x2c0);
	s->min_request = 3600; /* installed vendor's initial-request floor */
	s->max_request = min(max_dt, max_apdo);
	s->request_failed = cp_start.request_failed;
	if (time_after(jiffies, cp_start.stamp + msecs_to_jiffies(1000)))
		s->valid = 0;
	cp_start.action = cpfix_start_decide(s);
	cp_start.scans++;
	if (cp_start.apply && cp_start.action == CP_START_RECOVER) {
		status = cpfix_start_failure(&cp_start.failures);
		cp_start.terminal = status == 2;
		cp_start.recoveries++;
		WRITE_ONCE(*(int *)(base + 0x21c), status);
		WRITE_ONCE(*(int *)(base + 0x218), 4);
		set_w(r, 9, 4);
	}
	if (cp_start.apply && s->state == 3 && s->mode == 1 && cp_start.action == CP_START_KEEP) {
		if (!cp_start.healthy) {
			cp_start.healthy = true;
			cp_start.healthy_since = jiffies;
		} else if (time_after_eq(jiffies, cp_start.healthy_since + msecs_to_jiffies(5000))) {
			cp_start.failures = 0;
		}
	} else {
		cp_start.healthy = false;
	}
out:
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

static int cpfix_pre_start_first(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq;
	cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start_owned(r->regs[19]) && cp_start.apply && cp_start.action == CP_START_FIRST)
		set_w(r, 2, cpfix_start_first_mv(&cp_start.sample));
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

/* Both comparison operands are prepared atomically at the first CMP. W11 is
 * the base to which the next instructions add high_gap. Stored ADCs are unchanged. */
static int cpfix_pre_start_window(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq;
	int v, low, base;
	cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start_owned(r->regs[19]) && cp_start.apply) {
		v = (int)r->regs[10];
		low = (int)r->regs[12];
		base = (int)r->regs[11];
		cpfix_start_window_operands(cp_start.action, v, cp_start.sample.high_gap, &low, &base);
		set_w(r, 12, low);
		set_w(r, 11, base);
	}
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

static int cpfix_pre_start_enabled(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq;
	cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start_owned(r->regs[19]) && cp_start.apply && cp_start.action == CP_START_WAIT)
		set_w(r, 1, 0); /* take the existing waiting branch, not TUNE */
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

/* Three call-site returns, not a global PD hook. Any nonzero result failed to
 * accept this setpoint. Preserve the original return value and vendor handling. */
static int cpfix_pre_start_pd_result(struct kprobe *p, struct pt_regs *r)
{
	unsigned long irq;
	cpfix_on(p);
	if (!READ_ONCE(cpfix_armed))
		return 0;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	if (cp_start_owned(r->regs[19]) && cp_start.sample.pd_type == 4) {
		cp_start.last_ret = (int)r->regs[0];
		cp_start.request_failed = cp_start.last_ret != 0;
	}
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return 0;
}

static ssize_t startup_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct cpfix_start_sample s;
	unsigned long irq, scans, recoveries, age;
	int action, failures, ret, apply;
	raw_spin_lock_irqsave(&cp_start_lock, irq);
	s = cp_start.sample;
	action = cp_start.action; failures = cp_start.failures; ret = cp_start.last_ret;
	apply = cp_start.apply; scans = cp_start.scans; recoveries = cp_start.recoveries;
	age = jiffies_to_msecs(jiffies - cp_start.stamp);
	raw_spin_unlock_irqrestore(&cp_start_lock, irq);
	return scnprintf(buf, PAGE_SIZE,
		"state=%d mode=%d valid=%#x converter=%02x,%02x enabled=%u "
		"vbus=%d cp_vbat=%d bms_vbat=%d request=%d action=%d apply=%d "
		"failures=%d pd_ret=%d scans=%lu recoveries=%lu age_ms=%lu\n",
		s.state, s.mode, s.valid, s.converter[0], s.converter[1], s.enabled,
		s.vbus, s.cp_vbat, s.bms_vbat, s.request, action, apply, failures, ret,
		scans, recoveries, age);
}
