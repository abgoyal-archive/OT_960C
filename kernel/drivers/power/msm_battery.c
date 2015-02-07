/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

/*
 * this needs to be before <linux/kernel.h> is loaded,
 * and <linux/sched.h> loads <linux/kernel.h>
 */
#define DEBUG  1
#define USE_FUEL_GAUGE // BR.Hwang 2012-03-16
#define UPDATE_FUEL_GAUGE // BR.Hwang 2012-03-21 test

#include <linux/slab.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/atomic.h>

#include <mach/msm_rpcrouter.h>
#include <mach/msm_battery.h>

#include "../../arch/arm/mach-msm/proc_comm.h"

enum {
	DEBUG_USER_STATE = 1U << 0,
	DEBUG_DETAIL = 1U << 2,
};
static int debug_mask = DEBUG_USER_STATE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define BATTERY_RPC_PROG	0x30000089
#define BATTERY_RPC_VER_1_1	0x00010001
#define BATTERY_RPC_VER_2_1	0x00020001
#define BATTERY_RPC_VER_4_1     0x00040001
#define BATTERY_RPC_VER_5_1     0x00050001

#define BATTERY_RPC_CB_PROG	(BATTERY_RPC_PROG | 0x01000000)

#define CHG_RPC_PROG		0x3000001a
#define CHG_RPC_VER_1_1		0x00010001
#define CHG_RPC_VER_1_3		0x00010003
#define CHG_RPC_VER_2_2		0x00020002
#define CHG_RPC_VER_3_1         0x00030001
#define CHG_RPC_VER_4_1         0x00040001

#define BATTERY_REGISTER_PROC				2
#define BATTERY_MODIFY_CLIENT_PROC			4
#define BATTERY_DEREGISTER_CLIENT_PROC			5
#define BATTERY_READ_MV_PROC				12
#define BATTERY_ENABLE_DISABLE_FILTER_PROC		14

#define VBATT_FILTER			0	//	Teddy 2012-04-12

#define BATTERY_CB_TYPE_PROC		1
#define BATTERY_CB_ID_ALL_ACTIV		1
#define BATTERY_CB_ID_LOW_VOL		2

#define BATTERY_LOW		3200
#define BATTERY_HIGH		4300

#define ONCRPC_CHG_GET_GENERAL_STATUS_PROC	12
#define ONCRPC_CHARGER_API_VERSIONS_PROC	0xffffffff

#define BATT_RPC_TIMEOUT    5000	/* 5 sec */

#define INVALID_BATT_HANDLE    -1

#define RPC_TYPE_REQ     0
#define RPC_TYPE_REPLY   1
#define RPC_REQ_REPLY_COMMON_HEADER_SIZE   (3 * sizeof(uint32_t))

#define M_BATT_HEALTH {\
			switch(msm_batt_info.batt_health)\
			{\
			case POWER_SUPPLY_HEALTH_UNKNOWN: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_UNKNOWN (%d)\n", msm_batt_info.batt_health); break;\
			case POWER_SUPPLY_HEALTH_GOOD: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_GOOD (%d)\n", msm_batt_info.batt_health); break;\
			case POWER_SUPPLY_HEALTH_OVERHEAT: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_OVERHEAT (%d)\n", msm_batt_info.batt_health); break;\
			case POWER_SUPPLY_HEALTH_OVERVOLTAGE: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_OVERVOLTAGE (%d)\n", msm_batt_info.batt_health); break;\
			case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_UNSPEC_FAILURE (%d)\n", msm_batt_info.batt_health); break;\
			default: DBG_LIMIT("BATT2: M.batt_health ? (%d)\n", msm_batt_info.batt_health); break;\
			}\
			DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);\
		}

#define M_BATTERY_STATUS {\
			switch(msm_batt_info.battery_status)\
			{\
			case BATTERY_STATUS_GOOD: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_GOOD (%d)\n", msm_batt_info.battery_status); break;\
			case BATTERY_STATUS_BAD_TEMP: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_BAD_TEMP (%d)\n", msm_batt_info.battery_status); break;\
			case BATTERY_STATUS_BAD: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_BAD (%d)\n", msm_batt_info.battery_status); break;\
			case BATTERY_STATUS_REMOVED: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_REMOVED (%d)\n", msm_batt_info.battery_status); break;\
			case BATTERY_STATUS_INVALID: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_INVALID (%d)\n", msm_batt_info.battery_status); break;\
			default: DBG_LIMIT("BATT2: M.battery_status ? (%d)\n", msm_batt_info.battery_status); break;\
			}\
			DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);\
		}

#define M_BATT_STATUS {\
			switch(msm_batt_info.batt_status)\
			{\
			case POWER_SUPPLY_STATUS_UNKNOWN: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_UNKNOWN (%d)\n", msm_batt_info.batt_status); break;\
			case POWER_SUPPLY_STATUS_CHARGING: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_CHARGING (%d)\n", msm_batt_info.batt_status); break;\
			case POWER_SUPPLY_STATUS_DISCHARGING: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_DISCHARGING (%d)\n", msm_batt_info.batt_status); break;\
			case POWER_SUPPLY_STATUS_NOT_CHARGING: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_NOT_CHARGING (%d)\n", msm_batt_info.batt_status); break;\
			case POWER_SUPPLY_STATUS_FULL: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_FULL (%d)\n", msm_batt_info.batt_status); break;\
			default: DBG_LIMIT("BATT2: M.batt_status ? (%d)\n", msm_batt_info.batt_status); break;\
			}\
			DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);\
		}

#define M_BATTERY_LEVEL {\
			switch(msm_batt_info.battery_level)\
			{\
			case BATTERY_LEVEL_DEAD: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_DEAD (%d)\n", msm_batt_info.battery_level); break;\
			case BATTERY_LEVEL_WEAK: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_WEAK (%d)\n", msm_batt_info.battery_level); break;\
			case BATTERY_LEVEL_GOOD: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_GOOD (%d)\n", msm_batt_info.battery_level); break;\
			case BATTERY_LEVEL_FULL: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_FULL (%d)\n", msm_batt_info.battery_level); break;\
			default: DBG_LIMIT("BATT2: M.battery_level ? (%d)\n", msm_batt_info.battery_level); break;\
			}\
			DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);\
		}

#if DEBUG
#define DBG_LIMIT(x...) do {if (printk_ratelimit()) pr_debug(x); } while (0)
#else
#define DBG_LIMIT(x...) do {} while (0)
#endif

enum {
	BATTERY_REGISTRATION_SUCCESSFUL = 0,
	BATTERY_DEREGISTRATION_SUCCESSFUL = BATTERY_REGISTRATION_SUCCESSFUL,
	BATTERY_MODIFICATION_SUCCESSFUL = BATTERY_REGISTRATION_SUCCESSFUL,
	BATTERY_INTERROGATION_SUCCESSFUL = BATTERY_REGISTRATION_SUCCESSFUL,
	BATTERY_CLIENT_TABLE_FULL = 1,
	BATTERY_REG_PARAMS_WRONG = 2,
	BATTERY_DEREGISTRATION_FAILED = 4,
	BATTERY_MODIFICATION_FAILED = 8,
	BATTERY_INTERROGATION_FAILED = 16,
	/* Client's filter could not be set because perhaps it does not exist */
	BATTERY_SET_FILTER_FAILED         = 32,
	/* Client's could not be found for enabling or disabling the individual
	 * client */
	BATTERY_ENABLE_DISABLE_INDIVIDUAL_CLIENT_FAILED  = 64,
	BATTERY_LAST_ERROR = 128,
};

enum {
	BATTERY_VOLTAGE_UP = 0,
	BATTERY_VOLTAGE_DOWN,
	BATTERY_VOLTAGE_ABOVE_THIS_LEVEL,
	BATTERY_VOLTAGE_BELOW_THIS_LEVEL,
	BATTERY_VOLTAGE_LEVEL,
	BATTERY_ALL_ACTIVITY,
	VBATT_CHG_EVENTS,
	BATTERY_VOLTAGE_UNKNOWN,
};

/*
 * This enum contains defintions of the charger hardware status
 */
enum chg_charger_status_type {
	/* The charger is good      */
	CHARGER_STATUS_GOOD,
	/* The charger is bad       */
	CHARGER_STATUS_BAD,
	/* The charger is weak      */
	CHARGER_STATUS_WEAK,
	/* Invalid charger status.  */
	CHARGER_STATUS_INVALID
};

/*
 *This enum contains defintions of the charger hardware type
 */
enum chg_charger_hardware_type {
	/* The charger is removed                 */
	CHARGER_TYPE_NONE,
	/* The charger is a regular wall charger   */
	CHARGER_TYPE_WALL,
	/* The charger is a PC USB                 */
	CHARGER_TYPE_USB_PC,
	/* The charger is a wall USB charger       */
	CHARGER_TYPE_USB_WALL,
	/* The charger is a USB carkit             */
	CHARGER_TYPE_USB_CARKIT,
	/* Invalid charger hardware status.        */
	CHARGER_TYPE_INVALID
};

/*
 *  This enum contains defintions of the battery status
 */
enum chg_battery_status_type {
	/* The battery is good        */
	BATTERY_STATUS_GOOD,
	/* The battery is cold/hot    */
	BATTERY_STATUS_BAD_TEMP,
	/* The battery is bad         */
	BATTERY_STATUS_BAD,
	/* The battery is removed     */
	BATTERY_STATUS_REMOVED,		/* on v2.2 only */
	BATTERY_STATUS_INVALID_v1 = BATTERY_STATUS_REMOVED,
	/* Invalid battery status.    */
	BATTERY_STATUS_INVALID
};

/*
 *This enum contains defintions of the battery voltage level
 */
