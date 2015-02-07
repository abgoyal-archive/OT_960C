/*
 *  max17043_battery.c
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2009 Samsung Electronics
 *  Minkyu Kang <mk7.kang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/max17043_battery.h>
#include <linux/slab.h>
#include "../../arch/arm/mach-msm/proc_comm.h"
#include <mach/socinfo.h>

enum {
	DEBUG_USER_STATE = 1U << 0,
	DEBUG_DETAIL = 1U << 2,
};
static int debug_mask = DEBUG_USER_STATE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define WORD_TYPE		//	Teddy 2012-06-25
//#define USE_INTERRUPT	//	Teddy 2012-06-27

#ifdef USE_INTERRUPT	//	Teddy 2012-06-27
#include <linux/interrupt.h>
#include <linux/gpio.h>
#endif	//	USE_INTERRUPT


#define MAX17043_VCELL_MSB	0x02
#define MAX17043_VCELL_LSB	0x03
#define MAX17043_SOC_MSB	0x04
#define MAX17043_SOC_LSB	0x05
#define MAX17043_MODE_MSB	0x06
#define MAX17043_MODE_LSB	0x07
#define MAX17043_VER_MSB	0x08
#define MAX17043_VER_LSB	0x09
#define MAX17043_RCOMP_MSB	0x0C
#define MAX17043_RCOMP_LSB	0x0D
#define MAX17043_OCV_MSB	0x0E
#define MAX17043_OCV_LSB	0x0F
#define MAX17043_UNLOCK_MSB	0x3E
#define MAX17043_UNLOCK_LSB	0x3F
#define MAX17043_CMD_MSB	0xFE
#define MAX17043_CMD_LSB	0xFF
#define MAX17043_MODEL_DATA		0x40

#define MAX17043_DELAY		(60*(HZ))	//	Temp 60->20
#define MAX17043_BATTERY_FULL	95

struct max17043_chip {
	struct i2c_client		*client;
	struct delayed_work		work;
	struct power_supply		battery;
	struct max17043_platform_data	*pdata;

	/* State Of Connect */
	int online;
	/* battery voltage */
	int vcell;
	/* battery capacity */
	int soc;
	/* State Of Charge */
	int status;

	int batt_type;

#ifdef USE_INTERRUPT	//	Teddy 2012-06-27
	int irq;

	struct work_struct		irq_work;
#else
	struct work_struct		irq_work;
#endif	//	USE_INTERRUPT
};

struct max17043_chip *chip_fuel = NULL;

static int write_type = 2;
static int read_type = 2;

static int get_hs_key(unsigned *supported,unsigned *key)
{
	return msm_proc_comm(PCOM_RESERVED_101, supported, key);
}

static unsigned get_batt_type(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);
	unsigned supported = 100;
	unsigned batt_id = 20;
	unsigned batt_type = 0;
	int board_type = 3;

	board_type = get_hw_id();
	get_hs_key(&supported, &batt_id);
	switch( board_type )
	{
		case HW_ID_REV_V1A:
		case HW_ID_REV_V2A:
		case HW_ID_REV_V3A:
		case HW_ID_REV_V4A:
			/*PS board*/
			if(batt_id > 100 && batt_id < 250)
				batt_type = 1; /*1600mA*/
			else if(batt_id > 1000 && batt_id < 1200)
				batt_type = 0; /*1480mA*/
			break;
		default:
			break;
	}

	chip->batt_type = batt_type;
	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: board_type(0x%X) batt_id(%d) batt_type(%d)\n", __func__, board_type, batt_id, batt_type);
	}
	return batt_type;
}

#ifdef USE_PROPERTY	//	Teddy 2012-06-26
static int max17043_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17043_chip *chip = container_of(psy,
				struct max17043_chip, battery);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = chip->vcell;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = chip->soc;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
#endif	//	USE_PROPERTY

