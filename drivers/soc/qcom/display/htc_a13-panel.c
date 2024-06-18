#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <mach/msm_memtypes.h>
#include <mach/board.h>
#include <linux/debug_display.h>
#include <linux/htc_flags.h>
#include "../../../../drivers/video/msm/mdss/mdss_dsi.h"

#define PANEL_ID_A13_TIANMA_HIMAX 1
#define PANEL_ID_A13_TIANMA_A2_HIMAX 2
#define PANEL_ID_A13_BITLAND_ILI 3

/* HTC: dsi_power_data overwrite the role of dsi_drv_cm_data
   in mdss_dsi_ctrl_pdata structure */
struct dsi_power_data {
	uint32_t sysrev;         /* system revision info */
	struct regulator *vddio; /* 1.8v */
	struct regulator *vdda;  /* 1.2v */
	struct regulator *vddpll; /* mipi 1.8v */
	int lcm_3v; /* 3v */
	int lcm_bl_en; /* Backlight */
};

#ifdef MODULE
extern struct module __this_module;
#define THIS_MODULE (&__this_module)
#else
#define THIS_MODULE ((struct module *)0)
#endif

const char bootstr[] = "offmode_charging";

static int htc_a13_regulator_init(struct platform_device *pdev)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct dsi_power_data *pwrdata = NULL;

	if (!pdev) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		pr_err("%s: invalid driver data\n", __func__);
		return -EINVAL;
	}

	pwrdata = devm_kzalloc(&pdev->dev,
				sizeof(struct dsi_power_data), GFP_KERNEL);
	if (!pwrdata) {
		pr_err("[DISP] %s: FAILED to alloc pwrdata\n", __func__);
		return -ENOMEM;
	}

	ctrl_pdata->dsi_pwrctrl_data = pwrdata;

	/* LCMIO 1v8 : LDO15 */
	pwrdata->vddio = devm_regulator_get(&pdev->dev, "vddio");
	if (IS_ERR(pwrdata->vddio)) {
		pr_err("%s: could not get vddio reg, rc=%ld\n",
			__func__, PTR_ERR(pwrdata->vddio));
		return PTR_ERR(pwrdata->vddio);
	}

	ret = regulator_set_voltage(pwrdata->vddio, 1800000,
	        1800000);
	if (ret) {
		pr_err("%s: set voltage failed on vddio vreg, rc=%d\n",
			__func__, ret);
		return ret;
	}

#if 0 // L2 is not only for MIPI analog rails. it also is for DDR/eMMC power.
	pwrdata->vdda = devm_regulator_get(&pdev->dev, "vdda");//L2 MIPI 1.2V
	if (IS_ERR(pwrdata->vdda)) {
		pr_err("%s: could not get vdda vreg, rc=%ld\n",
			__func__, PTR_ERR(pwrdata->vdda));
		return PTR_ERR(pwrdata->vdda);
	}
	ret = regulator_set_voltage(pwrdata->vdda, 1200000,
		1200000);
	if (ret) {
	    pr_err("%s: set voltage failed on vdda vreg, rc=%d\n",
	        __func__, ret);
	    return ret;
	}
#endif
	/* LCM 3V : GPIO 52 */
	pwrdata->lcm_3v = of_get_named_gpio(pdev->dev.of_node,
						"htc,lcm_3v-gpio", 0);
	/* LCM BL EN : GPIO 110 */
	pwrdata->lcm_bl_en = of_get_named_gpio(pdev->dev.of_node,
						"htc,lcm_bl_en-gpio", 0);

	return 0;
}

static int htc_a13_regulator_deinit(struct platform_device *pdev)
{
	/* devm_regulator() will automatically free regulators
	   while dev detach. */

	/* nothing */

	return 0;
}

void htc_a13_panel_reset(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_err("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return;
	}

	pr_debug("%s: enable = %d\n", __func__, enable);
	if (enable) {
		if (pdata->panel_info.first_power_on == 1) {
			PR_DISP_INFO("reset already on in first time\n");
			return;
		}
		if (pdata->panel_info.panel_id == PANEL_ID_A13_TIANMA_HIMAX ||
			pdata->panel_info.panel_id == PANEL_ID_A13_TIANMA_A2_HIMAX) {
			gpio_set_value((ctrl_pdata->rst_gpio), 1);
			usleep(10000);
			gpio_set_value((ctrl_pdata->rst_gpio), 0);
			usleep(50000);
			gpio_set_value((ctrl_pdata->rst_gpio), 1);
			usleep(10000);
		} else if (pdata->panel_info.panel_id == PANEL_ID_A13_BITLAND_ILI) {
			gpio_set_value((ctrl_pdata->rst_gpio), 1);
			usleep(1000);
			gpio_set_value((ctrl_pdata->rst_gpio), 0);
			usleep(10000);
			gpio_set_value((ctrl_pdata->rst_gpio), 1);
			usleep(120000);
		}
	} else {
		gpio_set_value((ctrl_pdata->rst_gpio), 0);
		usleep(5000);
	}
}