enum chg_battery_level_type {
	/* The battery voltage is dead/very low (less than 3.2V) */
	BATTERY_LEVEL_DEAD,
	/* The battery voltage is weak/low (between 3.2V and 3.4V) */
	BATTERY_LEVEL_WEAK,
	/* The battery voltage is good/normal(between 3.4V and 4.2V) */
	BATTERY_LEVEL_GOOD,
	/* The battery voltage is up to full (close to 4.2V) */
	BATTERY_LEVEL_FULL,
	/* Invalid battery voltage level. */
	BATTERY_LEVEL_INVALID
};

#ifndef CONFIG_BATTERY_MSM_FAKE
struct rpc_reply_batt_chg_v1 {
	struct rpc_reply_hdr hdr;
	u32 	more_data;

	u32	charger_status;
	u32	charger_type;
	u32	battery_status;
	u32	battery_level;
	u32     battery_voltage;
	u32	battery_temp;
};

struct rpc_reply_batt_chg_v2 {
	struct rpc_reply_batt_chg_v1	v1;

	u32	is_charger_valid;
	u32	is_charging;
	u32	is_battery_valid;
	u32	ui_event;
};

union rpc_reply_batt_chg {
	struct rpc_reply_batt_chg_v1	v1;
	struct rpc_reply_batt_chg_v2	v2;
};

static union rpc_reply_batt_chg rep_batt_chg;
#endif

struct msm_battery_info {
	u32 voltage_max_design;
	u32 voltage_min_design;
	u32 chg_api_version;
	u32 batt_technology;
	u32 batt_api_version;

	u32 avail_chg_sources;
	u32 current_chg_source;

	u32 batt_status;
	u32 batt_health;
	u32 charger_valid;
	u32 batt_valid;
	u32 batt_capacity; /* in percentage */

	u32 charger_status;
	u32 charger_type;
	u32 battery_status;
	u32 battery_level;
	u32 battery_voltage; /* in millie volts */
	u32 battery_temp;  /* in celsius */

	u32(*calculate_capacity) (u32 voltage);

	s32 batt_handle;

	struct power_supply *msm_psy_ac;
	struct power_supply *msm_psy_usb;
	struct power_supply *msm_psy_batt;
	struct power_supply *current_ps;

	struct msm_rpc_client *batt_client;
	struct msm_rpc_endpoint *chg_ep;

	wait_queue_head_t wait_q;

	u32 vbatt_modify_reply_avail;

	struct early_suspend early_suspend;
};

static struct msm_battery_info msm_batt_info = {
	.batt_handle = INVALID_BATT_HANDLE,
	.charger_status = CHARGER_STATUS_BAD,
	.charger_type = CHARGER_TYPE_INVALID,
	.battery_status = BATTERY_STATUS_GOOD,
	.battery_level = BATTERY_LEVEL_FULL,
	.battery_voltage = BATTERY_HIGH,
	.batt_capacity = 100,
	.batt_status = POWER_SUPPLY_STATUS_DISCHARGING,
	.batt_health = POWER_SUPPLY_HEALTH_GOOD,
	.batt_valid  = 1,
	.battery_temp = 23,
	.vbatt_modify_reply_avail = 0,
};

static enum power_supply_property msm_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *msm_power_supplied_to[] = {
	"battery",
};

static unsigned int chg_led_on(int on);
static unsigned chg_led_on(int on)
{
	unsigned chg_loc = 0;
	unsigned supported = 0;
	unsigned result = 0;

	if(10 == on)
	{
		chg_loc = 10;
	}
	else if(20 == on)
	{
		chg_loc = 20;
	}

	if(1 == msm_proc_comm(PCOM_CHG_IS_CHARGING, &chg_loc, &supported))
	{
		if(1 == supported)
		{
			result = chg_loc;
		}
	}

	printk("BATT: Check if chg_led_on (%d)(%d)(%d)\n", chg_loc, supported, result);
	return result;
}


static int msm_power_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
			val->intval = msm_batt_info.current_chg_source & AC_CHG
			    ? 1 : 0;
		}
		if (psy->type == POWER_SUPPLY_TYPE_USB) {
			val->intval = msm_batt_info.current_chg_source & USB_CHG
			    ? 1 : 0;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct power_supply msm_psy_ac = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.supplied_to = msm_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(msm_power_supplied_to),
	.properties = msm_power_props,
	.num_properties = ARRAY_SIZE(msm_power_props),
	.get_property = msm_power_get_property,
};

static struct power_supply msm_psy_usb = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.supplied_to = msm_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(msm_power_supplied_to),
	.properties = msm_power_props,
	.num_properties = ARRAY_SIZE(msm_power_props),
	.get_property = msm_power_get_property,
};

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
#if 1	//	Teddy 2011-12-28
	POWER_SUPPLY_PROP_TEMP,
#endif
};

static int msm_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = msm_batt_info.batt_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = msm_batt_info.batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = msm_batt_info.batt_valid;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = msm_batt_info.batt_technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = msm_batt_info.voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = msm_batt_info.voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = msm_batt_info.battery_voltage * 1000;	//	Teddy 2011-12-28
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = msm_batt_info.batt_capacity;
		break;
#if 1	//	Teddy 2011-12-28
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = msm_batt_info.battery_temp * 10;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

static struct power_supply msm_psy_batt = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = msm_batt_power_props,
	.num_properties = ARRAY_SIZE(msm_batt_power_props),
	.get_property = msm_batt_power_get_property,
};

#ifndef CONFIG_BATTERY_MSM_FAKE
struct msm_batt_get_volt_ret_data {
	u32 battery_voltage;
	u32 battery_temperature;
};

static int msm_batt_get_volt_ret_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct msm_batt_get_volt_ret_data *data_ptr, *buf_ptr;

	data_ptr = (struct msm_batt_get_volt_ret_data *)data;
	buf_ptr = (struct msm_batt_get_volt_ret_data *)buf;

	data_ptr->battery_voltage = be32_to_cpu(buf_ptr->battery_voltage);
	data_ptr->battery_temperature = be32_to_cpu(buf_ptr->battery_temperature); // BR.Hwang 2012-05-23 Get the temperature from CP.

	return 0;
}

static u32 msm_batt_get_vbatt_voltage(void)
{
	int rc;
	u32 result;

	struct msm_batt_get_volt_ret_data rep;

	rc = msm_rpc_client_req(msm_batt_info.batt_client,
			BATTERY_READ_MV_PROC,
			NULL, NULL,
			msm_batt_get_volt_ret_func, &rep,
			msecs_to_jiffies(BATT_RPC_TIMEOUT));

	if (rc < 0) {
		pr_err("%s: FAIL: vbatt get volt. rc=%d\n", __func__, rc);
		return 0;
	}

	result = (rep.battery_temperature << 16) | rep.battery_voltage;

	/*return rep.battery_voltage;*/
	return result;
}


#define	be32_to_cpu_self(v)	(v = be32_to_cpu(v))

#ifdef USE_FUEL_GAUGE // BR.Hwang 2012-01-20 Use fuel gauge instead of batt_adc.
int read_fuel_gauge_voltage(void);
int read_fuel_gauge_percentage(void);
int read_fuel_gauge_voltage_and_percentage(unsigned int *voltage, unsigned int *percentage);
static u32 msm_batt_capacity(u32 current_voltage);
#endif

static int msm_batt_get_batt_chg_status(void)
{
	int rc;

	struct rpc_req_batt_chg {
		struct rpc_request_hdr hdr;
		u32 more_data;
	} req_batt_chg;
	struct rpc_reply_batt_chg_v1 *v1p;

	req_batt_chg.more_data = cpu_to_be32(1);

	memset(&rep_batt_chg, 0, sizeof(rep_batt_chg));

	v1p = &rep_batt_chg.v1;
	rc = msm_rpc_call_reply(msm_batt_info.chg_ep,
				ONCRPC_CHG_GET_GENERAL_STATUS_PROC,
				&req_batt_chg, sizeof(req_batt_chg),
				&rep_batt_chg, sizeof(rep_batt_chg),
				msecs_to_jiffies(BATT_RPC_TIMEOUT));


	if (rc < 0) {
		pr_err("%s: ERROR. msm_rpc_call_reply failed! proc=%d rc=%d\n",
		       __func__, ONCRPC_CHG_GET_GENERAL_STATUS_PROC, rc);
		return rc;
	} else if (be32_to_cpu(v1p->more_data)) {
		be32_to_cpu_self(v1p->charger_status);
		be32_to_cpu_self(v1p->charger_type);
		be32_to_cpu_self(v1p->battery_status);
		be32_to_cpu_self(v1p->battery_level);
		be32_to_cpu_self(v1p->battery_voltage);
		be32_to_cpu_self(v1p->battery_temp);
	} else {
		pr_err("%s: No battery/charger data in RPC reply\n", __func__);
		return -EIO;
	}

	return 0;
}

static int enable_limit_flag = 1;

void enable_limit(void)
{
	enable_limit_flag = 1;
}