#ifdef WORD_TYPE	//	Teddy 2012-06-26
static s32 max17043_write_reg_word(struct i2c_client *client, u8 reg, u8 value1, u8 value2)
{
	s32 ret1 = 0;

	u16 normal_value;
	u16 swap_value;

	/*normal*/
	normal_value = value1;
	normal_value = ((normal_value << 8) | value2);

	/*swap*/
	swap_value = value2;
	swap_value = ((swap_value << 8) | value1);

	switch (write_type)
	{
		case 1: /*a word*/
			ret1 = i2c_smbus_write_word_data(client, reg, normal_value);
			break;
		case 2: /*a swap word*/
			ret1 = i2c_smbus_write_word_data(client, reg, swap_value);
			break;
		case 3: /*two bytes*/
			ret1 = i2c_smbus_write_byte_data(client, reg, value1);
			ret1 += i2c_smbus_write_byte_data(client, reg+1, value2);
			break;
		case 4: /*swap two bytes*/
			ret1 = i2c_smbus_write_byte_data(client, reg, value2);
			ret1 += i2c_smbus_write_byte_data(client, reg+1, value1);
			break;
		default:
			return 0;
	}

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: type(%d)\treg(0x%X) normal(0x%04X) swap(0x%04X) ret1=(%d)\n",__func__, write_type, reg, normal_value, swap_value, ret1);
	}

	if(0 == ret1 )
		return 0;
	else
		return 1;
}

static s32 max17043_read_reg_word(struct i2c_client *client, u8 reg, u8 * value1, u8 * value2)
{
	s32 ret = 0;
	u16 value = 0;

	switch (read_type)
	{
		case 1: /*a word*/
			value = i2c_smbus_read_word_data(client, reg);
			*value2 = (value & 0x00FF);
			*value1 = ((value >> 8) & 0x00FF);
			break;
		case 2: /*a swap word*/
			value = i2c_smbus_read_word_data(client, reg);
			*value1 = (value & 0x00FF);
			*value2 = ((value >> 8) & 0x00FF);
			break;
		case 3: /*two bytes*/
			*value1 = i2c_smbus_read_byte_data(client, reg);
			*value2 = i2c_smbus_read_byte_data(client, reg+1);
			break;
		case 4: /*swap two bytes*/
			*value2 = i2c_smbus_read_byte_data(client, reg);
			*value1 = i2c_smbus_read_byte_data(client, reg+1);
			break;
		default:
			return ret;
	}

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: type(%d)\treg(0x%X) (0x%04X)->value(0x%02X%02X)\n", __func__, read_type, reg, value, *value1, *value2);
	}

	return ret;
}
#else
static int max17043_write_reg(struct i2c_client *client, int reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, value);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static int max17043_read_reg(struct i2c_client *client, int reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}
#endif	//	WORD_TYPE

struct i2c_client *fuel_client = NULL;

