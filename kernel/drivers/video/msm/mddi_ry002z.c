/* Copyright (c) 2011, M7System. All rights reserved.
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

#include "msm_fb.h"
#include "mddihost.h"
#include "mddihosti.h"

#include <mach/gpio.h>

#include <linux/pwm.h>
#ifdef CONFIG_PMIC8058_PWM
#include <linux/mfd/pmic8058.h>
#include <linux/pmic8058-pwm.h>
#endif

//#define DEBUG

#ifdef DEBUG
#define VULCAN_DEBUG(fmt, ...) \
        printk(KERN_ERR "[VULCAN](%s:%d) " pr_fmt(fmt), __func__, __LINE__, ##__VA_ARGS__)
#else
#define VULCAN_DEBUG(...) do { } while(0)
#endif

#define RY002Z_WVGA_ID 1

// To turn the LCD on during boot-up sequence without CONFIG_FRAMEBUFFER_CONSOLE by sinclair.lee 20120324
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE)
#define ENABLE_FB_OPEN_ON_BOOT
#endif

/* Backlight */
#define LCD_BL_PWM 0 /* pm_gpio_24, channel 0 */
#define PWM_FREQ_HZ 20000
#define PWM_LEVEL 255
#define PWM_PERIOD_USEC (USEC_PER_SEC / PWM_FREQ_HZ)
static struct pwm_device *bl_pwm;
int lcd_status=1;
extern int is_prox_on(void);

/* Panel data */
static struct msm_panel_common_pdata *mddi_ry002z_pdata;

/* Command set */
struct mddi_ry002z_cmd {
	char reg;
	char data;
	int delay;
};

/* Power on commands */
static struct mddi_ry002z_cmd mddi_ry002z_power_on_cmds[] = {
	// SET password
//	{0xB0, 0x01, 1}, 
//	{0xFD, 0x0f, 1},

	// SET password
	{0xB9, 0xFF, 0},
	{0xFD, 0x83, 0},
	{0xFD, 0x69, 0},

	//Set Power
	{0xB1, 0x01, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x34, 0},
	{0xFD, 0x06, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x0F, 0},
	{0xFD, 0x0F, 0},
	{0xFD, 0x2A, 0},
	{0xFD, 0x32, 0},
	{0xFD, 0x3F, 0},
	{0xFD, 0x3F, 0},
	{0xFD, 0x07, 0},
	{0xFD, 0x23, 0},
	{0xFD, 0x01, 0},
	{0xFD, 0xE6, 0},
	{0xFD, 0xE6, 0},
	{0xFD, 0xE6, 0},
	{0xFD, 0xE6, 0},
	{0xFD, 0xE6, 0},

	// SET Display  480x800
	{0xB2, 0x00, 0},
	{0xFD, 0x20, 0},
	{0xFD, 0x0A, 0},//bp
	{0xFD, 0x0A, 0},//fp
	{0xFD, 0x70, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0xFF, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x03, 0},//bp_pe
	{0xFD, 0x03, 0},//fp_pe
	{0xFD, 0x00, 0},
	{0xFD, 0x01, 0},

	// SET Display
	{0xB4, 0x00, 0},
	{0xFD, 0x0C, 0},
	{0xFD, 0xA0, 0},
	{0xFD, 0x0E, 0},
	{0xFD, 0x06, 0},

	{0xB6, 0x2C, 0},
	{0xFD, 0x2C, 0},

	{0x3A, 0x77, 0},

	//SET GAMMA
	{0xD5, 0x00, 0},
	{0xFD, 0x05, 0},
	{0xFD, 0x03, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x01, 0},
	{0xFD, 0x09, 0},
	{0xFD, 0x10, 0},
	{0xFD, 0x80, 0},
	{0xFD, 0x37, 0},
	{0xFD, 0x37, 0},
	{0xFD, 0x20, 0},
	{0xFD, 0x31, 0},
	{0xFD, 0x46, 0},
	{0xFD, 0x8A, 0},
	{0xFD, 0x57, 0},
	{0xFD, 0x9B, 0},
	{0xFD, 0x20, 0},
	{0xFD, 0x31, 0},
	{0xFD, 0x46, 0},
	{0xFD, 0x8A, 0},
	{0xFD, 0x57, 0},
	{0xFD, 0x9B, 0},
	{0xFD, 0x07, 0},
	{0xFD, 0x0F, 0},
	{0xFD, 0x02, 0},
	{0xFD, 0x00, 0},