extern int new_initialized;
static void msm_batt_update_psy_status(void)
{
	static u32 unnecessary_event_count;
	u32	charger_status;
	u32	charger_type;
	u32	battery_status;
	u32	battery_level;
	u32	battery_voltage;
	u32	battery_temp;
	struct	power_supply	*supp;
#ifdef USE_FUEL_GAUGE // BR.Hwang 2012-01-20 Use fuel gauge instead of batt_adc.
	unsigned int fuel_voltage = 4200, fuel_percentage = 100;
	static unsigned int prev_batt_capacity = 0;
	static unsigned int full_batt_capacity_count = 0;
	static unsigned int prev_led_on = 0;
#define BATT_AVERAGE_WINDOW_SIZE 4
#define BATT_AVERAGE_ACCURACY 10
	static u32 batt_level_history[BATT_AVERAGE_WINDOW_SIZE] = {0,0,0,0};
	static u32 batt_level_history_index = 0;
	unsigned int upper_limit = 0;
	unsigned int below_limit = 0;
#ifdef UPDATE_FUEL_GAUGE
	int debug_adc_voltage = 0; // BR.Hwang 2012-03-21 debug adc voltage
	int debug_adc_capacity = 0; // BR.Hwang 2012-03-21 debug adc capacity
#endif
#endif
	unsigned int fuel_100 = 0;

	if (msm_batt_get_batt_chg_status())
		return;

	charger_status = rep_batt_chg.v1.charger_status;
	charger_type = rep_batt_chg.v1.charger_type;
	battery_status = rep_batt_chg.v1.battery_status;
	battery_level = rep_batt_chg.v1.battery_level;
	battery_voltage = rep_batt_chg.v1.battery_voltage;

#ifdef USE_FUEL_GAUGE // BR.Hwang 2012-01-20 Use fuel gauge instead of batt_adc.
	/*battery_voltage = read_fuel_gauge_voltage();*/
	read_fuel_gauge_voltage_and_percentage(&fuel_voltage, &fuel_percentage);
//	battery_voltage = fuel_voltage;
#ifdef UPDATE_FUEL_GAUGE
	debug_adc_voltage = rep_batt_chg.v1.battery_voltage;
	DBG_LIMIT("BATT: voltage(%d) from fuel gauge. DEBUG adc_voltage [%dmV]\n", fuel_voltage, debug_adc_voltage);

	// BR.Hwang 2012-04-16 test (use adc_voltage)
	/*
	 *battery_voltage = debug_adc_voltage;
	 *DBG_LIMIT("BATT: voltage(%d) from fuel gauge. DEBUG adc_voltage [%dmV]\n", battery_voltage, debug_adc_voltage);
	 */
#else
    DBG_LIMIT("BATT: voltage(%d) from fuel gauge\n", battery_voltage);
#endif
//	Teddy 2012-10-22	fuel_percentage = (fuel_percentage > 100 ? 100:fuel_percentage);	//	Teddy 2012-04-11
#endif

	battery_temp = rep_batt_chg.v1.battery_temp;

	/* Make correction for battery status */
	if (battery_status == BATTERY_STATUS_INVALID_v1) {
		if (msm_batt_info.chg_api_version < CHG_RPC_VER_3_1)
			battery_status = BATTERY_STATUS_INVALID;
	}

	if (charger_status == msm_batt_info.charger_status &&
		charger_type == msm_batt_info.charger_type &&
		battery_status == msm_batt_info.battery_status &&
		battery_level == msm_batt_info.battery_level &&
		battery_voltage == msm_batt_info.battery_voltage &&
#ifdef USE_FUEL_GAUGE	//	Teddy 2012-04-11
			//	Teddy 2012-10-22 remove the remark.
		fuel_percentage == msm_batt_info.batt_capacity &&
#endif	//	USE_FUEL_GAUGE
		battery_temp == msm_batt_info.battery_temp) {
		/* Got unnecessary event from Modem PMIC VBATT driver.
		 * Nothing changed in Battery or charger status.
		 */
		unnecessary_event_count++;
		if ((unnecessary_event_count % 20) == 1)
			DBG_LIMIT("BATT: same event count = %u\n",
				 unnecessary_event_count);
		return;
	}

	unnecessary_event_count = 0;

	DBG_LIMIT("BATT: rcvd: %d, %d, %d, %d; %d, %d\n",
		 charger_status, charger_type, battery_status,
		 battery_level, battery_voltage, battery_temp);

	if (battery_status == BATTERY_STATUS_INVALID &&
	    battery_level != BATTERY_LEVEL_INVALID) {
		DBG_LIMIT("BATT: change status(%d) to (%d) for level=%d\n",
			 battery_status, BATTERY_STATUS_GOOD, battery_level);
		battery_status = BATTERY_STATUS_GOOD;
	}

	if (msm_batt_info.charger_type != charger_type) {
		if (
			/*charger_type == CHARGER_TYPE_USB_WALL ||*/
			charger_type == CHARGER_TYPE_USB_PC ||
			charger_type == CHARGER_TYPE_USB_CARKIT) {
			DBG_LIMIT("BATT: USB charger plugged in\n");
			msm_batt_info.current_chg_source = USB_CHG;
			supp = &msm_psy_usb;
		} else if (charger_type == CHARGER_TYPE_WALL ||
				   charger_type == CHARGER_TYPE_USB_WALL) {
			DBG_LIMIT("BATT: AC Wall changer plugged in\n");
			msm_batt_info.current_chg_source = AC_CHG;
			supp = &msm_psy_ac;
		} else {
			if (msm_batt_info.current_chg_source & AC_CHG)
				DBG_LIMIT("BATT: AC Wall charger removed\n");
			else if (msm_batt_info.current_chg_source & USB_CHG)
				DBG_LIMIT("BATT: USB charger removed\n");
			else
				DBG_LIMIT("BATT: No charger present\n");
			msm_batt_info.current_chg_source = 0;
			supp = &msm_psy_batt;

			/* Correct charger status */
			if (charger_status != CHARGER_STATUS_INVALID) {
				DBG_LIMIT("BATT: No charging!\n");
				charger_status = CHARGER_STATUS_INVALID;
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
			}
		}
		
		{//for TSP Mark
			extern u32 TSP_Charger_Type;
			extern void ft_5306_anti_noise_enable(void);
			TSP_Charger_Type = charger_type;
			ft_5306_anti_noise_enable();
		}		

	} else
		supp = NULL;

	if (msm_batt_info.charger_status != charger_status) {
		if (charger_status == CHARGER_STATUS_GOOD ||
		    charger_status == CHARGER_STATUS_WEAK) {
			if (msm_batt_info.current_chg_source) {
				DBG_LIMIT("BATT: Charging.\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_CHARGING;

				/* Correct when supp==NULL */
				if (msm_batt_info.current_chg_source & AC_CHG)
					supp = &msm_psy_ac;
				else
					supp = &msm_psy_usb;
			}
		} else {
			DBG_LIMIT("BATT: No charging.\n");
			msm_batt_info.batt_status =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
			supp = &msm_psy_batt;
		}
	} else {
		/* Correct charger status */
		if (charger_type != CHARGER_TYPE_INVALID &&
		    charger_status == CHARGER_STATUS_GOOD) {
			DBG_LIMIT("BATT: In charging\n");
			msm_batt_info.batt_status =
				POWER_SUPPLY_STATUS_CHARGING;
		}
	}

	/* Correct battery voltage and status */
	if (!battery_voltage) {
		if (charger_status == CHARGER_STATUS_INVALID) {
			DBG_LIMIT("BATT: Read VBATT\n");
			{
				u32 temp = 0;
				temp = msm_batt_get_vbatt_voltage();
				battery_voltage = temp & 0x0000FFFF;
				/*battery_temp = temp >> 16; // BR.Hwang 2012-05-22 Use the temperature of vbatt.*/
			}
		} else
			/* Use previous */
			battery_voltage = msm_batt_info.battery_voltage;
	}
	if (battery_status == BATTERY_STATUS_INVALID) {
		if (battery_voltage >= msm_batt_info.voltage_min_design &&
		    battery_voltage <= msm_batt_info.voltage_max_design) {
			DBG_LIMIT("BATT: Battery valid\n");
			msm_batt_info.batt_valid = 1;
			battery_status = BATTERY_STATUS_GOOD;
		}
	}

	if (msm_batt_info.battery_status != battery_status) {
		if (battery_status != BATTERY_STATUS_INVALID) {
			msm_batt_info.batt_valid = 1;

			if (battery_status == BATTERY_STATUS_BAD) {
				DBG_LIMIT("BATT: Battery bad.\n");
				msm_batt_info.batt_health =
					POWER_SUPPLY_HEALTH_DEAD;
			} else if (battery_status == BATTERY_STATUS_BAD_TEMP) {
				DBG_LIMIT("BATT: Battery overheat.\n");
				msm_batt_info.batt_health =
					POWER_SUPPLY_HEALTH_OVERHEAT;
			} else {
				DBG_LIMIT("BATT: Battery good.\n");
				msm_batt_info.batt_health =
					POWER_SUPPLY_HEALTH_GOOD;
			}
		} else {
			msm_batt_info.batt_valid = 0;
			DBG_LIMIT("BATT: Battery invalid.\n");
			msm_batt_info.batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		}

		if (msm_batt_info.batt_status != POWER_SUPPLY_STATUS_CHARGING) {
			if (battery_status == BATTERY_STATUS_INVALID) {
				DBG_LIMIT("BATT: Battery -> unknown\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_UNKNOWN;
			} else {
				DBG_LIMIT("BATT: Battery -> discharging\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_DISCHARGING;
			}
		}

		if (!supp) {
			if (msm_batt_info.current_chg_source) {
				if (msm_batt_info.current_chg_source & AC_CHG)
					supp = &msm_psy_ac;
				else
					supp = &msm_psy_usb;
			} else
				supp = &msm_psy_batt;
		}
	}

	msm_batt_info.charger_status 	= charger_status;
	msm_batt_info.charger_type 	= charger_type;
	msm_batt_info.battery_status 	= battery_status;
	msm_batt_info.battery_level 	= battery_level;
	msm_batt_info.battery_temp 	= battery_temp;

#if 0
	DBG_LIMIT("BATT2: test1\n");
	{
		switch(msm_batt_info.batt_health)
		{
		case POWER_SUPPLY_HEALTH_UNKNOWN: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_UNKNOWN (%d)\n", msm_batt_info.batt_health); break;
		case POWER_SUPPLY_HEALTH_GOOD: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_GOOD (%d)\n", msm_batt_info.batt_health); break;
		case POWER_SUPPLY_HEALTH_OVERHEAT: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_OVERHEAT (%d)\n", msm_batt_info.batt_health); break;
		case POWER_SUPPLY_HEALTH_OVERVOLTAGE: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_OVERVOLTAGE (%d)\n", msm_batt_info.batt_health); break;
		case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE: DBG_LIMIT("BATT2: M.batt_health POWER_SUPPLY_HEALTH_UNSPEC_FAILURE (%d)\n", msm_batt_info.batt_health); break;
		default: DBG_LIMIT("BATT2: M.batt_health ? (%d)\n", msm_batt_info.batt_health); break;
		}
		DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);
	}
	DBG_LIMIT("BATT2: test1 a\n");
	{
		switch(msm_batt_info.battery_status)
		{
		case BATTERY_STATUS_GOOD: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_GOOD (%d)\n", msm_batt_info.battery_status); break;
		case BATTERY_STATUS_BAD_TEMP: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_BAD_TEMP (%d)\n", msm_batt_info.battery_status); break;
		case BATTERY_STATUS_BAD: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_BAD (%d)\n", msm_batt_info.battery_status); break;
		case BATTERY_STATUS_REMOVED: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_REMOVED (%d)\n", msm_batt_info.battery_status); break;
		case BATTERY_STATUS_INVALID: DBG_LIMIT("BATT2: M.battery_status BATTERY_STATUS_INVALID (%d)\n", msm_batt_info.battery_status); break;
		default: DBG_LIMIT("BATT2: M.battery_status ? (%d)\n", msm_batt_info.battery_status); break;
		}
		DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);
	}
	DBG_LIMIT("BATT2: test1 b\n");
	{
		switch(msm_batt_info.batt_status)
		{
		case POWER_SUPPLY_STATUS_UNKNOWN: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_UNKNOWN (%d)\n", msm_batt_info.batt_status); break;
		case POWER_SUPPLY_STATUS_CHARGING: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_CHARGING (%d)\n", msm_batt_info.batt_status); break;
		case POWER_SUPPLY_STATUS_DISCHARGING: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_DISCHARGING (%d)\n", msm_batt_info.batt_status); break;
		case POWER_SUPPLY_STATUS_NOT_CHARGING: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_NOT_CHARGING (%d)\n", msm_batt_info.batt_status); break;
		case POWER_SUPPLY_STATUS_FULL: DBG_LIMIT("BATT2: M.batt_status POWER_SUPPLY_STATUS_FULL (%d)\n", msm_batt_info.batt_status); break;
		default: DBG_LIMIT("BATT2: M.batt_status ? (%d)\n", msm_batt_info.batt_status); break;
		}
		DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);
	}

	DBG_LIMIT("BATT2: test1 c\n");
	{
		switch(msm_batt_info.battery_level)
		{
		case BATTERY_LEVEL_DEAD: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_DEAD (%d)\n", msm_batt_info.battery_level); break;
		case BATTERY_LEVEL_WEAK: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_WEAK (%d)\n", msm_batt_info.battery_level); break;
		case BATTERY_LEVEL_GOOD: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_GOOD (%d)\n", msm_batt_info.battery_level); break;
		case BATTERY_LEVEL_FULL: DBG_LIMIT("BATT2: M.battery_level BATTERY_LEVEL_FULL (%d)\n", msm_batt_info.battery_level); break;
		default: DBG_LIMIT("BATT2: M.battery_level ? (%d)\n", msm_batt_info.battery_level); break;
		}
		DBG_LIMIT("BATT2: (%d) (U:%d D:%d), LEVEL(%d)\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_capacity);
	}
	DBG_LIMIT("BATT2: test1\n");
#endif

	if(debug_mask & DEBUG_DETAIL)
	{
		/*M_BATT_STATUS*/
		/*M_BATT_HEALTH*/
		M_BATTERY_LEVEL
		/*M_BATTERY_STATUS*/
	}

#ifdef USE_FUEL_GAUGE // BR.Hwang 2012-02-08 Check capacity, Although the battery_voltage is not changed.
	{
		int over_capacity = 0; // BR.Hwang 2012-02-16 for over_capacity test
#else
	if (msm_batt_info.battery_voltage != battery_voltage) {
#endif
		msm_batt_info.battery_voltage  	= battery_voltage;

#ifdef USE_FUEL_GAUGE // BR.Hwang 2012-01-25 Use fuel gauge instead of batt_adc.
		/*msm_batt_info.batt_capacity = read_fuel_gauge_percentage();*/
		msm_batt_info.batt_capacity = fuel_percentage;
		over_capacity = msm_batt_info.batt_capacity;
		if(msm_batt_info.batt_capacity > 100) // BR.Hwang 2012-01-30 percentage limit
		{
			msm_batt_info.batt_capacity = 100;
		}
#ifdef UPDATE_FUEL_GAUGE
		debug_adc_capacity = msm_batt_capacity(debug_adc_voltage);
		DBG_LIMIT("BATT: voltage = %u mV [capacity = %d%%(%d%%)] from fuel gauge. DEBUG adc_capacity [%d%%]\n",
			 battery_voltage, msm_batt_info.batt_capacity, over_capacity, debug_adc_capacity);

		fuel_100 = 0;
		if(100 == msm_batt_info.batt_capacity)
		{
			fuel_100 = 100;
			DBG_LIMIT("BATT: Fuel-Gauge FULL\n");
		}

		// BR.Hwang 2012-04-25 100% if battery level is full.
		if(BATTERY_LEVEL_FULL == msm_batt_info.battery_level)
		{
			msm_batt_info.batt_capacity = 100;
			DBG_LIMIT("BATT: FULL\n");
		}

		// BR.Hwang 2012-06-25 The voltage will be checked to determine 100%.
		//-------------------------------------------------------------------------------
		if(battery_voltage > 4170)
		{
			full_batt_capacity_count++;

			DBG_LIMIT("BATT: batt_capacity(%3d) full_batt_capacity_count(%2d) battery_voltage(%4d)\n", msm_batt_info.batt_capacity, full_batt_capacity_count, battery_voltage);

			if(fuel_100 != 100)
			{
				if(full_batt_capacity_count > 5)
				{
					switch (msm_batt_info.batt_capacity) {
					case 93:  msm_batt_info.batt_capacity = 97;	 break;
					case 94:  msm_batt_info.batt_capacity = 98;	 break;
					case 95:  msm_batt_info.batt_capacity = 99;	 break;
					case 96:  msm_batt_info.batt_capacity = 100; break;
					case 97:  msm_batt_info.batt_capacity = 100; break;
					case 98:  msm_batt_info.batt_capacity = 100; break;
					case 99:  msm_batt_info.batt_capacity = 100; break;
					case 100: msm_batt_info.batt_capacity = 100; break;
					default: break;
					}
				}
			}

			DBG_LIMIT("BATT: batt_capacity(%3d) full_batt_capacity_count(%2d) battery_voltage(%4d)\n", msm_batt_info.batt_capacity, full_batt_capacity_count, battery_voltage);
		}
		else
		{
			full_batt_capacity_count = 0;
		}
		//-------------------------------------------------------------------------------

		if(0 == prev_batt_capacity)
		{
			prev_batt_capacity = msm_batt_info.batt_capacity;
			prev_led_on = 0;
		}

		// BR.Hwang 2012-06-24 use a average of batt_level
		//-------------------------------------------------------------------------------
		{
			int i = 0;
			int first_time = 1;
			u32 new_value = msm_batt_info.batt_capacity;
			u32 accumulator = 0;

			for (i = 0; i < BATT_AVERAGE_WINDOW_SIZE; i++)
			{
				if(batt_level_history[i] != 0)
				{
					first_time = 0;
					break;
				}
			}

			if(1 == new_initialized)
			{
				new_initialized = 0;
				first_time = 1;
				DBG_LIMIT("BATT: new_initialized was used. (%d, %d)\n", new_initialized, first_time);
			}

			if(debug_mask & DEBUG_DETAIL)
			{
#if BATT_AVERAGE_WINDOW_SIZE == 4
				DBG_LIMIT("BATT: H(%4d,%4d,%4d,%4d)[%d] ", batt_level_history[0], batt_level_history[1], batt_level_history[2], batt_level_history[3], batt_level_history_index);
#elif BATT_AVERAGE_WINDOW_SIZE == 5
				DBG_LIMIT("BATT: H(%4d,%4d,%4d,%4d,%4d)[%d] ", batt_level_history[0], batt_level_history[1], batt_level_history[2], batt_level_history[3],	  batt_level_history[4], batt_level_history_index);
#endif
			}

			if(BATTERY_LEVEL_FULL == msm_batt_info.battery_level)
			{
				for (i = 0; i < BATT_AVERAGE_WINDOW_SIZE; i++)
				{
					batt_level_history[i] = 100 * BATT_AVERAGE_ACCURACY;
				}
			}
			else if(100 == fuel_100)
			{
				for (i = 0; i < BATT_AVERAGE_WINDOW_SIZE; i++)
				{
					batt_level_history[i] = 100 * BATT_AVERAGE_ACCURACY;
				}
			}
			else if(1 == first_time)
			{
				for (i = 0; i < BATT_AVERAGE_WINDOW_SIZE; i++)
				{
					batt_level_history[i] = new_value * BATT_AVERAGE_ACCURACY;
				}
			}
			else
			{
				batt_level_history[batt_level_history_index] = new_value * BATT_AVERAGE_ACCURACY;
				batt_level_history_index++;
				if(batt_level_history_index >= BATT_AVERAGE_WINDOW_SIZE)
				{
					batt_level_history_index = 0;
				}
			}

            if(debug_mask & DEBUG_DETAIL)
			{
#if BATT_AVERAGE_WINDOW_SIZE == 4
				DBG_LIMIT("		 H(%4d,%4d,%4d,%4d)[%d] ", batt_level_history[0], batt_level_history[1], batt_level_history[2], batt_level_history[3], batt_level_history_index);
#elif BATT_AVERAGE_WINDOW_SIZE == 5
				DBG_LIMIT("		 H(%4d,%4d,%4d,%4d,%4d)[%d] ", batt_level_history[0], batt_level_history[1], batt_level_history[2], batt_level_history[3],	batt_level_history[4], batt_level_history_index);
#endif
			}

			for (i = 0; i < BATT_AVERAGE_WINDOW_SIZE; i++)
			{
				accumulator = accumulator + batt_level_history[i];
			}

			accumulator = accumulator / (BATT_AVERAGE_WINDOW_SIZE * BATT_AVERAGE_ACCURACY);

#if 0
			{
				int found = 0;
				int temp_index = 0;
				temp_index = batt_level_history_index;

				if(0 == temp_index)
				{
					temp_index = BATT_AVERAGE_WINDOW_SIZE - 1;
				}
				else
				{
					temp_index--;
				}

				if((100 * BATT_AVERAGE_ACCURACY) == batt_level_history[temp_index])
				{
					found++;
				}

				if(0 == temp_index)
				{
					temp_index = BATT_AVERAGE_WINDOW_SIZE - 1;
				}
				else
				{
					temp_index--;
				}

				if((100 * BATT_AVERAGE_ACCURACY) == batt_level_history[temp_index])
				{
					found++;
				}

				if(2 == found)
				{
					accumulator = 100;
				}
			}
#endif

#if 1
			{
				int found = 0;

				for (i = 0; i < BATT_AVERAGE_WINDOW_SIZE; i++)
				{
					if((100 * BATT_AVERAGE_ACCURACY) == batt_level_history[i])
					{
						found++;
					}
				}

				if(found >= (BATT_AVERAGE_WINDOW_SIZE-1))
				{
					accumulator = 100;
					DBG_LIMIT("BATT: %d x 100 FULL (%d)\n", BATT_AVERAGE_WINDOW_SIZE-1, found);
				}
			}
#endif // 0

			if(accumulator > 100)
			{
				accumulator = 100;
				DBG_LIMIT("Warning!!");
			}

			msm_batt_info.batt_capacity = accumulator;

			DBG_LIMIT("new_value(%4d) batt_capacity(%4d)\n", new_value, msm_batt_info.batt_capacity);
		}
		//-------------------------------------------------------------------------------

		if(msm_batt_info.batt_capacity >= 100)
		{
			chg_led_on(20); // off
			if(0 == prev_led_on || 10 == prev_led_on)
			{
				prev_led_on = 20;
			}

			msm_batt_info.battery_level = BATTERY_LEVEL_FULL;

			msm_batt_info.batt_status = POWER_SUPPLY_STATUS_FULL;
			/*msm_batt_info.batt_status = POWER_SUPPLY_STATUS_NOT_CHARGING;*/

			if(debug_mask & DEBUG_DETAIL)
			{
				DBG_LIMIT("BATT: chg_led_on case1 (%d) (U:%d D:%d), %d\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_status);
			}
		}
		else if(POWER_SUPPLY_STATUS_CHARGING == msm_batt_info.batt_status)
		{
			chg_led_on(10); // on
			if(0 == prev_led_on || 20 == prev_led_on)
			{
				prev_led_on = 10;
			}
			upper_limit = 0;
			below_limit = 0;

			if(debug_mask & DEBUG_DETAIL)
			{
				DBG_LIMIT("BATT: chg_led_on case2 (%d) (U:%d D:%d), %d\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_status);
			}
		}
		else if(POWER_SUPPLY_STATUS_NOT_CHARGING == msm_batt_info.batt_status)
		{
			chg_led_on(20); // off
			if(0 == prev_led_on || 10 == prev_led_on)
			{
				prev_led_on = 20;
			}

			if(enable_limit_flag) // BR.Hwang 2012-05-30 Can limit after 5 minutes from bootup.
			{
				upper_limit = 1;
			}
			else
			{
				upper_limit = 0;
			}
			below_limit = 0;

			msm_batt_info.batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
			if(debug_mask & DEBUG_DETAIL)
			{
				DBG_LIMIT("BATT: chg_led_on case3 (%d) (U:%d D:%d), %d\n", prev_led_on, upper_limit, below_limit, msm_batt_info.batt_status);
			}
		}

		DBG_LIMIT("BATT: charger_status(%d)", msm_batt_info.charger_status);
		if (msm_batt_info.charger_status == CHARGER_STATUS_INVALID) // charger is not connected.
		{
			if(10 == prev_led_on)
			{
				if(debug_mask & DEBUG_DETAIL)
				{
					DBG_LIMIT("BATT: Charger is not connected. prev_led_on(%d) NOT OK", prev_led_on);
				}
			}
			else
			{
				if(debug_mask & DEBUG_DETAIL)
				{
					DBG_LIMIT("BATT: Charger is not connected. prev_led_on(%d) OK", prev_led_on);
				}
			}

			chg_led_on(20); // off
			prev_led_on = 20;
			msm_batt_info.batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
		}

		DBG_LIMIT("BATT: prev_batt_capacity (%d)\n", prev_batt_capacity);
		/*if (1 == upper_limit)*/
		if (upper_limit)
		{
			DBG_LIMIT("BATT: upper_limit\n");
			if(msm_batt_info.batt_capacity > prev_batt_capacity)
			{
				DBG_LIMIT("BATT: No charging, upper limit is prev_batt_capacity(%d%%). [%d%%]\n", prev_batt_capacity, msm_batt_info.batt_capacity);
				msm_batt_info.batt_capacity = prev_batt_capacity;
			}
			else
			{
				prev_batt_capacity = msm_batt_info.batt_capacity;
			}
		}
		/*else if (1 == below_limit)*/
		else if (below_limit)
		{
			DBG_LIMIT("BATT: below_limit\n");
			if(msm_batt_info.batt_capacity < prev_batt_capacity)
			{
				DBG_LIMIT("BATT: In charging, below limit is prev_batt_capacity(%d%%). [%d%%] \n", prev_batt_capacity, msm_batt_info.batt_capacity);
				msm_batt_info.batt_capacity = prev_batt_capacity;
			}
			else
			{
				prev_batt_capacity = msm_batt_info.batt_capacity;
			}
		}
		else
		{
			DBG_LIMIT("BATT: no limit (%d)\n", msm_batt_info.batt_capacity);
			prev_batt_capacity = msm_batt_info.batt_capacity;
		}

		// BR.Hwang 2012-04-16 test (use adc_capacity)
		/*
		 *msm_batt_info.batt_capacity = debug_adc_capacity;
		 *DBG_LIMIT("BATT: voltage = %u mV [capacity = %d%%(%d%%)] from fuel gauge. DEBUG adc_capacity [%d%%]\n",
		 *	   battery_voltage, msm_batt_info.batt_capacity, over_capacity, debug_adc_capacity);
		 */
#else
		DBG_LIMIT("BATT: voltage = %u mV [capacity = %d%%(%d%%)] from fuel gauge\n",
			 battery_voltage, msm_batt_info.batt_capacity, over_capacity);
#endif

#else
		msm_batt_info.batt_capacity =
			msm_batt_info.calculate_capacity(battery_voltage);
		DBG_LIMIT("BATT: voltage = %u mV [capacity = %d%%]\n",
			 battery_voltage, msm_batt_info.batt_capacity);
#endif

		if (!supp)
			supp = msm_batt_info.current_ps;
	}

#if 1	//	Teddy 2012-10-22 change if 0 => if 1
	if( msm_batt_info.battery_level == BATTERY_LEVEL_FULL )
	{
		msm_batt_info.batt_capacity = 100;
		msm_batt_info.batt_status = POWER_SUPPLY_STATUS_FULL;
	}
#endif

	if(debug_mask & DEBUG_DETAIL)
	{
		DBG_LIMIT("BATT2: test2\n");
		/*M_BATT_STATUS*/
		/*M_BATT_HEALTH*/
		M_BATTERY_LEVEL
		/*M_BATTERY_STATUS*/
		DBG_LIMIT("BATT2: test2\n");
	}

	if (supp) {
		msm_batt_info.current_ps = supp;
		DBG_LIMIT("BATT: Supply = %s\n", supp->name);
		power_supply_changed(supp);
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
struct batt_modify_client_req {

	u32 client_handle;

	/* The voltage at which callback (CB) should be called. */
	u32 desired_batt_voltage;

	/* The direction when the CB should be called. */
	u32 voltage_direction;

	/* The registered callback to be called when voltage and
	 * direction specs are met. */
	u32 batt_cb_id;

	/* The call back data */
	u32 cb_data;
};

struct batt_modify_client_rep {
	u32 result;
};

static int msm_batt_modify_client_arg_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct batt_modify_client_req *batt_modify_client_req =
		(struct batt_modify_client_req *)data;
	u32 *req = (u32 *)buf;
	int size = 0;

	*req = cpu_to_be32(batt_modify_client_req->client_handle);
	size += sizeof(u32);
	req++;

	*req = cpu_to_be32(batt_modify_client_req->desired_batt_voltage);
	size += sizeof(u32);
	req++;

	*req = cpu_to_be32(batt_modify_client_req->voltage_direction);
	size += sizeof(u32);
	req++;

	*req = cpu_to_be32(batt_modify_client_req->batt_cb_id);
	size += sizeof(u32);
	req++;

	*req = cpu_to_be32(batt_modify_client_req->cb_data);
	size += sizeof(u32);

	return size;
}

static int msm_batt_modify_client_ret_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct  batt_modify_client_rep *data_ptr, *buf_ptr;

	data_ptr = (struct batt_modify_client_rep *)data;
	buf_ptr = (struct batt_modify_client_rep *)buf;

	data_ptr->result = be32_to_cpu(buf_ptr->result);

	return 0;
}

static int msm_batt_modify_client(u32 client_handle, u32 desired_batt_voltage,
	     u32 voltage_direction, u32 batt_cb_id, u32 cb_data)
{
	int rc;

	struct batt_modify_client_req  req;
	struct batt_modify_client_rep rep;

	req.client_handle = client_handle;
	req.desired_batt_voltage = desired_batt_voltage;
	req.voltage_direction = voltage_direction;
	req.batt_cb_id = batt_cb_id;
	req.cb_data = cb_data;

	rc = msm_rpc_client_req(msm_batt_info.batt_client,
			BATTERY_MODIFY_CLIENT_PROC,
			msm_batt_modify_client_arg_func, &req,
			msm_batt_modify_client_ret_func, &rep,
			msecs_to_jiffies(BATT_RPC_TIMEOUT));

	if (rc < 0) {
		pr_err("%s: ERROR. failed to modify  Vbatt client\n",
		       __func__);
		return rc;
	}

	if (rep.result != BATTERY_MODIFICATION_SUCCESSFUL) {
		pr_err("%s: ERROR. modify client failed. result = %u\n",
		       __func__, rep.result);
		return -EIO;
	}

	return 0;
}

void msm_batt_early_suspend(struct early_suspend *h)
{
	int rc;

	pr_debug("%s: enter\n", __func__);

	if (msm_batt_info.batt_handle != INVALID_BATT_HANDLE) {
#if 1	//	Teddy 2011-12-28
		rc = msm_batt_modify_client(msm_batt_info.batt_handle,
				BATTERY_HIGH, BATTERY_ALL_ACTIVITY,
				BATTERY_CB_ID_LOW_VOL, BATTERY_LOW);
#else
		rc = msm_batt_modify_client(msm_batt_info.batt_handle,
				BATTERY_LOW, BATTERY_VOLTAGE_BELOW_THIS_LEVEL,
				BATTERY_CB_ID_LOW_VOL, BATTERY_LOW);
#endif

		if (rc < 0) {
			pr_err("%s: msm_batt_modify_client. rc=%d\n",
			       __func__, rc);
			return;
		}
	} else {
		pr_err("%s: ERROR. invalid batt_handle\n", __func__);
		return;
	}

	pr_debug("%s: exit\n", __func__);
}

void msm_batt_late_resume(struct early_suspend *h)
{
	int rc;

	pr_debug("%s: enter\n", __func__);

	if (msm_batt_info.batt_handle != INVALID_BATT_HANDLE) {
		rc = msm_batt_modify_client(msm_batt_info.batt_handle,
				BATTERY_LOW, BATTERY_ALL_ACTIVITY,
			       BATTERY_CB_ID_ALL_ACTIV, BATTERY_ALL_ACTIVITY);
		if (rc < 0) {
			pr_err("%s: msm_batt_modify_client FAIL rc=%d\n",
			       __func__, rc);
			return;
		}
	} else {
		pr_err("%s: ERROR. invalid batt_handle\n", __func__);
		return;
	}

	msm_batt_update_psy_status();
	pr_debug("%s: exit\n", __func__);
}
#endif

struct msm_batt_vbatt_filter_req {
	u32 batt_handle;
	u32 enable_filter;
	u32 vbatt_filter;
};

struct msm_batt_vbatt_filter_rep {
	u32 result;
};

static int msm_batt_filter_arg_func(struct msm_rpc_client *batt_client,

		void *buf, void *data)
{
	struct msm_batt_vbatt_filter_req *vbatt_filter_req =
		(struct msm_batt_vbatt_filter_req *)data;
	u32 *req = (u32 *)buf;
	int size = 0;

	*req = cpu_to_be32(vbatt_filter_req->batt_handle);
	size += sizeof(u32);
	req++;

	*req = cpu_to_be32(vbatt_filter_req->enable_filter);
	size += sizeof(u32);
	req++;

	*req = cpu_to_be32(vbatt_filter_req->vbatt_filter);
	size += sizeof(u32);
	return size;
}

static int msm_batt_filter_ret_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{

	struct msm_batt_vbatt_filter_rep *data_ptr, *buf_ptr;

	data_ptr = (struct msm_batt_vbatt_filter_rep *)data;
	buf_ptr = (struct msm_batt_vbatt_filter_rep *)buf;

	data_ptr->result = be32_to_cpu(buf_ptr->result);
	return 0;
}

static int msm_batt_enable_filter(u32 vbatt_filter)
{
	int rc;
	struct  msm_batt_vbatt_filter_req  vbatt_filter_req;
	struct  msm_batt_vbatt_filter_rep  vbatt_filter_rep;

	vbatt_filter_req.batt_handle = msm_batt_info.batt_handle;
	vbatt_filter_req.enable_filter = 1;
	vbatt_filter_req.vbatt_filter = vbatt_filter;

	rc = msm_rpc_client_req(msm_batt_info.batt_client,
			BATTERY_ENABLE_DISABLE_FILTER_PROC,
			msm_batt_filter_arg_func, &vbatt_filter_req,
			msm_batt_filter_ret_func, &vbatt_filter_rep,
			msecs_to_jiffies(BATT_RPC_TIMEOUT));

	if (rc < 0) {
		pr_err("%s: FAIL: enable vbatt filter. rc=%d\n",
		       __func__, rc);
		return rc;
	}

	if (vbatt_filter_rep.result != BATTERY_DEREGISTRATION_SUCCESSFUL) {
		pr_err("%s: FAIL: enable vbatt filter: result=%d\n",
		       __func__, vbatt_filter_rep.result);
		return -EIO;
	}

	pr_debug("%s: enable vbatt filter: OK\n", __func__);
	return rc;
}

struct batt_client_registration_req {
	/* The voltage at which callback (CB) should be called. */
	u32 desired_batt_voltage;

	/* The direction when the CB should be called. */
	u32 voltage_direction;

	/* The registered callback to be called when voltage and
	 * direction specs are met. */
	u32 batt_cb_id;

	/* The call back data */
	u32 cb_data;
	u32 more_data;
	u32 batt_error;
};

struct batt_client_registration_req_4_1 {
	/* The voltage at which callback (CB) should be called. */
	u32 desired_batt_voltage;

	/* The direction when the CB should be called. */
	u32 voltage_direction;

	/* The registered callback to be called when voltage and
	 * direction specs are met. */
	u32 batt_cb_id;

	/* The call back data */
	u32 cb_data;
	u32 batt_error;
};

struct batt_client_registration_rep {
	u32 batt_handle;
};

struct batt_client_registration_rep_4_1 {
	u32 batt_handle;
	u32 more_data;
	u32 err;
};

static int msm_batt_register_arg_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct batt_client_registration_req *batt_reg_req =
		(struct batt_client_registration_req *)data;

	u32 *req = (u32 *)buf;
	int size = 0;


	if (msm_batt_info.batt_api_version == BATTERY_RPC_VER_4_1) {
		*req = cpu_to_be32(batt_reg_req->desired_batt_voltage);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->voltage_direction);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->batt_cb_id);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->cb_data);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->batt_error);
		size += sizeof(u32);

		return size;
	} else {
		*req = cpu_to_be32(batt_reg_req->desired_batt_voltage);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->voltage_direction);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->batt_cb_id);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->cb_data);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->more_data);
		size += sizeof(u32);
		req++;

		*req = cpu_to_be32(batt_reg_req->batt_error);
		size += sizeof(u32);

		return size;
	}

}