static int htc_a13_panel_power_on(struct mdss_panel_data *pdata, int enable)
{
	int ret;

	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct dsi_power_data *pwrdata = NULL;

	PR_DISP_INFO("%s: en=%d\n", __func__, enable);
	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pwrdata = ctrl_pdata->dsi_pwrctrl_data;

	if (!pwrdata) {
		pr_err("%s: pwrdata not initialized\n", __func__);
		return -EINVAL;
	}

	if (enable) {
#if 0
		ret = regulator_set_optimum_mode(pwrdata->vddio, 100000);
		if (ret < 0) {
			pr_err("%s: vddio set opt mode failed.\n",
				__func__);
			return ret;
		}
#endif
#if 0 // L2 is not only for MIPI analog rails. it also is for DDR/eMMC power.
		ret = regulator_set_optimum_mode(pwrdata->vdda, 100000);
		if (ret < 0) {
			pr_err("%s: vdda set opt mode failed.\n",
				__func__);
			return ret;
		}
#endif
		/* LCMIO 1v8 ENABLE */
		ret = regulator_enable(pwrdata->vddio);
		if (ret) {
			pr_err("%s: Failed to enable regulator.\n",
				__func__);
			return ret;
		}
		usleep(1000);

#if 0 // L2 is not only for MIPI analog rails. it also is for DDR/eMMC power.
		/*ENABLE 1V2*/
		ret = regulator_enable(pwrdata->vdda);//L2 MIPI 1.2V
		if (ret) {
			pr_err("%s: Failed to enable regulator : vdda.\n",
				__func__);
			return ret;
		}
		msleep(20);
#endif

		/* LCM 3V ENABLE */
		gpio_set_value(pwrdata->lcm_3v, 1);
		usleep(1000);

		gpio_set_value(pwrdata->lcm_bl_en, 1);
		usleep(1000);
	} else {
		gpio_set_value(pwrdata->lcm_bl_en, 0);

		/* LCM 3V DISABLE */
		gpio_set_value(pwrdata->lcm_3v, 0);
		usleep(1000);

#if 0 // L2 is not only for MIPI analog rails. it also is for DDR/eMMC power.
		ret = regulator_disable(pwrdata->vdda);//L2 MIPI 1.2V
		if (ret) {
			pr_err("%s: Failed to disable regulator.\n",
				__func__);
			return ret;
		}
#endif
		/* LCMIO 1v8 disable */
		ret = regulator_disable(pwrdata->vddio);
		if (ret) {
			pr_err("%s: Failed to disable regulator : vddio.\n",
				__func__);
			return ret;
		}
		usleep(2000);
#if 0
		ret = regulator_set_optimum_mode(pwrdata->vddio, 100);
		if (ret < 0) {
			pr_err("%s: vdd_io_vreg set opt mode failed.\n",
				__func__);
			return ret;
		}
#endif
#if 0 // L2 is not only for MIPI analog rails. it also is for DDR/eMMC power.
		ret = regulator_set_optimum_mode(pwrdata->vdda, 100);
		if (ret < 0) {
			pr_err("%s: vdda_vreg set opt mode failed.\n",
				__func__);
			return ret;
		}
#endif
	}
	PR_DISP_INFO("%s: en=%d done\n", __func__, enable);

	return 0;
}

static struct mdss_dsi_pwrctrl dsi_pwrctrl = {
	.dsi_regulator_init = htc_a13_regulator_init,
	.dsi_regulator_deinit = htc_a13_regulator_deinit,
	.dsi_power_on = htc_a13_panel_power_on,
	.dsi_panel_reset = htc_a13_panel_reset,
};

static struct platform_device dsi_pwrctrl_device = {
	.name          = "mdss_dsi_pwrctrl",
	.id            = -1,
	.dev.platform_data = &dsi_pwrctrl,
};

int __init htc_8909_dsi_panel_power_register(void)
{
	pr_info("%s#%d\n", __func__, __LINE__);
	platform_device_register(&dsi_pwrctrl_device);
	return 0;
}

arch_initcall(htc_8909_dsi_panel_power_register);