	{0xE0, 0x00, 0},
	{0xFD, 0x08, 0},
	{0xFD, 0x0D, 0},
	{0xFD, 0x2D, 0},
	{0xFD, 0x34, 0},
	{0xFD, 0x3F, 0},
	{0xFD, 0x19, 0},
	{0xFD, 0x38, 0},
	{0xFD, 0x09, 0},
	{0xFD, 0x0E, 0},
	{0xFD, 0x0E, 0},
	{0xFD, 0x12, 0},
	{0xFD, 0x14, 0},
	{0xFD, 0x12, 0},
	{0xFD, 0x14, 0},
	{0xFD, 0x13, 0},
	{0xFD, 0x19, 0},
	{0xFD, 0x00, 0},
	{0xFD, 0x08, 0},
	{0xFD, 0x0D, 0},
	{0xFD, 0x2D, 0},
	{0xFD, 0x34, 0},
	{0xFD, 0x3F, 0},
	{0xFD, 0x19, 0},
	{0xFD, 0x38, 0},
	{0xFD, 0x09, 0},
	{0xFD, 0x0E, 0},
	{0xFD, 0x0E, 0},
	{0xFD, 0x12, 0},
	{0xFD, 0x14, 0},
	{0xFD, 0x12, 0},
	{0xFD, 0x14, 0},
	{0xFD, 0x13, 0},
	{0xFD, 0x19, 0},

	{0x35, 0x00, 0},

	{0x11, 0x00, 120},
	{0xCC, 0x02, 0},
	{0x29, 0x00, 0},
	{0x2C, 0x00, 5},
};

/* Power off commands */
static struct mddi_ry002z_cmd mddi_ry002z_power_off_cmds[] = {
	{0x28, 0x00, 0},
	{0x10, 0x00, 120},
};
#if 1
static struct mddi_ry002z_cmd mddi_ry002z_power_on_cmds_after_on[] = {
	{0x11, 0x00, 120},
	{0xCC, 0x02, 0},
	{0x29, 0x00, 5},
};
static struct mddi_ry002z_cmd mddi_ry002z_prox_on_cmds[] = {
	{0x11, 0x00, 100},
	{0xCC, 0x02, 0},
	{0x29, 0x00, 5},
};
#endif
#if 0
/* Deep standby off commands */
static struct mddi_ry002z_cmd mddi_ry002z_deep_standby_off_cmds[] = {
	{0xB1, 0x00, 0},
	{0xFD, 0x00, 210},
};

/* Deep standby on commands */
static struct mddi_ry002z_cmd mddi_ry002z_deep_standby_on_cmds[] = {
	{0x28, 0x00, 0},
	{0x10, 0x00, 120},
	{0xB1, 0x00, 0},
	{0xFD, 0x01, 10},
};
#endif

static void pmic_set_lcd_intensity(int level)
{
	int ret;

	VULCAN_DEBUG("level = %d, bl_pwm = %p\n", level, bl_pwm);

	if (bl_pwm) {
		ret = pwm_config(bl_pwm, 
			(PWM_PERIOD_USEC * level) / PWM_LEVEL, PWM_PERIOD_USEC);
		if (ret) {
			pr_err("%s: pwm_config failed.\n", __func__);
			return;
		}

#if 0	// by sinclair.lee 20120223 check whether to use pwm_disable or not later
		ret = pwm_enable(bl_pwm);
#else
		if(level == 0)
		{
			pwm_disable(bl_pwm);
			ret = 0;
		}
		else
			ret = pwm_enable(bl_pwm);

#endif
		if (ret) {
			pr_err("%s: pwm_enable failed.\n", __func__);
			return;
		}
	}

}