static void max17043_new_initial_voltage(void)
{
	int ret=0;
	u8 Volt_MSB, Volt_LSB;
	u16 VCell1, VCell2, OCV, Desired_OCV;

	if(fuel_client == NULL)
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: Fuel-Gauge STEP_: fuel_client is NULL\n", __func__);
		}
		return;
	}

	/******************************************************************************
	  Step 1.  Read First VCELL Sample
	  */
	/*ReadWord(0x02, &Volt_MSB, &Volt_LSB);*/
	max17043_read_reg_word(fuel_client, MAX17043_VCELL_MSB, &Volt_MSB, &Volt_LSB);
	VCell1 = ((Volt_MSB << 8) + Volt_LSB);
	if(debug_mask & DEBUG_USER_STATE)
	{
		printk(KERN_DEBUG "Fuel-Gauge STEP_: FUEL-GAUGE New Initial Voltage VCELL1 ( 0x%X.0x%X ) [0x%X] \n", Volt_MSB, Volt_LSB, VCell1 );
	}
	/******************************************************************************
	  Step 2.  Delay 125ms
	  Delay at least 125ms to ensure a new reading in the VCELL register.
	  */
	mdelay(125);
	/******************************************************************************
	  Step 3.  Read First VCELL Sample
	  */
	/*ReadWord(0x02, &Volt_MSB, &Volt_LSB);*/
	max17043_read_reg_word(fuel_client, MAX17043_VCELL_MSB, &Volt_MSB, &Volt_LSB);
	VCell2 = ((Volt_MSB << 8) + Volt_LSB);
	if(debug_mask & DEBUG_USER_STATE)
	{
		printk(KERN_DEBUG "Fuel-Gauge STEP_: FUEL-GAUGE New Initial Voltage VCELL2 ( 0x%X.0x%X ) [0x%X] \n", Volt_MSB, Volt_LSB, VCell2 );
	}
	/******************************************************************************
	  Step 4.  Unlock Model Access
	  To unlock access to the model the host software must write 0x4Ah to memory 
	  location 0x3E and write 0x57 to memory location 0x3F.  
	  Model Access must be unlocked to read and write the OCV register.
	  */
	/*WriteWord(0x3E, 0x4A, 0x57);*/
	ret = max17043_write_reg_word(fuel_client, MAX17043_UNLOCK_MSB, 0x4A, 0x57);
	/******************************************************************************
	  Step 5.  Read OCV
	  */
	/*ReadWord(0x0E, &Volt_MSB, &Volt_LSB);*/
	ret = max17043_read_reg_word(fuel_client, MAX17043_OCV_MSB, &Volt_MSB, &Volt_LSB);
	OCV = ((Volt_MSB << 8) + Volt_LSB);
	if(debug_mask & DEBUG_USER_STATE)
	{
		printk(KERN_DEBUG "Fuel-Gauge STEP_: FUEL-GAUGE New Initial Voltage OCV ( 0x%X.0x%X ) [0x%X] \n", Volt_MSB, Volt_LSB, OCV );
	}
	/******************************************************************************
	  Step 6.  Determine maximum value of VCell1, VCell2, and OCV
	  */
	if((VCell1 >= VCell2) && (VCell1 >= OCV))
	{
		Desired_OCV = VCell1;
	}
	else if((VCell2 >= VCell1) && (VCell2 >= OCV))
	{
		Desired_OCV = VCell2;
	}
	else
	{
		Desired_OCV = OCV;
	}

	/******************************************************************************
	  Step 7.  Write OCV
	  */
	/*WriteWord(0x0E, Desired_OCV >> 8, Desired_OCV & 0xFF);*/
	ret = max17043_write_reg_word(fuel_client, MAX17043_OCV_MSB, (Desired_OCV >> 8), (Desired_OCV & 0xFF));
	/******************************************************************************
	  Step 8. Lock Model Access
	  */
	/*WriteWord(0x3E, 0x00, 0x00);*/
	ret = max17043_write_reg_word(fuel_client, MAX17043_UNLOCK_MSB, 0x00, 0x00);
	/******************************************************************************
	  Step 9.  Delay 125ms
	  This delay must be at least 150mS before reading the SOC Register to allow 
	  the correct value to be calculated by the device.
	  */
	mdelay(125);
}

static void max17043_reset(struct i2c_client *client)
{
#ifdef WORD_TYPE	//	Teddy 2012-06-25
	int ret = 0;
	if(client == NULL)
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: Fuel-Gauge STEP_: client is NULL\n", __func__);
		}
		return;
	}
	ret = max17043_write_reg_word(client, MAX17043_CMD_MSB, 0x54, 0x00);
	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: (%d)\n", __func__, ret);
	}
#else
	max17043_write_reg(client, MAX17043_CMD_MSB, 0x54);
	max17043_write_reg(client, MAX17043_CMD_LSB, 0x00);
#endif	//	WORD_TYPE
}

static void max17043_get_vcell(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);
	u8 msb;
	u8 lsb;

#ifdef WORD_TYPE	//	Teddy 2012-06-25
	max17043_read_reg_word(client, MAX17043_VCELL_MSB, &msb, &lsb);

	chip->vcell = ( msb << 4) + (lsb >> 4);
	chip->vcell = chip->vcell*125/100;
	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: vcell(%d) msb(%d) lsb(%d)\n", __func__, chip->vcell, msb, lsb);
	}