static int msm_batt_register_ret_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct batt_client_registration_rep *data_ptr, *buf_ptr;
	struct batt_client_registration_rep_4_1 *data_ptr_4_1, *buf_ptr_4_1;

	if (msm_batt_info.batt_api_version == BATTERY_RPC_VER_4_1) {
		data_ptr_4_1 = (struct batt_client_registration_rep_4_1 *)data;
		buf_ptr_4_1 = (struct batt_client_registration_rep_4_1 *)buf;

		data_ptr_4_1->batt_handle
			= be32_to_cpu(buf_ptr_4_1->batt_handle);
		data_ptr_4_1->more_data
			= be32_to_cpu(buf_ptr_4_1->more_data);
		data_ptr_4_1->err = be32_to_cpu(buf_ptr_4_1->err);
		return 0;
	} else {
		data_ptr = (struct batt_client_registration_rep *)data;
		buf_ptr = (struct batt_client_registration_rep *)buf;

		data_ptr->batt_handle = be32_to_cpu(buf_ptr->batt_handle);
		return 0;
	}
}

static int msm_batt_register(u32 desired_batt_voltage,
			     u32 voltage_direction, u32 batt_cb_id, u32 cb_data)
{
	struct batt_client_registration_req batt_reg_req;
	struct batt_client_registration_req_4_1 batt_reg_req_4_1;
	struct batt_client_registration_rep batt_reg_rep;
	struct batt_client_registration_rep_4_1 batt_reg_rep_4_1;
	void *request;
	void *reply;
	int rc;

	if (msm_batt_info.batt_api_version == BATTERY_RPC_VER_4_1) {
		batt_reg_req_4_1.desired_batt_voltage = desired_batt_voltage;
		batt_reg_req_4_1.voltage_direction = voltage_direction;
		batt_reg_req_4_1.batt_cb_id = batt_cb_id;
		batt_reg_req_4_1.cb_data = cb_data;
		batt_reg_req_4_1.batt_error = 1;
		request = &batt_reg_req_4_1;
	} else {
		batt_reg_req.desired_batt_voltage = desired_batt_voltage;
		batt_reg_req.voltage_direction = voltage_direction;
		batt_reg_req.batt_cb_id = batt_cb_id;
		batt_reg_req.cb_data = cb_data;
		batt_reg_req.more_data = 1;
		batt_reg_req.batt_error = 0;
		request = &batt_reg_req;
	}

	if (msm_batt_info.batt_api_version == BATTERY_RPC_VER_4_1)
		reply = &batt_reg_rep_4_1;
	else
		reply = &batt_reg_rep;

	rc = msm_rpc_client_req(msm_batt_info.batt_client,
			BATTERY_REGISTER_PROC,
			msm_batt_register_arg_func, request,
			msm_batt_register_ret_func, reply,
			msecs_to_jiffies(BATT_RPC_TIMEOUT));

	if (rc < 0) {
		pr_err("%s: FAIL: vbatt register. rc=%d\n", __func__, rc);
		return rc;
	}

	if (msm_batt_info.batt_api_version == BATTERY_RPC_VER_4_1) {
		if (batt_reg_rep_4_1.more_data != 0
			&& batt_reg_rep_4_1.err
				!= BATTERY_REGISTRATION_SUCCESSFUL) {
			pr_err("%s: vBatt Registration Failed proc_num=%d\n"
					, __func__, BATTERY_REGISTER_PROC);
			return -EIO;
		}
		msm_batt_info.batt_handle = batt_reg_rep_4_1.batt_handle;
	} else
		msm_batt_info.batt_handle = batt_reg_rep.batt_handle;

	return 0;
}