static void mddi_ry002z_set_backlight(struct msm_fb_data_type *mfd)
{
	VULCAN_DEBUG("mfd->bl_level = %d\n", mfd->bl_level);

	pmic_set_lcd_intensity(mfd->bl_level);
}

boolean sleep_mode = 0;
extern int ft_tsp_power( int on );
extern void ft5306_reset_func(void);
extern void ft5306_reset_func_gpio(void);
extern void mddi_host_disable_hibernation(boolean disable);
extern int display_reset_power(boolean on);

static int mddi_ry002z_lcd_on(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	static int power_on_init=0, first_entered=0;
	
	VULCAN_DEBUG("ARRAY_SIZE = %d\n", ARRAY_SIZE(mddi_ry002z_power_on_cmds));
	lcd_status = 1;
	if(!power_on_init)
	{
		//power_on_init = 1;
#if 1//TSP Power
		ft_tsp_power(1);
		ft5306_reset_func_gpio();
#endif
		mddi_host_disable_hibernation(FALSE);//LCD RGB Invert issue
		display_reset_power(1);
		mddi_host_disable_hibernation(TRUE);

		for (i = 0; i < ARRAY_SIZE(mddi_ry002z_power_on_cmds); i++) {
			ret = mddi_queue_register_write(mddi_ry002z_power_on_cmds[i].reg,
						mddi_ry002z_power_on_cmds[i].data, TRUE, 0);
			if (ret) printk(KERN_ERR "%s: mddi register write failed.\n", __func__);

			VULCAN_DEBUG("ARRAY_SIZE1 = %d\n", ARRAY_SIZE(mddi_ry002z_power_on_cmds));

			if(!first_entered)
			{
				if((mddi_ry002z_power_on_cmds[i].reg == 0xCC)||
					(mddi_ry002z_power_on_cmds[i].reg == 0x29)||
					(mddi_ry002z_power_on_cmds[i].reg == 0x2C)
				)
					continue;
			}
#if 0//LCD RGB Invert issue
			if(mddi_ry002z_power_on_cmds[i].reg == 0x11)
			{
				ft_tsp_power(1);
				mddi_wait(5);
				ft5306_reset_func();
			}
			else 
#endif
			if (mddi_ry002z_power_on_cmds[i].delay) {
				mddi_wait(mddi_ry002z_power_on_cmds[i].delay);
			}
		}

		if(!first_entered)
		{
			first_entered = 1;
			mddi_host_disable_hibernation(FALSE);//LCD RGB Invert issue
			mddi_queue_register_write(0x01, 0x00, TRUE, 0);
			mddi_wait(120);
			mddi_host_disable_hibernation(TRUE);
			
			for (i = 0; i < ARRAY_SIZE(mddi_ry002z_power_on_cmds); i++) {
				ret = mddi_queue_register_write(mddi_ry002z_power_on_cmds[i].reg,
							mddi_ry002z_power_on_cmds[i].data, TRUE, 0);
			
				if (mddi_ry002z_power_on_cmds[i].delay) {
						mddi_wait(mddi_ry002z_power_on_cmds[i].delay);
					}
				}
		}
	}
	else if(is_prox_on())
	{
		for (i = 0; i < ARRAY_SIZE(mddi_ry002z_prox_on_cmds); i++) {
			ret = mddi_queue_register_write(mddi_ry002z_prox_on_cmds[i].reg,
						mddi_ry002z_prox_on_cmds[i].data, TRUE, 0);
			if (ret) printk(KERN_ERR "%s: mddi register write failed.\n", __func__);

			VULCAN_DEBUG("ARRAY_SIZE2 = %d\n", ARRAY_SIZE(mddi_ry002z_prox_on_cmds));

			if(i==0)
			{
				ft_tsp_power(1);
				mddi_wait(5);
				ft5306_reset_func();
			}
			else if (mddi_ry002z_prox_on_cmds[i].delay) {
				mddi_wait(mddi_ry002z_prox_on_cmds[i].delay);
			}
		}
	}
	else
	{
		for (i = 0; i < ARRAY_SIZE(mddi_ry002z_power_on_cmds_after_on); i++) {
			ret = mddi_queue_register_write(mddi_ry002z_power_on_cmds_after_on[i].reg,
						mddi_ry002z_power_on_cmds_after_on[i].data, TRUE, 0);
			if (ret) printk(KERN_ERR "%s: mddi register write failed.\n", __func__);

			VULCAN_DEBUG("ARRAY_SIZE3 = %d\n", ARRAY_SIZE(mddi_ry002z_power_on_cmds_after_on));

			if(i==0)
			{
				ft_tsp_power(1);
				mddi_wait(5);
				ft5306_reset_func();
			}
			else if (mddi_ry002z_power_on_cmds_after_on[i].delay) {
				mddi_wait(mddi_ry002z_power_on_cmds_after_on[i].delay);
			}
		}
	}
	lcd_status = 2;
	return 0;
}