#else
	msb = max17043_read_reg(client, MAX17043_VCELL_MSB);
	lsb = max17043_read_reg(client, MAX17043_VCELL_LSB);

	chip->vcell = (msb << 4) + (lsb >> 4);
#endif	//	WORD_TYPE
}

static void max17043_get_soc(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);
	u8 msb;
	u8 lsb;

#ifdef WORD_TYPE	//	Teddy 2012-06-25
	max17043_read_reg_word(client, MAX17043_SOC_MSB, &msb, &lsb);

	chip->soc = ((msb+1) >> 1);
	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: soc(%d) msb(%d) lsb(%d)\n", __func__, chip->soc, msb, lsb);
	}
#else
	msb = max17043_read_reg(client, MAX17043_SOC_MSB);
	lsb = max17043_read_reg(client, MAX17043_SOC_LSB);

	chip->soc = msb;
#endif	//	WORD_TYPE
}

static void max17043_get_version(struct i2c_client *client)
{
	u8 msb;
	u8 lsb;

#ifdef WORD_TYPE	//	Teddy 2012-06-25
	max17043_read_reg_word(client, MAX17043_VER_MSB, &msb, &lsb);
	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: version (%d,%d)\n", __func__, msb, lsb);
	}
#else
	msb = max17043_read_reg(client, MAX17043_VER_MSB);
	lsb = max17043_read_reg(client, MAX17043_VER_LSB);
#endif	//	WORD_TYPE

	dev_info(&client->dev, "MAX17043 Fuel-Gauge Ver %d%d\n", msb, lsb);
}

static void max17043_get_online(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);

	if (chip->pdata->battery_online)
		chip->online = chip->pdata->battery_online();
	else
		chip->online = 1;
}

static void max17043_get_status(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);

	if (!chip->pdata->charger_online || !chip->pdata->charger_enable) {
		chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
		return;
	}

	if (chip->pdata->charger_online()) {
		if (chip->pdata->charger_enable())
			chip->status = POWER_SUPPLY_STATUS_CHARGING;
		else
			chip->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (chip->soc > MAX17043_BATTERY_FULL)
		chip->status = POWER_SUPPLY_STATUS_FULL;
}

static void max17043_work(struct work_struct *work)
{
	struct max17043_chip *chip;

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s:\n", __func__);
	}
	chip = container_of(work, struct max17043_chip, work.work);

	max17043_get_vcell(chip->client);
	max17043_get_soc(chip->client);
	if( 0 )
	{
	max17043_get_online(chip->client);
	max17043_get_status(chip->client);
	}

	schedule_delayed_work(&chip->work, MAX17043_DELAY);
}

#ifdef USE_INTERRUPT	//	Teddy 2012-06-27
static void max17043_irq_work(struct work_struct *work)
{
	struct max17043_chip *chip;

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s:\n", __func__);
	}
	chip = container_of(work, struct max17043_chip, irq_work);

	max17043_get_vcell(chip->client);
	max17043_get_soc(chip->client);

	enable_irq(chip->irq);
}

static irqreturn_t max17043_irq(int irq, void *data)
{
	struct max17043_chip *chip = (struct max17043_chip *)data;

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s:\n", __func__);
	}
	if( chip == NULL )
		return -EINVAL;

	disable_irq_nosync(chip->irq);
#if 1
	enable_irq(chip->irq);
#else
	schedule_work(&chip->irq_work);
#endif

	return IRQ_HANDLED;
}
#else
static void max17043_irq_work(struct work_struct *work)
{
	struct max17043_chip *chip;

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s:\n", __func__);
	}
	chip = container_of(work, struct max17043_chip, irq_work);

	max17043_get_vcell(chip->client);
	max17043_get_soc(chip->client);
}
#endif	//	USE_INTERRUPT

#ifdef USE_PROPERTY	//	Teddy 2012-06-26
static enum power_supply_property max17043_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};
#endif	//	USE_PROPERTY

