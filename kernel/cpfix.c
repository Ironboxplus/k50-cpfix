// SPDX-License-Identifier: GPL-2.0
/*
 * cpfix: undo the cycle-count throttles of the K50 vendor charge-pump modules.
 *
 * Each patch point (patchpoints.def) is a kprobe on a verified instruction of
 * the running pd_cp_manager / mtk_charger_framework. The pre_handler rewrites
 * a register before the original instruction executes. Nothing is written to
 * module text by hand; unloading cpfix removes every probe.
 */
#define pr_fmt(fmt) "cpfix: " fmt

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#include "cpfix_policy.h"

#ifndef CPFIX_VERSION
#define CPFIX_VERSION "unknown"
#endif

/* votable API exported by mtk_charger_framework, prototypes as pd_cp_manager uses them */
struct votable;
struct votable *find_votable(const char *name);
int vote(struct votable *votable, const char *client_str, bool enabled, int val);
int get_effective_result(struct votable *votable);

#define CPFIX_USER_VOTER "CPFIX_USER_VOTER"
#define PDPM_DT_HIGH_SOC 0x1c0		/* struct usbpd_pm: DT high_soc (u32) */
#define PDPM_REQUEST_VOLTAGE 0x28c	/* struct usbpd_pm: request_voltage (int, mV) */
#define CPFIX_POLL_MS 2000

static struct votable *cpfix_fcc, *cpfix_fv;
static bool cpfix_armed;		/* set only after every probe is verified */
static bool cpfix_ready;		/* work initialised, guarded by cpfix_lock */
static DEFINE_MUTEX(cpfix_lock);
static struct delayed_work cpfix_work;

/* ---- parameters ---- */

struct cpfix_param {
	int *val;
	int min, max, zero_ok;
};

static void cpfix_kick(void)
{
	mutex_lock(&cpfix_lock);
	if (cpfix_ready)
		mod_delayed_work(system_wq, &cpfix_work, 0);
	mutex_unlock(&cpfix_lock);
}

static int cpfix_param_set(const char *buf, const struct kernel_param *kp)
{
	const struct cpfix_param *p = kp->arg;
	int v, ret;

	ret = kstrtoint(buf, 0, &v);
	if (ret)
		return ret;
	WRITE_ONCE(*p->val, cpfix_clamp_param(v, p->min, p->max, p->zero_ok));
	cpfix_kick();
	return 0;
}

static int cpfix_param_get(char *buf, const struct kernel_param *kp)
{
	const struct cpfix_param *p = kp->arg;

	return scnprintf(buf, PAGE_SIZE, "%d\n", READ_ONCE(*p->val));
}

static const struct kernel_param_ops cpfix_param_ops = {
	.set = cpfix_param_set,
	.get = cpfix_param_get,
};