static int mddi_ry002z_lcd_off(struct platform_device *pdev)
{
	int i;
	int ret = 0;

	VULCAN_DEBUG("ARRAY_SIZE = %d\n", ARRAY_SIZE(mddi_ry002z_power_off_cmds));
	lcd_status = 0;

	for (i = 0; i < ARRAY_SIZE(mddi_ry002z_power_off_cmds); i++) {
		ret = mddi_queue_register_write(mddi_ry002z_power_off_cmds[i].reg,
					mddi_ry002z_power_off_cmds[i].data, TRUE, 0);
		if (ret) {
			printk(KERN_ERR "%s: mddi register write failed.\n", __func__);
		}

		if (mddi_ry002z_power_off_cmds[i].delay) {
			mddi_wait(mddi_ry002z_power_off_cmds[i].delay);
		}
	}
	display_reset_power(0);

	return 0;
}

#ifdef ENABLE_FB_OPEN_ON_BOOT
static int mddi_ry002z_fb_notified = 0;
static struct workqueue_struct *ry_wq = (struct workqueue_struct *)NULL;
struct work_struct ry_work;
struct fb_info *ry_info;

static int mddi_ry002z_fb_event_notify(struct notifier_block *self, unsigned long action, void *data);


static struct notifier_block mddi_ry002z_fb_event_notifier = {
	.notifier_call	= mddi_ry002z_fb_event_notify,
};

static void ry002z_work_func(struct work_struct *work)
{
	if(mddi_ry002z_fb_notified == 1)
	{
		mddi_ry002z_fb_notified = 2;
		ry_info->fbops->fb_open(ry_info, 0);
		pmic_set_lcd_intensity(102);	// force to turn on backlight
	}
	else if(mddi_ry002z_fb_notified == 3)
	{
		mddi_ry002z_fb_notified = 4;
		fb_unregister_client(&mddi_ry002z_fb_event_notifier);
		if (ry_info->fbops->fb_release) {
			ry_info->fbops->fb_release(ry_info, 0);
		}
//		if (ry_wq)
//			destroy_workqueue(ry_wq);
	}

}

static int mddi_ry002z_fb_event_notify(struct notifier_block *self, unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct fb_info *info = event->info;
	int ret = 0;

	if (mddi_ry002z_fb_notified == 0) {
		if (action == FB_EVENT_FB_REGISTERED && info->fbops->fb_open) {
			mddi_ry002z_fb_notified = 1;
			ry_info = info;
			queue_work(ry_wq, &ry_work);
		}
	} else if (mddi_ry002z_fb_notified == 2) {
		struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
		if (2 < mfd->ref_cnt) {		// boot animation part
			mddi_ry002z_fb_notified = 3;
			queue_work(ry_wq, &ry_work);
		}
	}

	return ret;
}
#endif