static u8 model_data[][64] =
{
	{ 0xAA,0x00,0xB4,0xC0,0xB7,0xF0,0xBA,0x90,0xBB,0xB0,0xBC,0x70,0xBC,0xF0,0xBD,0xA0,0xBE,0x50,0xBF,0xB0,0xC1,0xB0,0xC3,0xB0,0xC5,0xA0,0xC7,0xC0,0xCC,0x30,0xD1,0x30,0x01,0x70,0x15,0x10,0x14,0x20,0x23,0x10,0x6C,0x60,0x6A,0x50,0x32,0xF0,0x39,0xE0,0x2A,0xD0,0x18,0xF0,0x18,0xB0,0x19,0xF0,0x17,0xE0,0x11,0xF0,0x0F,0x20,0x0F,0x20 }, // (830b_1_040612.INI.TXT)
	{ 0xAA,0x00,0xB2,0x50,0xB6,0xB0,0xBB,0x20,0xBB,0xA0,0xBC,0x20,0xBC,0x90,0xBC,0xD0,0xBD,0x10,0xBE,0x00,0xBE,0xF0,0xC1,0x40,0xC5,0x40,0xC9,0x40,0xCD,0x50,0xD1,0x60,0x07,0x70,0x09,0x10,0x08,0xF0,0x64,0x00,0x64,0x00,0x88,0x00,0x77,0x00,0x77,0x00,0x29,0x30,0x29,0x30,0x25,0x40,0x14,0xD0,0x14,0x10,0x0E,0x00,0x0E,0x00,0x0E,0x00 }, // (830_1_022212.INI.txt)
};

static u8 RCOMP0[]		= {0x4B,0x43};
static u8 OCVTestA[]	= {0xDB,0xDB};
static u8 OCVTestB[]	= {0x30,0x60};
static u8 SOCCheckA[]	= {0xEC,0xEA};
static u8 SOCCheckB[]	= {0xEE,0xEC};

static int is_fuel_gauge_exist = 1;

static int fuel_gauge_init(struct i2c_client *client)
{
	unsigned int	batt_type = get_batt_type(client);
	int index = 0;
	int ret = 0;
	int count = 3;
	u8 OCV_1, OCV_2, SOC_1, SOC_2, test_1, test_2;
	int result = 0;

	if(client == NULL)
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: Fuel-Gauge STEP_: client is NULL\n", __func__);
		}
		return -1;
	}

	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: STEP1: Unlock Model Access\n", __func__);
	}
	ret = max17043_write_reg_word(client, MAX17043_UNLOCK_MSB, 0x4A, 0x57);

	if( ret )
	{
		is_fuel_gauge_exist = 0;
		return -2;
	}

	do {

// Step 2. Read OCV
		mdelay(200);
		ret = max17043_read_reg_word(client, MAX17043_OCV_MSB, &OCV_1, &OCV_2);
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: STEP2: OCV_1 = %d(0x%X), OCV_2 = %d(0x%X)\n", __func__, OCV_1, OCV_1, OCV_2, OCV_2);
		}

// Step 2.5 Verify Model Access Unlocked
		if(0xFF == OCV_1 && 0xFF == OCV_2)
		{
			/*goto Step 1*/
			if(debug_mask & DEBUG_USER_STATE)
			{
				pr_info( "%s: Unlock Model Fail", __func__);
			}

			ret = max17043_write_reg_word(client, MAX17043_UNLOCK_MSB, 0x4A, 0x57);

			if(--count < 0)
			{
				result++;
				break;
			}
		}
		else
		{
			if(debug_mask & DEBUG_USER_STATE)
			{
				pr_info( "%s: Unlock Model OK", __func__);
			}
			ret = max17043_read_reg_word(client, MAX17043_UNLOCK_MSB, &test_1, &test_2);
			break;
		}

	} while (0xFF == OCV_1 && 0xFF == OCV_2);

// Step 3. Write OCV (MAX17040/1/3/4 only)// 56176 -> DB70
	ret = max17043_write_reg_word(client, MAX17043_OCV_MSB, OCVTestA[batt_type], OCVTestB[batt_type]);