#define X(name, def, min, max, zero_ok)                                        \
	static int cp_##name = def;                                            \
	static struct cpfix_param cpfix_param_##name = { &cp_##name, min, max, \
							  zero_ok };           \
	module_param_cb(name, &cpfix_param_ops, &cpfix_param_##name, 0644);    \
	MODULE_PARM_DESC(name, "default " #def ", range " #min "-" #max        \
			       " (0 allowed: " #zero_ok ")");
CPFIX_PARAMS(X)
#undef X

/* ---- battery temperature cache, written by the work, read by the P6 handler ---- */

static atomic_t cpfix_temp = ATOMIC_INIT(0);		/* 0.1 degC */
static atomic_long_t cpfix_temp_stamp = ATOMIC_LONG_INIT(0);	/* jiffies */
static atomic_t cpfix_temp_valid = ATOMIC_INIT(0);
static atomic_t cpfix_thermal_state = ATOMIC_INIT(0);	/* hysteresis, before freshness */
static int cpfix_cap_applied;				/* work and exit only */

static unsigned long cpfix_temp_age_ms(void)
{
	return jiffies_to_msecs(jiffies - atomic_long_read(&cpfix_temp_stamp));
}

static int cpfix_thermal_now(void)
{
	return cpfix_thermal_effective(atomic_read(&cpfix_thermal_state),
				       cpfix_temp_fresh(atomic_read(&cpfix_temp_valid),
							cpfix_temp_age_ms()));
}

/* ---- patch points ---- */

struct cpfix_point {
	const char *id, *module, *symbol;
	unsigned int offset;
	u32 expected;
	kprobe_pre_handler_t handler;
	char name[MODULE_NAME_LEN + KSYM_NAME_LEN];	/* "module:symbol" */
	struct kprobe kp;
	atomic_long_t hits;
	bool registered;
};

/* Counts the hit; true when the handler may change registers. */
static bool cpfix_on(struct kprobe *p)
{
	struct cpfix_point *pt = container_of(p, struct cpfix_point, kp);

	atomic_long_inc(&pt->hits);
	return READ_ONCE(cpfix_armed) && READ_ONCE(cp_enable);
}

/* Write a W register the way the CPU does: zero-extended into X. */
static void set_w(struct pt_regs *regs, int n, u32 v)
{
	regs->regs[n] = v;
}

/* P1 pdm_check_condition: entry margin w26 and SOC ceiling w22 as for < 800 cycles */
static int cpfix_pre_p1(struct kprobe *p, struct pt_regs *regs)
{
	u32 dt;

	if (!cpfix_on(p))
		return 0;
	dt = READ_ONCE(*(u32 *)(regs->regs[19] + PDPM_DT_HIGH_SOC));
	set_w(regs, 26, (u32)-READ_ONCE(cp_entry_vbat_margin_mv));
	set_w(regs, 22, cpfix_high_soc(READ_ONCE(cp_entry_high_soc), dt));
	return 0;
}

/* P2 pdm_main_sm `cmp w9,#0x31f`: cycle count seen as 0, so the step is not locked to 1 */
static int cpfix_pre_p2(struct kprobe *p, struct pt_regs *regs)
{
	if (cpfix_on(p) && READ_ONCE(cp_fast_ramp))
		set_w(regs, 9, 0);
	return 0;
}

/* P4 pdm_taper_reduce_current: w20 = reduce_ma, w25 = cycle count (0: every time) */
static int cpfix_pre_p4(struct kprobe *p, struct pt_regs *regs)
{
	if (!cpfix_on(p))
		return 0;
	set_w(regs, 20, READ_ONCE(cp_taper_reduce_ma));
	set_w(regs, 25, 0);
	return 0;
}

/* P5 charge_monitor_func `cbz w10`: adapter treated as verified for FFC */
static int cpfix_pre_p5(struct kprobe *p, struct pt_regs *regs)
{
	if (cpfix_on(p) && READ_ONCE(cp_ffc_any_pps))
		set_w(regs, 10, 1);
	return 0;
}

/*
 * P6 vote() entry: raise THERMAL_VOTER on CHARGER_FCC while the battery is cool;
 * cap every enabled CHARGER_FV vote at fv_ceiling_mv.
 */
static int cpfix_pre_p6(struct kprobe *p, struct pt_regs *regs)
{
	struct votable *v = (struct votable *)regs->regs[0];
	int val, nv;

	if (!cpfix_on(p) || (v != cpfix_fcc && v != cpfix_fv))
		return 0;
	val = (int)regs->regs[3];
	if (v == cpfix_fcc)
		nv = cpfix_vote_value(1, (const char *)regs->regs[1], (u8)regs->regs[2], val,
				      READ_ONCE(cp_thermal_override), cpfix_thermal_now(),
				      READ_ONCE(cp_thermal_override_ma));
	else
		nv = cpfix_fv_vote_value(1, (u8)regs->regs[2], val, 1, READ_ONCE(cp_fv_ceiling_mv));
	if (nv != val)
		set_w(regs, 3, nv);
	return 0;
}

/*
 * P7 pdm_main_sm `mul w8,w8,w11`: resistance of the tune clamp, stock 350 mOhm,
 * chosen so the bound is vbus + ibus * 350 / 1000 + clamp_headroom_mv
 * (cpfix_clamp_r). Here w8 = total_ibus, w10 = master_cp_vbus,
 * pdpm+0x28c = last request. The last_* values are for display only.
 */
static int cpfix_last_vbus, cpfix_last_ibus, cpfix_last_req, cpfix_last_bound;

static int cpfix_pre_p7(struct kprobe *p, struct pt_regs *regs)
{
	int vbus, ibus, r;

	if (!cpfix_on(p))
		return 0;
	ibus = (int)regs->regs[8];
	vbus = (int)regs->regs[10];
	r = cpfix_clamp_r(ibus, READ_ONCE(cp_clamp_headroom_mv));
	set_w(regs, 11, r);
	WRITE_ONCE(cpfix_last_vbus, vbus);
	WRITE_ONCE(cpfix_last_ibus, ibus);
	WRITE_ONCE(cpfix_last_req, READ_ONCE(*(int *)(regs->regs[19] + PDPM_REQUEST_VOLTAGE)));
	WRITE_ONCE(cpfix_last_bound, vbus + ibus * r / 1000);
	return 0;
}

/* P8a charge_monitor_func `sub w8,w9,w10`: w10 = diff_fv_val of the FFC_VOTER vote */
static int cpfix_pre_p8_x10(struct kprobe *p, struct pt_regs *regs)
{
	if (cpfix_on(p))
		set_w(regs, 10, cpfix_fv_drop((int)regs->regs[10], READ_ONCE(cp_fv_drop_max_mv)));
	return 0;
}

/* P8b/P8c `sub w8,w8,w9`: w9 = diff_fv_val of the FFC_VOTER / JEITA_CHARGE_VOTER vote */
static int cpfix_pre_p8_x9(struct kprobe *p, struct pt_regs *regs)
{
	if (cpfix_on(p))
		set_w(regs, 9, cpfix_fv_drop((int)regs->regs[9], READ_ONCE(cp_fv_drop_max_mv)));
	return 0;
}

/*
 * P10 do_algorithm `cbz x10` of the HVCHG branch: w9 = 1600000 uA input current,
 * stored to info+0x90 on both paths after the branch
 */
static int cpfix_pre_p10(struct kprobe *p, struct pt_regs *regs)
{
	if (cpfix_on(p))
		set_w(regs, 9, cpfix_hvchg_icl_ua(READ_ONCE(cp_hvchg_icl_ma)));
	return 0;
}

static struct cpfix_point cpfix_points[] = {
#define PP(pid, mod, sym, off, insn, fn)                                      \
	{ .id = #pid, .module = mod, .symbol = sym, .offset = off,            \
	  .expected = insn, .handler = fn },
#include "patchpoints.def"
#undef PP
};

static void cpfix_unregister_all(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cpfix_points); i++) {
		if (cpfix_points[i].registered) {
			unregister_kprobe(&cpfix_points[i].kp);
			cpfix_points[i].registered = false;
		}
	}
}