static int __devinit mddi_ry002z_probe(struct platform_device *pdev)
{
	VULCAN_DEBUG("\n");

#ifdef ENABLE_FB_OPEN_ON_BOOT
	if(!ry_wq)
	{
		ry_wq = create_singlethread_workqueue("ry_wq");
		if(ry_wq)
		{
			INIT_WORK(&ry_work, ry002z_work_func);
		}
	}
#endif

	if (pdev->id == 0) {
		mddi_ry002z_pdata = pdev->dev.platform_data;

		return 0;
	}

	/* Backlight */
	bl_pwm = pwm_request(LCD_BL_PWM, "backlight");
	if (bl_pwm == NULL || IS_ERR(bl_pwm)) {
		pr_err("%s: pwm_request failed\n", __func__);
		bl_pwm = NULL;
	}

	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mddi_ry002z_probe,
	.driver = {
		.name   = "mddi_ry002z_wvga",
	},
};

static struct msm_fb_panel_data mddi_ry002z_panel_data0 = {
	.on = mddi_ry002z_lcd_on,
	.off = mddi_ry002z_lcd_off,
	.set_backlight = mddi_ry002z_set_backlight,
};

static struct platform_device this_device_0 = {
	.name   = "mddi_ry002z_wvga",
	.id	= RY002Z_WVGA_ID,
	.dev	= {
		.platform_data = &mddi_ry002z_panel_data0,
	}
};

static void lcd_id_check(int *board_nums, int* lcd_module)
{
		int g102, g103, g149, g150;
		gpio_tlmm_config(GPIO_CFG(102, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(103, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(149, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(150, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);

		g102 = gpio_get_value(102);
		g103 = gpio_get_value(103);
		g149 = gpio_get_value(149);
		g150 = gpio_get_value(150);

		if(g103 == 0 && g102 == 1)//gpio-i2c
			*board_nums = 0;//PT1
		else if(g103 == 0 && g102 == 0)//dedicate MSM i2c 
			*board_nums = 1;//PT2
		else if(g103 == 1 && g102 == 0)//dedicate MSM i2c 
			*board_nums = 2;//ES
		else if(g103 == 1 && g102 == 0)//dedicate MSM i2c 
			*board_nums = 3;

		if(g149 == 1 && g150 == 1)
			* lcd_module = 0;//Seiko
		else
			* lcd_module = 1;//Truly

		gpio_tlmm_config(GPIO_CFG(149, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_tlmm_config(GPIO_CFG(150, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);

		printk(KERN_ERR "leesh mddi hw version  = (%d), lcd_module=(%d)\n", *board_nums, *lcd_module);
}

static int __init mddi_ry002z_init(void)
{
	int ret, board_nums=2, lcd_module=0;
	struct msm_panel_info *pinfo;

	lcd_id_check(&board_nums, &lcd_module);

	VULCAN_DEBUG("\n");
	ret = platform_driver_register(&this_driver);
	if (!ret) {
		pinfo = &mddi_ry002z_panel_data0.panel_info;
		pinfo->xres = 480;
		pinfo->yres = 800;
		MSM_FB_SINGLE_MODE_PANEL(pinfo);
		pinfo->type = MDDI_PANEL;
		pinfo->wait_cycle = 0;
		pinfo->pdest = DISPLAY_1;
		pinfo->mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;
		pinfo->wait_cycle = 0;
		pinfo->bpp = 24;
		pinfo->fb_num = 3;
		pinfo->clk_rate = 245760000;

		if(lcd_module)//Truly
			pinfo->clk_min = 192000000;//120000000;
		else//Seiko
			pinfo->clk_min = 122880000;//120000000;

		pinfo->clk_max = 445500000;//125000000;
		pinfo->lcd.vsync_enable = TRUE;
		pinfo->lcd.refx100 = 6151;
		pinfo->lcd.v_back_porch = 12;
		pinfo->lcd.v_front_porch = 5;
		pinfo->lcd.v_pulse_width = 4;
		pinfo->lcd.hw_vsync_mode = TRUE;
		pinfo->lcd.vsync_notifier_period = (1 / 60) * 1000000;
		pinfo->bl_max = PWM_LEVEL;
		pinfo->bl_min = 1;

		// data0, data1 enable
		pinfo->lcd.rev = 2;

#ifdef ENABLE_FB_OPEN_ON_BOOT
		fb_register_client(&mddi_ry002z_fb_event_notifier);
#endif

		ret = platform_device_register(&this_device_0);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}

module_init(mddi_ry002z_init);