// Step 4. Write RCOMP to its Maximum Value (MAX17040/1/3/4 only)
	ret = max17043_write_reg_word(client, MAX17043_RCOMP_MSB, 0xFF, 0x00);
	ret = max17043_read_reg_word(client, MAX17043_RCOMP_MSB, &test_1, &test_2);

// Step 5. Write the Model
	ret = 0;
	for (index = 0; index < 32; index++)
	{
		ret += max17043_write_reg_word(client, MAX17043_MODEL_DATA+(index*2), model_data[batt_type][index*2], model_data[batt_type][index*2+1]);
	}
	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: STEP5: error ret = (%d)\n", __func__, ret);
	}

// Step 6. Delay at least 150ms (MAX17040/1/3/4 only)
	mdelay(200);

// Step 7. Write OCV
	ret = max17043_write_reg_word(client, MAX17043_OCV_MSB, OCVTestA[batt_type], OCVTestB[batt_type]);

// Step 8. Delay between 150ms and 600ms
	mdelay(200);

// Step 9. Read SOC Register and compare to expected result
	ret = max17043_read_reg_word(client, MAX17043_SOC_MSB, &SOC_1, &SOC_2);
	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: STEP9: Read SOC = ( 0x%X > 0x%X ) ( 0x%X < 0x%X )\n", __func__, SOC_1, SOCCheckA[batt_type], SOC_2, SOCCheckB[batt_type]);
	}

	if( SOC_1 >= SOCCheckA[batt_type] && SOC_1 <= SOCCheckB[batt_type])
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "------ model was loaded successfully	----------------%d",  ret);
		}
	}
	else
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "------ mdodel was NOT loaded successfully  ----------------%d", ret);
		}
		result = result + 10;
	}

// Step 10. Restore CONFIG and OCV
	ret = max17043_write_reg_word(client, MAX17043_RCOMP_MSB, RCOMP0[batt_type], 0x1C);
	ret = max17043_read_reg_word(client, MAX17043_RCOMP_MSB, &test_1, &test_2);
	ret = max17043_write_reg_word(client, MAX17043_OCV_MSB, OCV_1, OCV_2);

// Step 11. Lock Model Access
	ret = max17043_read_reg_word(client, MAX17043_UNLOCK_MSB, &test_1, &test_2);
	ret = max17043_write_reg_word(client, MAX17043_UNLOCK_MSB, 0x00, 0x00);
	ret = max17043_read_reg_word(client, MAX17043_UNLOCK_MSB, &test_1, &test_2);

// Step 12. Delay at least 150mS
	mdelay(200);

	return result;
}

int read_fuel_gauge_voltage(void)
{
	int voltage = 4200;

	if( chip_fuel )
		voltage = chip_fuel->vcell;

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: %d:%d\n", __func__, voltage, chip_fuel->vcell);
	}
	return voltage;
}

int read_fuel_gauge_percentage(void)
{
	int percentage = 100;

	if( chip_fuel )
		percentage = chip_fuel->soc;

	if(debug_mask & DEBUG_DETAIL)
	{
		pr_info( "%s: %d:%d\n", __func__, percentage, chip_fuel->soc);
	}
	return percentage;
}

static unsigned chg_fuel_gauge_is_initialized(void)
{
	unsigned chg_loc = 0;
	unsigned supported = 0;
	unsigned result = 700;

	chg_loc = 70;

	msm_proc_comm(PCOM_CHG_IS_CHARGING, &chg_loc, &supported);
	{
		if(1 == supported)
		{
			result = chg_loc;
		}
	}

	pr_info("%s: (%d)(%d)(%d)\n", __func__, chg_loc, supported, result);
	return result;
}

int new_initialized = 0;
static int reinit_advantage_point = 0;