/* Register every point and compare the saved original instruction. All or nothing. */
static int cpfix_register_all(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(cpfix_points); i++) {
		struct cpfix_point *pt = &cpfix_points[i];

		snprintf(pt->name, sizeof(pt->name), "%s:%s", pt->module, pt->symbol);
		pt->kp.symbol_name = pt->name;
		pt->kp.offset = pt->offset;
		pt->kp.pre_handler = pt->handler;
		ret = register_kprobe(&pt->kp);
		if (ret) {
			pr_err("%s %s+%#x: register_kprobe failed: %d\n",
			       pt->id, pt->name, pt->offset, ret);
			goto fail;
		}
		pt->registered = true;
		/* kp.opcode is the instruction kprobes saved; memory now holds BRK */
		if (pt->kp.opcode != pt->expected) {
			pr_err("%s %s+%#x at %px: expected %#010x, found %#010x\n",
			       pt->id, pt->name, pt->offset, pt->kp.addr,
			       pt->expected, pt->kp.opcode);
			goto fail;
		}
		pr_info("%s %s+%#x at %px verified %#010x\n",
			pt->id, pt->name, pt->offset, pt->kp.addr, pt->kp.opcode);
	}
	return 0;
fail:
	cpfix_unregister_all();
	return -ENOEXEC;
}

/* ---- periodic work: temperature cache and user FCC cap ---- */

static void cpfix_apply_cap(void)
{
	int en, val, ret;

	if (!cpfix_cap_decide(READ_ONCE(cp_enable), READ_ONCE(cp_fcc_cap_ma),
			      cpfix_cap_applied, &en, &val))
		return;
	ret = vote(cpfix_fcc, CPFIX_USER_VOTER, en, val);
	if (ret)
		pr_warn("vote %s %d %d: %d\n", CPFIX_USER_VOTER, en, val, ret);
	cpfix_cap_applied = val;
}

static void cpfix_work_fn(struct work_struct *work)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval v;

	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_TEMP, &v)) {
			atomic_set(&cpfix_temp, v.intval);
			atomic_set(&cpfix_thermal_state,
				   cpfix_thermal_next(atomic_read(&cpfix_thermal_state), v.intval,
						      READ_ONCE(cp_batt_temp_hi),
						      READ_ONCE(cp_batt_temp_lo)));
			atomic_long_set(&cpfix_temp_stamp, jiffies);
			atomic_set(&cpfix_temp_valid, 1);
		}
		power_supply_put(psy);
	}
	cpfix_apply_cap();
	queue_delayed_work(system_wq, &cpfix_work, msecs_to_jiffies(CPFIX_POLL_MS));
}

/* ---- /sys/kernel/cpfix ---- */

static struct kobject *cpfix_kobj;

static ssize_t patches_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	const char *state = READ_ONCE(cp_enable) ? "active" : "verified";
	unsigned int i;
	int n = 0;

	for (i = 0; i < ARRAY_SIZE(cpfix_points); i++) {
		struct cpfix_point *pt = &cpfix_points[i];

		n += scnprintf(buf + n, PAGE_SIZE - n, "%s %s+%#x %px %#010x %#010x %s %ld\n",
			       pt->id, pt->name, pt->offset, pt->kp.addr, pt->expected,
			       pt->kp.opcode, state, atomic_long_read(&pt->hits));
	}
	return n;
}

static ssize_t batt_temp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&cpfix_temp));
}