struct batt_client_deregister_req {
	u32 batt_handle;
};

struct batt_client_deregister_rep {
	u32 batt_error;
};

static int msm_batt_deregister_arg_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct batt_client_deregister_req *deregister_req =
		(struct  batt_client_deregister_req *)data;
	u32 *req = (u32 *)buf;
	int size = 0;

	*req = cpu_to_be32(deregister_req->batt_handle);
	size += sizeof(u32);

	return size;
}

static int msm_batt_deregister_ret_func(struct msm_rpc_client *batt_client,
				       void *buf, void *data)
{
	struct batt_client_deregister_rep *data_ptr, *buf_ptr;

	data_ptr = (struct batt_client_deregister_rep *)data;
	buf_ptr = (struct batt_client_deregister_rep *)buf;

	data_ptr->batt_error = be32_to_cpu(buf_ptr->batt_error);

	return 0;
}

static int msm_batt_deregister(u32 batt_handle)
{
	int rc;
	struct batt_client_deregister_req req;
	struct batt_client_deregister_rep rep;

	req.batt_handle = batt_handle;

	rc = msm_rpc_client_req(msm_batt_info.batt_client,
			BATTERY_DEREGISTER_CLIENT_PROC,
			msm_batt_deregister_arg_func, &req,
			msm_batt_deregister_ret_func, &rep,
			msecs_to_jiffies(BATT_RPC_TIMEOUT));

	if (rc < 0) {
		pr_err("%s: FAIL: vbatt deregister. rc=%d\n", __func__, rc);
		return rc;
	}

	if (rep.batt_error != BATTERY_DEREGISTRATION_SUCCESSFUL) {
		pr_err("%s: vbatt deregistration FAIL. error=%d, handle=%d\n",
		       __func__, rep.batt_error, batt_handle);
		return -EIO;
	}

	return 0;
}
#endif  /* CONFIG_BATTERY_MSM_FAKE */