int read_fuel_gauge_voltage_and_percentage(unsigned int *voltage, unsigned int *percentage)
{
	static int new_initial_voltage_count = 0;
	int reinit = 0;
	static int first_check = 0;

	if( chip_fuel )
	{
#ifdef USE_INTERRUPT	//	Teddy 2012-07-10
		disable_irq_nosync(chip_fuel->irq);
#endif	//	USE_INTERRUPT
		schedule_work(&chip_fuel->irq_work);
	}

	*voltage = read_fuel_gauge_voltage();
	*percentage = read_fuel_gauge_percentage();

	if(*percentage >= 100)
	{
		reinit_advantage_point = 0;
	}

	if((*voltage > 4100) && (*percentage > 90))
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: 0-a new_initial_voltage_count(%d) voltage(%d), percentage(%d), reinit_advantage_point(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, reinit_advantage_point);
		}

		*percentage = *percentage + reinit_advantage_point;

		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: 0-b new_initial_voltage_count(%d) voltage(%d), percentage(%d), reinit_advantage_point(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, reinit_advantage_point);
		}
	}
	else
	{
		reinit_advantage_point = 0;
	}

	if( *voltage < 3200 )
	{
		if(debug_mask & DEBUG_USER_STATE)
		{
			printk(KERN_DEBUG "Fuel-Gauge STEP_: Oops! Oops! voltage(%d)\n", *voltage);
		}
		*percentage = 0;
	}

	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: 1 new_initial_voltage_count(%d) voltage(%d), percentage(%d), reinit_advantage_point(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, reinit_advantage_point);
	}

	if(*percentage <= 1)
	{
		if(*voltage > 3700)
		{
			reinit = 1;
		}
	}

	if(0 == first_check)
	{
		unsigned result = 0;

		first_check = 1;
		result = chg_fuel_gauge_is_initialized();
		if(1 == result)
		{
		}
		else
		{
			reinit = 1;
		}

		if(debug_mask & DEBUG_USER_STATE)
		{
			pr_info( "%s: 1-a new_initial_voltage_count(%d) voltage(%d), percentage(%d), first_check(%d), result(%d), reinit(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, first_check, result, reinit);
		}
	}

	{
		if(1 == reinit)
		{
			new_initial_voltage_count++;
			if(new_initial_voltage_count < 20)
			{
				int result = 0;

				if(debug_mask & DEBUG_USER_STATE)
				{
					pr_info( "%s: 2 new_initial_voltage_count(%d) voltage(%d), percentage(%d), result(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, result);
				}
				max17043_reset(fuel_client);
				result = fuel_gauge_init(fuel_client);
				if(0 == result)
				{
				}
				else
				{
					// one more time
					max17043_reset(fuel_client);
					fuel_gauge_init(fuel_client);
				}

				max17043_new_initial_voltage();

				if( chip_fuel )
				{
#ifdef USE_INTERRUPT	//	Teddy 2012-07-10
					disable_irq_nosync(chip_fuel->irq);
#endif	//	USE_INTERRUPT
					schedule_work(&chip_fuel->irq_work);
				}
				max17043_get_vcell(fuel_client);
				max17043_get_soc(fuel_client);
				*voltage = read_fuel_gauge_voltage();
				*percentage = read_fuel_gauge_percentage();
				new_initialized = 1;
				if(*percentage == 0)
				{
					*percentage = 1;
				}
#if 1
				else
				{
					pr_info( "%s: 2-a new_initial_voltage_count(%d) voltage(%d), percentage(%d), result(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, result);
					if((*voltage > 4100) && (*percentage > 90))
					{
						*percentage = *percentage + 7;
						if(*percentage > 100)
						{
							*percentage = 100;
						}
						reinit_advantage_point = 7;

						if(debug_mask & DEBUG_USER_STATE)
						{
							pr_info( "%s: 2-b new_initial_voltage_count(%d) voltage(%d), percentage(%d), result(%d), reinit_advantage_point(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, result, reinit_advantage_point);
						}
					}
				}
#endif // 0
				if(debug_mask & DEBUG_USER_STATE)
				{
					pr_info( "%s: 2 new_initial_voltage_count(%d) voltage(%d), percentage(%d), result(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage, result);
				}

		}
	}
	else
	{
		new_initial_voltage_count = 0;
	}
	}

	if(debug_mask & DEBUG_USER_STATE)
	{
		pr_info( "%s: 3 new_initial_voltage_count(%d) voltage(%d), percentage(%d)\n", __func__, new_initial_voltage_count, *voltage, *percentage);
		/*pr_info( "%s: voltage(%d), percentage(%d)\n", __func__, *voltage, *percentage);*/
	}
	return 1;
}

static int __devinit max17043_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct max17043_chip *chip;
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, chip);

#ifdef USE_PROPERTY	//	Teddy 2012-06-26
	chip->battery.name		= "battery";
	chip->battery.type		= POWER_SUPPLY_TYPE_BATTERY;
	chip->battery.get_property	= max17043_get_property;
	chip->battery.properties	= max17043_battery_props;
	chip->battery.num_properties	= ARRAY_SIZE(max17043_battery_props);

	ret = power_supply_register(&client->dev, &chip->battery);
	if (ret) {
		dev_err(&client->dev, "failed: power supply register\n");
		goto register_error;
	}
#endif	//	USE_PROPERTY

	chip_fuel = chip;
	fuel_client = client;

	if(0)
		max17043_reset(client);
	max17043_get_version(client);

	if(0)
		fuel_gauge_init(client);
	chip->vcell = 4200;
	chip->soc = 100;
	max17043_get_vcell(client);
	max17043_get_soc(client);

#ifdef USE_INTERRUPT	//	Teddy 2012-06-27
	if( gpio_request(chip->pdata->irq_gpio, "fuel_irq"))
	{
		pr_err("request_gpio failed!\n");
	}

	gpio_tlmm_config(GPIO_CFG(chip->pdata->irq_gpio, 0, GPIO_CFG_INPUT,
					GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);

	chip->irq = MSM_GPIO_TO_INT(chip->pdata->irq_gpio);

	if( (ret = request_irq(chip->irq, max17043_irq, IRQF_TRIGGER_FALLING, "max17043_irq", chip)) )
	{
		pr_err("requset irq %d failed! The return value is %d\n", chip->irq, ret);
		goto request_irq_failed;
	}

	INIT_WORK(&chip->irq_work, max17043_irq_work);
#else
	INIT_WORK(&chip->irq_work, max17043_irq_work);
#endif	//	USE_INTERRUPT
	INIT_DELAYED_WORK(&chip->work, max17043_work);
	schedule_delayed_work(&chip->work, MAX17043_DELAY);

	return 0;

#ifdef USE_INTERRUPT	//	Teddy 2012-06-27
request_irq_failed:
	gpio_free(chip->pdata->irq_gpio);
#endif	//	USE_INTERRUPT
#ifdef USE_PROPERTY	//	Teddy 2012-06-27
register_error:
#endif	//	USE_PROPERTY
	kfree(chip);
	return ret;
}

static int __devexit max17043_remove(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);

#if 0
	power_supply_unregister(&chip->battery);
#endif
	cancel_delayed_work(&chip->work);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM

static int max17043_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);

	cancel_delayed_work(&chip->work);
	return 0;
}

static int max17043_resume(struct i2c_client *client)
{
	struct max17043_chip *chip = i2c_get_clientdata(client);

	schedule_delayed_work(&chip->work, MAX17043_DELAY);
	return 0;
}

#else

#define max17043_suspend NULL
#define max17043_resume NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id max17043_id[] = {
	{ "max17043", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max17043_id);

static struct i2c_driver max17043_i2c_driver = {
	.driver	= {
		.name	= "max17043",
	},
	.probe		= max17043_probe,
	.remove		= __devexit_p(max17043_remove),
	.suspend	= max17043_suspend,
	.resume		= max17043_resume,
	.id_table	= max17043_id,
};

static int __init max17043_init(void)
{
	return i2c_add_driver(&max17043_i2c_driver);
}
module_init(max17043_init);

static void __exit max17043_exit(void)
{
	i2c_del_driver(&max17043_i2c_driver);
}
module_exit(max17043_exit);

MODULE_AUTHOR("Minkyu Kang <mk7.kang@samsung.com>");
MODULE_DESCRIPTION("MAX17043 Fuel Gauge");
MODULE_LICENSE("GPL");