static ssize_t batt_temp_age_ms_show(struct kobject *kobj, struct kobj_attribute *attr,
				     char *buf)
{
	if (!atomic_read(&cpfix_temp_valid))
		return scnprintf(buf, PAGE_SIZE, "-1\n");
	return scnprintf(buf, PAGE_SIZE, "%lu\n", cpfix_temp_age_ms());
}

static ssize_t thermal_active_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", cpfix_thermal_now());
}

static ssize_t fcc_effective_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_effective_result(cpfix_fcc));
}

/* last tune clamp: last_vbus last_ibus last_req last_bound (mV, mA, mV, mV) */
static ssize_t clamp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d %d %d %d\n", READ_ONCE(cpfix_last_vbus),
			 READ_ONCE(cpfix_last_ibus), READ_ONCE(cpfix_last_req),
			 READ_ONCE(cpfix_last_bound));
}

static ssize_t fv_effective_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_effective_result(cpfix_fv));
}

static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", CPFIX_VERSION);
}

static struct kobj_attribute cpfix_attr_patches = __ATTR(patches, 0400, patches_show, NULL);
static struct kobj_attribute cpfix_attr_batt_temp = __ATTR_RO(batt_temp);
static struct kobj_attribute cpfix_attr_batt_temp_age_ms = __ATTR_RO(batt_temp_age_ms);
static struct kobj_attribute cpfix_attr_thermal_active = __ATTR_RO(thermal_active);
static struct kobj_attribute cpfix_attr_fcc_effective = __ATTR_RO(fcc_effective);
static struct kobj_attribute cpfix_attr_clamp = __ATTR_RO(clamp);
static struct kobj_attribute cpfix_attr_fv_effective = __ATTR_RO(fv_effective);
static struct kobj_attribute cpfix_attr_version = __ATTR_RO(version);

static struct attribute *cpfix_attrs[] = {
	&cpfix_attr_patches.attr,
	&cpfix_attr_batt_temp.attr,
	&cpfix_attr_batt_temp_age_ms.attr,
	&cpfix_attr_thermal_active.attr,
	&cpfix_attr_fcc_effective.attr,
	&cpfix_attr_clamp.attr,
	&cpfix_attr_fv_effective.attr,
	&cpfix_attr_version.attr,
	NULL,
};

static const struct attribute_group cpfix_group = {
	.attrs = cpfix_attrs,
};

/* ---- init / exit ---- */

static int __init cpfix_init(void)
{
	int ret;

	cpfix_fcc = find_votable("CHARGER_FCC");
	if (IS_ERR_OR_NULL(cpfix_fcc)) {
		pr_err("votable CHARGER_FCC not found\n");
		return -ENODEV;
	}
	cpfix_fv = find_votable("CHARGER_FV");
	if (IS_ERR_OR_NULL(cpfix_fv)) {
		pr_err("votable CHARGER_FV not found\n");
		return -ENODEV;
	}

	ret = cpfix_register_all();
	if (ret)
		return ret;

	cpfix_kobj = kobject_create_and_add("cpfix", kernel_kobj);
	if (!cpfix_kobj) {
		ret = -ENOMEM;
		goto err_probes;
	}
	ret = sysfs_create_group(cpfix_kobj, &cpfix_group);
	if (ret)
		goto err_kobj;

	INIT_DELAYED_WORK(&cpfix_work, cpfix_work_fn);
	mutex_lock(&cpfix_lock);
	cpfix_ready = true;
	queue_delayed_work(system_wq, &cpfix_work, 0);
	mutex_unlock(&cpfix_lock);

	WRITE_ONCE(cpfix_armed, true);
	pr_info("%s armed, %zu patch points, enable=%d\n", CPFIX_VERSION,
		ARRAY_SIZE(cpfix_points), cp_enable);
	return 0;

err_kobj:
	kobject_put(cpfix_kobj);
err_probes:
	cpfix_unregister_all();
	return ret;
}

static void __exit cpfix_exit(void)
{
	WRITE_ONCE(cpfix_armed, false);
	cpfix_unregister_all();

	mutex_lock(&cpfix_lock);
	cpfix_ready = false;
	mutex_unlock(&cpfix_lock);
	/* stop the work first so it cannot vote again after the revoke */
	cancel_delayed_work_sync(&cpfix_work);
	if (cpfix_cap_applied)
		vote(cpfix_fcc, CPFIX_USER_VOTER, false, 0);

	sysfs_remove_group(cpfix_kobj, &cpfix_group);
	kobject_put(cpfix_kobj);
	pr_info("unloaded, stock behaviour restored\n");
}

module_init(cpfix_init);
module_exit(cpfix_exit);

MODULE_DESCRIPTION("K50 charge-pump cycle-count throttle fix (kprobes on vendor modules)");
MODULE_AUTHOR("ironbox");
MODULE_LICENSE("GPL");
MODULE_VERSION(CPFIX_VERSION);