static int msm_batt_cleanup(void)
{
	int rc = 0;

#ifndef CONFIG_BATTERY_MSM_FAKE
	if (msm_batt_info.batt_handle != INVALID_BATT_HANDLE) {

		rc = msm_batt_deregister(msm_batt_info.batt_handle);
		if (rc < 0)
			pr_err("%s: FAIL: msm_batt_deregister. rc=%d\n",
			       __func__, rc);
	}

	msm_batt_info.batt_handle = INVALID_BATT_HANDLE;

	if (msm_batt_info.batt_client)
		msm_rpc_unregister_client(msm_batt_info.batt_client);
#endif  /* CONFIG_BATTERY_MSM_FAKE */

	if (msm_batt_info.msm_psy_ac)
		power_supply_unregister(msm_batt_info.msm_psy_ac);

	if (msm_batt_info.msm_psy_usb)
		power_supply_unregister(msm_batt_info.msm_psy_usb);
	if (msm_batt_info.msm_psy_batt)
		power_supply_unregister(msm_batt_info.msm_psy_batt);

#ifndef CONFIG_BATTERY_MSM_FAKE
	if (msm_batt_info.chg_ep) {
		rc = msm_rpc_close(msm_batt_info.chg_ep);
		if (rc < 0) {
			pr_err("%s: FAIL. msm_rpc_close(chg_ep). rc=%d\n",
			       __func__, rc);
		}
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	if (msm_batt_info.early_suspend.suspend == msm_batt_early_suspend)
		unregister_early_suspend(&msm_batt_info.early_suspend);
#endif
#endif
	return rc;
}

static u32 msm_batt_capacity(u32 current_voltage)
{
#ifdef UPDATE_FUEL_GAUGE
	u32 low_voltage = 3200;
	u32 high_voltage = 4200;
#else
	u32 low_voltage = msm_batt_info.voltage_min_design;
	u32 high_voltage = msm_batt_info.voltage_max_design;
#endif

	if (current_voltage <= low_voltage)
		return 0;
	else if (current_voltage >= high_voltage)
		return 100;
	else
		return (current_voltage - low_voltage) * 100
			/ (high_voltage - low_voltage);
}

#ifndef CONFIG_BATTERY_MSM_FAKE
int msm_batt_get_charger_api_version(void)
{
	int rc ;
	struct rpc_reply_hdr *reply;

	struct rpc_req_chg_api_ver {
		struct rpc_request_hdr hdr;
		u32 more_data;
	} req_chg_api_ver;

	struct rpc_rep_chg_api_ver {
		struct rpc_reply_hdr hdr;
		u32 num_of_chg_api_versions;
		u32 *chg_api_versions;
	};

	u32 num_of_versions;

	struct rpc_rep_chg_api_ver *rep_chg_api_ver;


	req_chg_api_ver.more_data = cpu_to_be32(1);

	msm_rpc_setup_req(&req_chg_api_ver.hdr, CHG_RPC_PROG, CHG_RPC_VER_1_1,
			  ONCRPC_CHARGER_API_VERSIONS_PROC);

	rc = msm_rpc_write(msm_batt_info.chg_ep, &req_chg_api_ver,
			sizeof(req_chg_api_ver));
	if (rc < 0) {
		pr_err("%s: FAIL: msm_rpc_write. proc=0x%08x, rc=%d\n",
		       __func__, ONCRPC_CHARGER_API_VERSIONS_PROC, rc);
		return rc;
	}

	for (;;) {
		rc = msm_rpc_read(msm_batt_info.chg_ep, (void *) &reply, -1,
				BATT_RPC_TIMEOUT);
		if (rc < 0)
			return rc;
		if (rc < RPC_REQ_REPLY_COMMON_HEADER_SIZE) {
			pr_err("%s: LENGTH ERR: msm_rpc_read. rc=%d (<%d)\n",
			       __func__, rc, RPC_REQ_REPLY_COMMON_HEADER_SIZE);

			rc = -EIO;
			break;
		}
		/* we should not get RPC REQ or call packets -- ignore them */
		if (reply->type == RPC_TYPE_REQ) {
			pr_err("%s: TYPE ERR: type=%d (!=%d)\n",
			       __func__, reply->type, RPC_TYPE_REQ);
			kfree(reply);
			continue;
		}

		/* If an earlier call timed out, we could get the (no
		 * longer wanted) reply for it.	 Ignore replies that
		 * we don't expect
		 */
		if (reply->xid != req_chg_api_ver.hdr.xid) {
			pr_err("%s: XID ERR: xid=%d (!=%d)\n", __func__,
			       reply->xid, req_chg_api_ver.hdr.xid);
			kfree(reply);
			continue;
		}
		if (reply->reply_stat != RPCMSG_REPLYSTAT_ACCEPTED) {
			rc = -EPERM;
			break;
		}
		if (reply->data.acc_hdr.accept_stat !=
				RPC_ACCEPTSTAT_SUCCESS) {
			rc = -EINVAL;
			break;
		}

		rep_chg_api_ver = (struct rpc_rep_chg_api_ver *)reply;

		num_of_versions =
			be32_to_cpu(rep_chg_api_ver->num_of_chg_api_versions);

		rep_chg_api_ver->chg_api_versions =  (u32 *)
			((u8 *) reply + sizeof(struct rpc_reply_hdr) +
			sizeof(rep_chg_api_ver->num_of_chg_api_versions));

		rc = be32_to_cpu(
			rep_chg_api_ver->chg_api_versions[num_of_versions - 1]);

		pr_debug("%s: num_of_chg_api_versions = %u. "
			"The chg api version = 0x%08x\n", __func__,
			num_of_versions, rc);
		break;
	}
	kfree(reply);
	return rc;
}

static int msm_batt_cb_func(struct msm_rpc_client *client,
			    void *buffer, int in_size)
{
	int rc = 0;
	struct rpc_request_hdr *req;
	u32 procedure;
	u32 accept_status;

	req = (struct rpc_request_hdr *)buffer;
	procedure = be32_to_cpu(req->procedure);

	switch (procedure) {
	case BATTERY_CB_TYPE_PROC:
		accept_status = RPC_ACCEPTSTAT_SUCCESS;
		break;

	default:
		accept_status = RPC_ACCEPTSTAT_PROC_UNAVAIL;
		pr_err("%s: ERROR. procedure (%d) not supported\n",
		       __func__, procedure);
		break;
	}

	msm_rpc_start_accepted_reply(msm_batt_info.batt_client,
			be32_to_cpu(req->xid), accept_status);

	rc = msm_rpc_send_accepted_reply(msm_batt_info.batt_client, 0);
	if (rc)
		pr_err("%s: FAIL: sending reply. rc=%d\n", __func__, rc);

	if (accept_status == RPC_ACCEPTSTAT_SUCCESS)
		msm_batt_update_psy_status();

	return rc;
}
#endif  /* CONFIG_BATTERY_MSM_FAKE */

static int __devinit msm_batt_probe(struct platform_device *pdev)
{
	int rc;
	struct msm_psy_batt_pdata *pdata = pdev->dev.platform_data;

	if (pdev->id != -1) {
		dev_err(&pdev->dev,
			"%s: MSM chipsets Can only support one"
			" battery ", __func__);
		return -EINVAL;
	}

#ifndef CONFIG_BATTERY_MSM_FAKE
	if (pdata->avail_chg_sources & AC_CHG) {
#else
	{
#endif
		rc = power_supply_register(&pdev->dev, &msm_psy_ac);
		if (rc < 0) {
			dev_err(&pdev->dev,
				"%s: power_supply_register failed"
				" rc = %d\n", __func__, rc);
			msm_batt_cleanup();
			return rc;
		}
		msm_batt_info.msm_psy_ac = &msm_psy_ac;
		msm_batt_info.avail_chg_sources |= AC_CHG;
	}

	if (pdata->avail_chg_sources & USB_CHG) {
		rc = power_supply_register(&pdev->dev, &msm_psy_usb);
		if (rc < 0) {
			dev_err(&pdev->dev,
				"%s: power_supply_register failed"
				" rc = %d\n", __func__, rc);
			msm_batt_cleanup();
			return rc;
		}
		msm_batt_info.msm_psy_usb = &msm_psy_usb;
		msm_batt_info.avail_chg_sources |= USB_CHG;
	}

	if (!msm_batt_info.msm_psy_ac && !msm_batt_info.msm_psy_usb) {

		dev_err(&pdev->dev,
			"%s: No external Power supply(AC or USB)"
			"is avilable\n", __func__);
		msm_batt_cleanup();
		return -ENODEV;
	}

	msm_batt_info.voltage_max_design = pdata->voltage_max_design;
	msm_batt_info.voltage_min_design = pdata->voltage_min_design;
	msm_batt_info.batt_technology = pdata->batt_technology;
	msm_batt_info.calculate_capacity = pdata->calculate_capacity;

	if (!msm_batt_info.voltage_min_design)
		msm_batt_info.voltage_min_design = BATTERY_LOW;
	if (!msm_batt_info.voltage_max_design)
		msm_batt_info.voltage_max_design = BATTERY_HIGH;

	if (msm_batt_info.batt_technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
		msm_batt_info.batt_technology = POWER_SUPPLY_TECHNOLOGY_LION;

	if (!msm_batt_info.calculate_capacity)
		msm_batt_info.calculate_capacity = msm_batt_capacity;

	rc = power_supply_register(&pdev->dev, &msm_psy_batt);
	if (rc < 0) {
		dev_err(&pdev->dev, "%s: power_supply_register failed"
			" rc=%d\n", __func__, rc);
		msm_batt_cleanup();
		return rc;
	}
	msm_batt_info.msm_psy_batt = &msm_psy_batt;

#ifndef CONFIG_BATTERY_MSM_FAKE
	rc = msm_batt_register(BATTERY_LOW, BATTERY_ALL_ACTIVITY,
			       BATTERY_CB_ID_ALL_ACTIV, BATTERY_ALL_ACTIVITY);
	if (rc < 0) {
		dev_err(&pdev->dev,
			"%s: msm_batt_register failed rc = %d\n", __func__, rc);
		msm_batt_cleanup();
		return rc;
	}

	rc =  msm_batt_enable_filter(VBATT_FILTER);

	if (rc < 0) {
		dev_err(&pdev->dev,
			"%s: msm_batt_enable_filter failed rc = %d\n",
			__func__, rc);
		msm_batt_cleanup();
		return rc;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	msm_batt_info.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	msm_batt_info.early_suspend.suspend = msm_batt_early_suspend;
	msm_batt_info.early_suspend.resume = msm_batt_late_resume;
	register_early_suspend(&msm_batt_info.early_suspend);
#endif
	msm_batt_update_psy_status();

#else
	power_supply_changed(&msm_psy_ac);
#endif  /* CONFIG_BATTERY_MSM_FAKE */

	return 0;
}

static int __devexit msm_batt_remove(struct platform_device *pdev)
{
	int rc;
	rc = msm_batt_cleanup();

	if (rc < 0) {
		dev_err(&pdev->dev,
			"%s: msm_batt_cleanup  failed rc=%d\n", __func__, rc);
		return rc;
	}
	return 0;
}

static struct platform_driver msm_batt_driver = {
	.probe = msm_batt_probe,
	.remove = __devexit_p(msm_batt_remove),
	.driver = {
		   .name = "msm-battery",
		   .owner = THIS_MODULE,
		   },
};

static int __devinit msm_batt_init_rpc(void)
{
	int rc;

#ifdef CONFIG_BATTERY_MSM_FAKE
	pr_info("Faking MSM battery\n");
#else

	msm_batt_info.chg_ep =
		msm_rpc_connect_compatible(CHG_RPC_PROG, CHG_RPC_VER_4_1, 0);
	msm_batt_info.chg_api_version =  CHG_RPC_VER_4_1;
	if (msm_batt_info.chg_ep == NULL) {
		pr_err("%s: rpc connect CHG_RPC_PROG = NULL\n", __func__);
		return -ENODEV;
	}

	if (IS_ERR(msm_batt_info.chg_ep)) {
		msm_batt_info.chg_ep = msm_rpc_connect_compatible(
				CHG_RPC_PROG, CHG_RPC_VER_3_1, 0);
		msm_batt_info.chg_api_version =  CHG_RPC_VER_3_1;
	}
	if (IS_ERR(msm_batt_info.chg_ep)) {
		msm_batt_info.chg_ep = msm_rpc_connect_compatible(
				CHG_RPC_PROG, CHG_RPC_VER_1_1, 0);
		msm_batt_info.chg_api_version =  CHG_RPC_VER_1_1;
	}
	if (IS_ERR(msm_batt_info.chg_ep)) {
		msm_batt_info.chg_ep = msm_rpc_connect_compatible(
				CHG_RPC_PROG, CHG_RPC_VER_1_3, 0);
		msm_batt_info.chg_api_version =  CHG_RPC_VER_1_3;
	}
	if (IS_ERR(msm_batt_info.chg_ep)) {
		msm_batt_info.chg_ep = msm_rpc_connect_compatible(
				CHG_RPC_PROG, CHG_RPC_VER_2_2, 0);
		msm_batt_info.chg_api_version =  CHG_RPC_VER_2_2;
	}
	if (IS_ERR(msm_batt_info.chg_ep)) {
		rc = PTR_ERR(msm_batt_info.chg_ep);
		pr_err("%s: FAIL: rpc connect for CHG_RPC_PROG. rc=%d\n",
		       __func__, rc);
		msm_batt_info.chg_ep = NULL;
		return rc;
	}

	/* Get the real 1.x version */
	if (msm_batt_info.chg_api_version == CHG_RPC_VER_1_1)
		msm_batt_info.chg_api_version =
			msm_batt_get_charger_api_version();

	/* Fall back to 1.1 for default */
	if (msm_batt_info.chg_api_version < 0)
		msm_batt_info.chg_api_version = CHG_RPC_VER_1_1;
	msm_batt_info.batt_api_version =  BATTERY_RPC_VER_4_1;

	msm_batt_info.batt_client =
		msm_rpc_register_client("battery", BATTERY_RPC_PROG,
					BATTERY_RPC_VER_4_1,
					1, msm_batt_cb_func);

	if (msm_batt_info.batt_client == NULL) {
		pr_err("%s: FAIL: rpc_register_client. batt_client=NULL\n",
		       __func__);
		return -ENODEV;
	}
	if (IS_ERR(msm_batt_info.batt_client)) {
		msm_batt_info.batt_client =
			msm_rpc_register_client("battery", BATTERY_RPC_PROG,
						BATTERY_RPC_VER_1_1,
						1, msm_batt_cb_func);
		msm_batt_info.batt_api_version =  BATTERY_RPC_VER_1_1;
	}
	if (IS_ERR(msm_batt_info.batt_client)) {
		msm_batt_info.batt_client =
			msm_rpc_register_client("battery", BATTERY_RPC_PROG,
						BATTERY_RPC_VER_2_1,
						1, msm_batt_cb_func);
		msm_batt_info.batt_api_version =  BATTERY_RPC_VER_2_1;
	}
	if (IS_ERR(msm_batt_info.batt_client)) {
		msm_batt_info.batt_client =
			msm_rpc_register_client("battery", BATTERY_RPC_PROG,
						BATTERY_RPC_VER_5_1,
						1, msm_batt_cb_func);
		msm_batt_info.batt_api_version =  BATTERY_RPC_VER_5_1;
	}
	if (IS_ERR(msm_batt_info.batt_client)) {
		rc = PTR_ERR(msm_batt_info.batt_client);
		pr_err("%s: ERROR: rpc_register_client: rc = %d\n ",
		       __func__, rc);
		msm_batt_info.batt_client = NULL;
		return rc;
	}
#endif  /* CONFIG_BATTERY_MSM_FAKE */

	rc = platform_driver_register(&msm_batt_driver);

	if (rc < 0)
		pr_err("%s: FAIL: platform_driver_register. rc = %d\n",
		       __func__, rc);

	return rc;
}

static int __init msm_batt_init(void)
{
	int rc;

	pr_debug("%s: enter\n", __func__);

	rc = msm_batt_init_rpc();

	if (rc < 0) {
		pr_err("%s: FAIL: msm_batt_init_rpc.  rc=%d\n", __func__, rc);
		msm_batt_cleanup();
		return rc;
	}

	pr_info("%s: Charger/Battery = 0x%08x/0x%08x (RPC version)\n",
		__func__, msm_batt_info.chg_api_version,
		msm_batt_info.batt_api_version);

	return 0;
}

static void __exit msm_batt_exit(void)
{
	platform_driver_unregister(&msm_batt_driver);
}

module_init(msm_batt_init);
module_exit(msm_batt_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kiran Kandi, Qualcomm Innovation Center, Inc.");
MODULE_DESCRIPTION("Battery driver for Qualcomm MSM chipsets.");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:msm_battery");
