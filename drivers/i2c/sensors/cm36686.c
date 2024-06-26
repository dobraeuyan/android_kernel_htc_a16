/* drivers/i2c/chips/cm36686.c - cm36686 optical sensors driver
 *
 * Copyright (C) 2014 HTC, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define CM36686
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/lightsensor.h>
#include <linux/slab.h>
#include <linux/cm36686.h>
#include <linux/pl_sensor.h>
#include <linux/capella_cm3602.h>
#include <linux/wakelock.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <asm/uaccess.h> // get_usr
//#include <mach/devices_cmdline.h> // board_mfg_mode() /*FIXME*/
//#include "../../drivers/video/msm/msm_fb.h" /*FIXME*/
//
//#include <mach/rpm-regulator.h> /*FIXME*/
#include <linux/regulator/consumer.h>
//==============================================


#if 0
//#include <asm/mach-types.h>
#include <asm/setup.h>
//#include <mach/board.h>
//#include <mach/board_htc.h>
//#include <mach/rpm-regulator.h>
//#include <mach/devices_cmdline.h>
#endif

#define D(x...) pr_info(x)
#define I2C_RETRY_COUNT		10
#define MFG_MODE		1
#define NEAR_DELAY_TIME 	((100 * HZ) / 1000)
#define Max_open_value		500
#define CALIBRATION_DATA_PATH	"/calibration_data"
#define LIGHT_SENSOR_FLASH_DATA	"als_flash"
#define PSENSOR_FLASH_DATA	"ps_flash"
#define POLLING_DELAY		200
#define TH_ADD			100

static int record_init_fail = 0;
static void sensor_irq_do_work(struct work_struct *work);
static DECLARE_WORK(sensor_irq_work, sensor_irq_do_work);
static void polling_do_work(struct work_struct *w);
static DECLARE_DELAYED_WORK(polling_work, polling_do_work);
static uint8_t sensor_chipId[3] = {0};
static void report_near_do_work(struct work_struct *w);
static DECLARE_DELAYED_WORK(report_near_work, report_near_do_work);
static int inter_error = 0;
static int is_probe_success;
static int lightsensor_cali;
static int psensor_cali;
static int min_adc;
static int call_count = 0;
static uint32_t correction_table[10] = {0};
static uint32_t adctable[10] = {0};
static uint32_t luxtable[10] = {0, 4, 10, 16, 75, 150, 1000, 1500, 2700, 4070};
static int record_adc[6] = {0};
static int avg_min_adc = 0;
static int p_status = 9;
static int p_irq_status;
static int prev_correction;
static uint16_t ps_canc_set;
static uint16_t mfg_thd;
static struct cm36686_info *lp_info;
static int enable_cm36686_log = 0;
static int f_cm36686_level = -1;
static int current_lightsensor_adc;
static int current_lightsensor_kadc;
static struct mutex als_enable_mutex, als_disable_mutex, als_get_adc_mutex;
static struct mutex ps_enable_mutex;
static int ps_hal_enable, ps_drv_enable;
static int lightsensor_enable(struct cm36686_info *lpi);
static void lightsensor_enable_work_func(struct work_struct *work);
static int lightsensor_disable(struct cm36686_info *lpi);
static void psensor_initial_cmd(struct cm36686_info *lpi);
static int ps_near;
static int pocket_mode_flag, psensor_enable_by_touch;
static int ps_kparam1 = 0;
static int ps_kparam2 = 0;
static int als_kadc = 0;
static int phone_status;
//static int oncall = 0;/*FIXME*/
static int PS_max;
module_param(p_status, int, 0444);

struct cm36686_info {
	struct class *cm36686_class;
	struct device *ls_dev;
	struct device *ps_dev;
	struct input_dev *ls_input_dev;
	struct input_dev *ps_input_dev;
	struct notifier_block fb_notif;
	struct workqueue_struct *cm36686_fb_wq;
	struct delayed_work work_fb;
	struct work_struct lightsensor_enable_work;
	struct i2c_client *i2c_client;
	struct workqueue_struct *lp_wq;
	struct wake_lock ps_wake_lock;
	struct regulator	*sr_2v85;
	int intr_pin;
	int als_enable;
	int ps_enable;
	int ps_irq_flag;
	uint32_t *adc_table;
	uint32_t *lux_table;
	uint16_t cali_table[10];
	int irq;
	int ls_calibrate;
	int (*power)(int, uint8_t); /* power to the chip */
	int (*lpm_power)(int on); /* power to the chip */
	uint32_t als_kadc;
	uint32_t emmc_als_kadc;
	uint32_t als_gadc;
	uint16_t golden_adc;
	int psensor_opened;
	int lightsensor_opened;
	uint16_t cm36686_slave_address;
	uint32_t emmc_ps_kadc1;
	uint32_t emmc_ps_kadc2;
	uint32_t ps_kparam1;
	uint32_t ps_kparam2;
	uint16_t ps_thd_set;
	uint16_t ps_thh_diff;
	uint16_t original_ps_thd_set;
	int current_level;
	uint16_t current_adc;
	uint16_t inte_ps_canc;
	uint16_t mfg_thd;
	uint8_t ps_conf1_val;
	uint8_t ps_conf2_val;
	uint8_t ps_conf1_val_from_board;
	uint8_t ps_conf2_val_from_board;
	uint8_t ps_conf3_val;
	uint8_t ps_ms_val;
	int ps_pocket_mode;
	unsigned long j_start;
	unsigned long j_end;
	int mfg_mode;
	uint16_t *mapping_table;
	uint8_t mapping_size;
	uint8_t ps_base_index;
	uint16_t ps_thd_no_cal;
	uint32_t dynamical_threshold;
	uint8_t ls_cmd;
	uint8_t ps_debounce;
	uint16_t ps_delay_time;
	unsigned int no_need_change_setting;
	int ps_th_add;
	uint8_t dark_level;
	int use__PS2v85;
	uint32_t *correction_table;
	int SR_3v_used;
	int ps_stop_polling;
	struct pinctrl *pinctrl;
	struct pinctrl_state *gpio_state_init;
};

static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data);
static uint16_t cm36686_mapping_table[] = {
	0x000, 0x030, 0x060, 0x090, 0x0C0,
	0x0F0, 0x120, 0x150, 0x180, 0x1B0,
	0x1E0, 0x210, 0x240, 0x270, 0x2A0,
	0x2D0, 0x300, 0x330, 0x360, 0x390,
	0x3C0, 0x3F0, 0x430, 0x470, 0x4B0,
	0x4F0, 0x530, 0x570, 0x5B0, 0x5F0,
	0x630, 0x670, 0x6B0, 0x700, 0x750,
	0x7A0, 0x7F0, 0x840, 0x890, 0x8E0,
	0x930, 0x980, 0x9D0, 0xA20, 0xA80,
	0xAE0, 0xB40, 0xBA0, 0xC00, 0xC60,
	0xCC0, 0xD30, 0xDA0, 0xE10, 0xE80,
	0xEF0, 0xF60, 0xFF0};
int get_lightsensoradc(void)
{
	return current_lightsensor_adc;

}
int get_lightsensorkadc(void)
{
	return current_lightsensor_kadc;

}

static int I2C_RxData_2(char *rxData, int length)
{
	uint8_t loop_i;
	struct cm36686_info *lpi = lp_info;

	struct i2c_msg msgs[] = {
		{
			.addr = lp_info->i2c_client->addr,
			.flags = 0,
			.len = 1,
			.buf = rxData,
		},
		{
			.addr = lp_info->i2c_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rxData,
		},
	};

	for (loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(lp_info->i2c_client->adapter, msgs, 2) > 0)
			break;

		D("[PS][cm36686 warning] %s, i2c err, ISR gpio %d\n",
				__func__, lpi->intr_pin);
		msleep(10);
	}

	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[PS_ERR][cm36686 error] %s retry over %d\n",
				__func__, I2C_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}
static int I2C_TxData(uint16_t slaveAddr, uint8_t *txData, int length)
{
	uint8_t loop_i;
	struct cm36686_info *lpi = lp_info;
	struct i2c_msg msg[] = {
		{
			.addr = slaveAddr,
			.flags = 0,
			.len = length,
			.buf = txData,
		},
	};

	for (loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(lp_info->i2c_client->adapter, msg, 1) > 0)
			break;

		D("[PS][cm36686 warning] %s, i2c err, slaveAddr 0x%x, register 0x%x, value 0x%x, ISR gpio%d, record_init_fail %d\n",
				__func__, slaveAddr, txData[0], txData[1], lpi->intr_pin, record_init_fail);

		msleep(10);
	}

	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[PS_ERR][cm36686 error] %s retry over %d\n", __func__, I2C_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int _cm36686_I2C_Read2(uint16_t slaveAddr,
		uint8_t cmd, uint8_t *pdata, int length)
{
	char buffer[3] = {0};
	int ret = 0, i;

	if (pdata == NULL)
		return -EFAULT;

	if (length > 2) {
		pr_err(
				"[PS_ERR][cm36686 error]%s: length %d> 2: \n", __func__, length);
		return ret;
	}
	buffer[0] = cmd;
	ret = I2C_RxData_2(buffer, length);
	if (ret < 0) {
		pr_err(
				"[PS_ERR][cm36686 error]%s: I2C_RxData fail, slave addr: 0x%x\n", __func__, slaveAddr);
		return ret;
	}

	for (i = 0; i < length; i++) {
		*(pdata+i) = buffer[i];
	}
#if 0
	/* Debug use */
	printk(KERN_DEBUG "[cm36686] %s: I2C_RxData[0x%x] = 0x%x\n",
			__func__, slaveAddr, buffer);
#endif
	return ret;
}

static int _cm36686_I2C_Write2(uint16_t SlaveAddress,
		uint8_t cmd, uint8_t *data, int length)
{
	char buffer[3];
	int ret = 0;
#if 0
	/* Debug use */
	printk(KERN_DEBUG
			"[cm36686] %s: _cm36686_I2C_Write_Byte[0x%x, 0x%x, 0x%x]\n",
			__func__, SlaveAddress, cmd, *data);
#endif
	if (length > 3) {
		pr_err("[PS_ERR][cm36686 error]%s: length %d> 2: \n", __func__, length);
		return ret;
	}

	buffer[0] = cmd;
	buffer[1] = *data;
	buffer[2] = *(data+1);
	ret = I2C_TxData(SlaveAddress, buffer, length);
	if (ret < 0) {
		pr_err("[PS_ERR][cm36686 error]%s: I2C_TxData fail\n", __func__);
		return -EIO;
	}

	return ret;
}
static int sensor_lpm_power(int enable)
{
	struct cm36686_info *lpi = lp_info;

	if (lpi->lpm_power)
		lpi->lpm_power(enable);

	return 0;
}
static int get_ls_adc_value(uint32_t *als_step, int resume)
{

	struct cm36686_info *lpi = lp_info;
	uint8_t	lsb, msb;
	int ret = 0;
	char cmd[3];
	char ls_cmd;
	uint32_t als_step_temp = 0;

	if (als_step == NULL)
		return -EFAULT;

	if (resume == 1) {
		ls_cmd = (CM36686_ALS_IT_80ms | CM36686_ALS_PERS_1);	/* disable CM36686_ALS_INT_EN */
		D("[LS][cm36686] %s:resume %d\n", __func__, resume);
	} else
		ls_cmd = (lpi->ls_cmd);					/* disable CM36686_ALS_INT_EN */

	cmd[0] = ls_cmd;
	cmd[1] = 0;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address, ALS_config_cmd, cmd, 3);

	if (ret < 0) {
		pr_err("[LS][cm36686 error]%s: _cm36686_I2C_Write_Byte fail\n", __func__);
		return -EIO;
	}

	/* Read ALS data */

	ret = _cm36686_I2C_Read2(lpi->cm36686_slave_address, ALS_data, cmd, 2);
	if (ret < 0) {
		pr_err("[LS][cm36686 error]%s: _cm36686_I2C_Read_Byte  fail\n",	__func__);
		return -EIO;
	}
	lsb = cmd[0];
	msb = cmd[1];

	*als_step = (uint32_t)msb;
	*als_step <<= 8;
	*als_step |= (uint32_t)lsb;
	if (resume != 2)
		D("[LS][cm36686] %s: raw adc = 0x%X, ls_calibrate = %d\n", __func__, *als_step, lpi->ls_calibrate);


	if (!lpi->ls_calibrate) {
		als_step_temp = *als_step;
		*als_step = (*als_step) * lpi->als_gadc / lpi->als_kadc;
		if( ((*als_step)*lpi->als_kadc) < (als_step_temp*lpi->als_gadc)) {
			*als_step = (*als_step) + 1;
		}
		if (*als_step > 0xFFFF)
			*als_step = 0xFFFF;
	}

	return ret;
}


static int get_ps_adc_value(uint16_t *ps_adc)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;
	char cmd[3];

	if (ps_adc == NULL)
		return -EFAULT;

	ret = _cm36686_I2C_Read2(lpi->cm36686_slave_address, PS_data, cmd, 2);
	if (ret < 0) {
		pr_err("[PS_ERR][cm36686 error] %s: _cm36686_I2C_Read_Byte MSB fail\n", __func__);
		return -EIO;
	}
	*ps_adc = (cmd[1] << 8) | cmd[0];
	/*
	   pr_info("[PS][cm36686] %s: PS1_ADC = 0x%02X, PS2_ADC = 0x%02X\n",
	   __func__, *ps1_adc, *ps2_adc);
	 */
	return ret;
}

static int set_lsensor_range(uint16_t low_thd, uint16_t high_thd)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;
	char cmd[3] = {0};

	uint8_t	high_msb;
	uint8_t	high_lsb;
	uint8_t	low_msb;
	uint8_t	low_lsb;

	D("[cm36686] %s: low_thd = 0x%X, high_thd = 0x%x \n", __func__, low_thd, high_thd);
	high_msb = (uint8_t) (high_thd >> 8);
	high_lsb = (uint8_t) (high_thd & 0x00ff);
	low_msb	 = (uint8_t) (low_thd >> 8);
	low_lsb	 = (uint8_t) (low_thd & 0x00ff);

	cmd[0] = high_lsb;
	cmd[1] = high_msb;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address,	ALS_high_thd, cmd, 3);

	cmd[0] = low_lsb;
	cmd[1] = low_msb;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address,	ALS_low_thd, cmd, 3);
	return ret;
}

static void report_near_do_work(struct work_struct *w)
{
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686]  %s: delay %dms, report proximity NEAR\n", __func__, lpi->ps_delay_time);

	input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, 0);
	input_sync(lpi->ps_input_dev);
	//blocking_notifier_call_chain(&psensor_notifier_list, 2 + oncall, NULL); /*FIXME*/
}

static void report_psensor_input_event(struct cm36686_info *lpi, int interrupt_flag)
{
	uint16_t ps_thd_set = 0;
	uint16_t ps_adc = 0;
	uint16_t ps1_adc = 0;
	int val, ret = 0;
	int index = 0;

	if (interrupt_flag > 1 && lpi->ps_enable == 0) {
		D("[PS][cm36686] P-sensor disable but intrrupt occur, record_init_fail %d.\n", record_init_fail);
		return;
	}

	if (lpi->ps_debounce == 1 && lpi->mfg_mode != MFG_MODE)
		cancel_delayed_work(&report_near_work);

	lpi->j_end = jiffies;
	/* D("%s: j_end = %lu", __func__, lpi->j_end); */

	ret = get_ps_adc_value(&ps1_adc);

	if (pocket_mode_flag == 1 || psensor_enable_by_touch == 1) {
		D("[PS][cm36686] pocket_mode_flag: %d, psensor_enable_by_touch: %d, add delay = 7ms\n", pocket_mode_flag, psensor_enable_by_touch);
		mdelay(7);		/* 0.32ms * IT * ITB * 12 = 6.144*/
		while (index <= 10 && ps1_adc == 0) {
			D("[PS][cm36686]ps1_adc = 0 retry");
			get_ps_adc_value(&ps1_adc);
			if(ps1_adc != 0) {
				D("[PS][cm36686]retry work");
				break;
			}
			mdelay(1);
			index++;
		}
	}

	if (lpi->ps_thh_diff == 0)
		ps_thd_set = lpi->ps_thd_set + 1;
	else
		ps_thd_set = lpi->ps_thd_set + lpi->ps_thh_diff;

	ps_adc = ps1_adc;

	if (interrupt_flag == 0) {	/*interrupt_fla = 0 meam polling mode*/
		if (ret == 0) {
			val = (ps_adc >= ps_thd_set) ? 0 : 1;
		} else {		/*i2c err, report far to workaround*/
			val = 1;
			ps_adc = 0;
			D("[PS][cm36686] proximity i2c err, report %s, ps_adc=%d, record_init_fail %d\n",
					val ? "FAR" : "NEAR", ps_adc, record_init_fail);
		}
	} else {			/*interrupt_fla = 2 meam close isr,  interrupt_fla = 1 mean far isr*/
		val = (interrupt_flag == 2) ? 0 : 1;
	}
	ps_near = !val;

	if (lpi->ps_debounce == 1 && lpi->mfg_mode != MFG_MODE) {
		if (val == 0) {
			/*======== dynamic threshold adjustment ====*/
			if (lpi->dynamical_threshold == 1 && val == 0
					&& pocket_mode_flag != 1 && psensor_enable_by_touch != 1 &&
					time_before(lpi->j_end, (lpi->j_start + NEAR_DELAY_TIME))) {
				lpi->ps_pocket_mode = 1;
//				blocking_notifier_call_chain(&psensor_notifier_list, 2 + oncall, NULL); /*FIXME*/
				D("[PS][cm36686] Ignore NEAR event\n");
				return;
				/*======== dynamic threshold adjustment ====*/
			}
			D("[PS][cm36686] delay proximity %s, ps_adc=%d, High thd= %d, interrupt_flag %d\n",
					val ? "FAR" : "NEAR", ps_adc, ps_thd_set, interrupt_flag);
			queue_delayed_work(lpi->lp_wq, &report_near_work,
					msecs_to_jiffies(lpi->ps_delay_time));
			return;
		} else {
			/* dummy report */
			input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, -1);
			input_sync(lpi->ps_input_dev);
		}
	}
	D("[PS][cm36686] proximity %s, ps_adc=%d, High thd= %d, interrupt_flag %d, calibration %d\n",
			val ? "FAR" : "NEAR", ps_adc, ps_thd_set, interrupt_flag, psensor_cali);
	/*======== dynamic threshold adjustment ====*/
	if (lpi->dynamical_threshold == 1 && val == 0 && lpi->mfg_mode != MFG_MODE &&
			pocket_mode_flag != 1 && psensor_enable_by_touch != 1 &&
			time_before(lpi->j_end, (lpi->j_start + NEAR_DELAY_TIME))) {
//		blocking_notifier_call_chain(&psensor_notifier_list, 2 + oncall, NULL); /*FIXME*/
		lpi->ps_pocket_mode = 1;
		D("[PS][cm36686] Ignore NEAR event\n");
		/*======== dynamic threshold adjustment ====*/
	} else {
		/* 0 is close, 1 is far */
		input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, val);
		input_sync(lpi->ps_input_dev);
//		blocking_notifier_call_chain(&psensor_notifier_list, val + 2 + oncall, NULL); /*FIXME*/
	}
}

static void enable_als_interrupt(void)
{
	char cmd[3];
	struct cm36686_info *lpi = lp_info;
	int ret = 0;

	cmd[0] = (lpi->ls_cmd | CM36686_ALS_INT_EN);
	cmd[1] = 0;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address,
			ALS_config_cmd, cmd, 3);
	if (ret != 0) {
		lpi->als_enable = 0;
		D("[LS][cm36686] L-sensor i2c err, enable interrupt error\n");
	} else
		lpi->als_enable = 1;
}

static void report_lsensor_input_event(struct cm36686_info *lpi, int resume)
{
	/*when resume need report a data, so the paramerter need to quick reponse*/
	uint32_t adc_value = 0;

	int level = 0, i, ret = 0;

	mutex_lock(&als_get_adc_mutex);

	ret = get_ls_adc_value(&adc_value, resume);

	if (resume) {
		adc_value = adc_value * 4;	/*because the cm36686_ALS_IT for 320ms - >80ms*/
		if (adc_value > 0xFFFF)
			adc_value = 0xFFFF;
	}
	for (i = 0; i < 10; i++) {
		if (adc_value <= (*(lpi->adc_table + i))) {
			level = i;
			if (*(lpi->adc_table + i))
				break;
		}
		if (i == 9) {			/*avoid  i = 10, because 'cali_table' of size is 10 */
			level = i;
			break;
		}
	}
	ret = set_lsensor_range(((i == 0) || (adc_value == 0)) ? 0 :
			*(lpi->cali_table + (i - 1)) + 1,
			*(lpi->cali_table + i));

	if (ret < 0)
		printk(KERN_ERR "[LS][cm36686 error] %s fail\n", __func__);

	if ((i == 0) || (adc_value == 0))
		D("[LS][cm36686] %s: ADC=0x%03X, Level=%d, l_thd equal 0, h_thd = 0x%x, calibration %d \n",
				__func__, adc_value, level, *(lpi->cali_table + i), lightsensor_cali);
	else
		D("[LS][cm36686] %s: ADC=0x%03X, Level=%d, l_thd = 0x%x, h_thd = 0x%x, calibration %d \n",
				__func__, adc_value, level, *(lpi->cali_table + (i - 1)) + 1, *(lpi->cali_table + i), lightsensor_cali);
	current_lightsensor_adc = adc_value;
	lpi->current_level = level;
	lpi->current_adc = adc_value;

	/*D("[cm36686] %s: *(lpi->cali_table + (i - 1)) + 1 = 0x%X, *(lpi->cali_table + i) = 0x%x \n", __func__, *(lpi->cali_table + (i - 1)) + 1, *(lpi->cali_table + i));*/
	if (f_cm36686_level >= 0) {
		D("[LS][cm36686] L-sensor force level enable level=%d f_cm36686_level=%d\n", level, f_cm36686_level);
		level = f_cm36686_level;
	}
	input_report_abs(lpi->ls_input_dev, ABS_MISC, level);
	input_sync(lpi->ls_input_dev);
	enable_als_interrupt();
	mutex_unlock(&als_get_adc_mutex);
}

static void enable_ps_interrupt(char *ps_conf)
{
	struct cm36686_info *lpi = lp_info;
	int ret;
	char cmd[2] = {0};

	lpi->ps_enable = 1;
	cmd[0] = lpi->ps_thd_set & 0xFF;
	cmd[1] = (lpi->ps_thd_set >> 8) & 0xFF;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_low, cmd, 3);

	if (lpi->ps_thh_diff == 0) {
		cmd[0] = (lpi->ps_thd_set + 1) & 0xFF;
		cmd[1] = ((lpi->ps_thd_set + 1) >> 8) & 0xFF;
	} else {
		cmd[0] = (lpi->ps_thd_set + lpi->ps_thh_diff) & 0xFF;
		cmd[1] = ((lpi->ps_thd_set + lpi->ps_thh_diff) >> 8) & 0xFF;
	}
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_high, cmd, 3);

	cmd[0] = ps_conf[2];
	cmd[1] = ps_conf[3];
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address,
			PS_config_ms, cmd, 3);

	cmd[0] = ps_conf[0];	/*power on at last step*/
	cmd[1] = ps_conf[1];
	D("[PS][cm36686] %s, write cmd[0] = 0x%x, cmd[1] = 0x%x\n", __func__, cmd[0], cmd[1]);
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config, cmd, 3);

	if (ret != 0) {
		lpi->ps_enable = 0;
		D("[PS][cm36686] P-sensor i2c err, enable interrupt error\n");
	}
	_cm36686_I2C_Read2(lpi->cm36686_slave_address, PS_config, cmd, 2);
	D("[PS][cm36686] %s, read value => cmd[0] = 0x%x, cmd[1] = 0x%x\n", __func__, cmd[0], cmd[1]);
}

static int lightsensor_disable(struct cm36686_info *lpi);

static void sensor_irq_do_work(struct work_struct *work)
{
	struct cm36686_info *lpi = lp_info;
	uint8_t cmd[3];
	uint8_t add = 0;
	/* Check ALS or PS */
	_cm36686_I2C_Read2(lpi->cm36686_slave_address, INT_FLAG, cmd, 2);
	add = cmd[1];
	/*D("[cm36686] %s:, INTERRUPT = 0x%x \n", __func__, add); */

	if ((add & CM36686_PS_IF_AWAY) || (add & CM36686_PS_IF_CLOSE)) {
		wake_lock_timeout(&(lpi->ps_wake_lock), 2*HZ);
		inter_error = 0;
		if (add & CM36686_PS_IF_AWAY) {
			report_psensor_input_event(lpi, 1);	/*1 meam far*/
			p_irq_status = 0;
			/*======== dynamic threshold adjustment ====*/
			min_adc = PS_max;
			lpi->ps_base_index = lpi->mapping_size - 1;
			/*======== dynamic threshold adjustment ====*/
		} else {
			report_psensor_input_event(lpi, 2);	/*2 meam close*/
			p_irq_status = 1;
		}
		if (lpi->ps_pocket_mode | p_irq_status)
			p_status = 0;
		else
			p_status = 1;
	}

	if (((add & CM36686_ALS_IF_L) == CM36686_ALS_IF_L) ||
			((add & CM36686_ALS_IF_H) == CM36686_ALS_IF_H)) {
		if (lpi->lightsensor_opened) {
			inter_error = 0;
			report_lsensor_input_event(lpi, 0);
		} else {
			lightsensor_disable(lpi);
		}
	}

	if (!(add & 0x3F)) {					/*Not P-sensor or Light sensor trigger*/
		if (inter_error < 30) {
			D("[PS][cm36686 warning]%s unkown interrupt: 0x%x!\n", __func__, add);
			inter_error++ ;
		} else {
			pr_err("[PS][cm36686 error]%s error: unkown interrupt: 0x%x!\n", __func__, add);
		}
	}
	enable_irq(lpi->irq);
}

static uint16_t mid_value(uint16_t value[], uint16_t size)
{
	int i = 0, j = 0;
	uint16_t temp = 0;

	if (size < 3)
		return 0;

	for (i = 0; i < (size - 1); i++)
		for (j = (i + 1); j < size; j++)
			if (value[i] > value[j]) {
				temp = value[i];
				value[i] = value[j];
				value[j] = temp;
			}
	return value[((size - 1) / 2)];
}

static int get_stable_ps_adc_value(uint16_t *ps_adc1)
{
	int ret = 0;
	int i = 0;
	uint16_t mid_adc1 = 0;
	uint16_t adc1[3] = {0, 0, 0};

	for (i = 0; i < 3; i++) {
		ret = get_ps_adc_value(&adc1[i]);
		if (ret < 0) {
			pr_err("[PS_ERR][cm36686 error]%s: get_ps_adc_value\n",	__func__);
			return -EIO;
		}
	}

	mid_adc1 = mid_value(adc1, 3);

	*ps_adc1 = mid_adc1;

	return 0;
}

static void compute_light_sensor_correction(uint32_t adc_value, uint8_t *correction)
{
	struct cm36686_info *lpi = lp_info;
	int i = 0;

	for( i=0 ; i<10 ; i++)
	{
		if( i == 0 && *(lpi->correction_table + i) == 0 )
		{
			*correction = 0;
			break;
		}
		if( adc_value < *(lpi->correction_table + i) )
		{
			*correction = i;
			break;
		}
		if(i == 9)
			*correction = i+1;
	}

	if (*correction != prev_correction)
		D("[PS][cm36686] %s: light_sensor_correction: %d\n", __func__, *correction);
	prev_correction = *correction;
}

static void polling_do_work(struct work_struct *w)
{
	struct cm36686_info *lpi = lp_info;
	uint16_t ps_adc = 0;
	int i = 0;
	int ret = 0;
	char cmd[3];
	uint32_t ls_adc = 0;
	uint8_t light_sensor_correction = 0;
	/*D("lpi->ps_enable = %d\n", lpi->ps_enable);*/
	lpi->j_end = jiffies;
	if (time_after(lpi->j_end, (lpi->j_start + 3* HZ))){
		lpi->ps_pocket_mode = 0;
		if (lpi->ps_pocket_mode | p_irq_status)
			p_status = 0;
		else
			p_status = 1;
	}
	if (lpi->ps_enable == 0)
		return;

	ret = get_stable_ps_adc_value(&ps_adc);
	if (lpi->lightsensor_opened) {
		ret = get_ls_adc_value(&ls_adc, 2);
		enable_als_interrupt();
		compute_light_sensor_correction(ls_adc, &light_sensor_correction);
	} else
		light_sensor_correction = 0;
	/*
	   D("[cm36686] Polling: ps_adc = 0x%02X, ps_adc2 = 0x%02X, "
	   "ps_next_base_value = 0x%02X, ps1_thd_set = 0x%02X\n",
	   ps_adc, ps_adc2, lpi->mapping_table[lpi->ps_base_index],
	   lpi->ps1_thd_set);
	 */

	if ( min_adc > (ps_adc + light_sensor_correction) ) {
		//		D("[PS][cm36686] min_adc = %d, ps_adc = %d, light_sensor_correction = %d", min_adc, ps_adc, light_sensor_correction);
		avg_min_adc = 0;
		min_adc = ps_adc + light_sensor_correction;
		if (call_count < 2) {
			avg_min_adc = min_adc;
			for (i = 0; i < 5; i++)
				record_adc[i] = avg_min_adc;
		} else {
			record_adc[5] = min_adc;
			for (i = 1 ;i <= 5; i++) {
				avg_min_adc = avg_min_adc + record_adc[i];
			}
			D("[PS][cm36686] %s: record_adc[1-5]: %d, %d, %d, %d, %d\n", __func__, record_adc[1], record_adc[2], record_adc[3], record_adc[4], record_adc[5]);
			avg_min_adc = avg_min_adc / 5;
			if ((min_adc - avg_min_adc) >= lpi->ps_th_add)
				avg_min_adc = min_adc;
		}
	}

	for (i = lpi->ps_base_index; i >= 1; i--) {
		if (avg_min_adc > lpi->mapping_table[i])
			break;
		else if ((avg_min_adc > lpi->mapping_table[(i-1)]) &&
				(avg_min_adc <= lpi->mapping_table[i])) {
			lpi->ps_base_index = (i-1);

			if (i == (lpi->mapping_size - 1))
				lpi->ps_thd_set = 0xFFFF;
			else
				lpi->ps_thd_set = (lpi->mapping_table[i] + lpi->ps_th_add);

			if (lpi->ps_thd_set <= avg_min_adc)
				lpi->ps_thd_set = 0xFFFF;

			cmd[0] = (uint8_t) lpi->ps_thd_set & 0xFF;
			cmd[1] = (uint8_t) (lpi->ps_thd_set >> 8) & 0xFF;
			_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_low, cmd, 3);

			if (lpi->ps_thh_diff <= 0) {
				cmd[0] = (uint8_t) (lpi->ps_thd_set + 1) & 0xFF;
				cmd[1] = (uint8_t) ((lpi->ps_thd_set + 1) >> 8) & 0xFF;
			} else {
				cmd[0] = (uint8_t) (lpi->ps_thd_set + lpi->ps_thh_diff) & 0xFF;
				cmd[1] = (uint8_t) ((lpi->ps_thd_set + lpi->ps_thh_diff) >> 8) & 0xFF;
			}
			_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_high, cmd, 3);

			D("[PS][cm36686] SET THD1: lpi->ps_thd_set = %d, lpi->ps_thh_diff= %d avg_min_adc = %d\n",
					lpi->ps_thd_set, lpi->ps_thh_diff, avg_min_adc);
			break;
		}
	}

	if (avg_min_adc == 0 && lpi->ps_thd_set != (lpi->mapping_table[0] + lpi->ps_th_add)) {

		lpi->ps_thd_set = (lpi->mapping_table[0] + lpi->ps_th_add);

		cmd[0] = (uint8_t) lpi->ps_thd_set & 0xFF;
		cmd[1] = (uint8_t) (lpi->ps_thd_set >> 8) & 0xFF;
		_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_low, cmd, 3);

		if (lpi->ps_thh_diff <= 0) {
			cmd[0] = (uint8_t) (lpi->ps_thd_set + 1) & 0xFF;
			cmd[1] = (uint8_t) ((lpi->ps_thd_set + 1) >> 8) & 0xFF;
		} else {
			cmd[0] = (uint8_t) (lpi->ps_thd_set + lpi->ps_thh_diff) & 0xFF;
			cmd[1] = (uint8_t) ((lpi->ps_thd_set + lpi->ps_thh_diff) >> 8) & 0xFF;
		}
		_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_high, cmd, 3);

		D("[PS][cm36686] SET THD1: lpi->ps_thd_set = %d, lpi->ps_thh_diff= %d avg_min_adc = %d\n",
				lpi->ps_thd_set, lpi->ps_thh_diff, avg_min_adc);
	}
	if (!lpi->ps_stop_polling) {
		queue_delayed_work(lpi->lp_wq, &polling_work,
				msecs_to_jiffies(POLLING_DELAY));
	}
}

static irqreturn_t cm36686_irq_handler(int irq, void *data)
{
	struct cm36686_info *lpi = data;

	disable_irq_nosync(lpi->irq);
	if (enable_cm36686_log)
		D("[PS][cm36686] %s\n", __func__);

	queue_work(lpi->lp_wq, &sensor_irq_work);

	return IRQ_HANDLED;
}

static int als_power(int enable)
{
	struct cm36686_info *lpi = lp_info;

	if (lpi->power)
		lpi->power(LS_PWR_ON, enable);

	return 0;
}

static int cm36686_pinctrl_init(struct cm36686_info *lpi)
{
	int retval;
	struct i2c_client *client = lpi->i2c_client;
	int ret;

	D("cm36686_pinctrl_init");
	/* Get pinctrl if target uses pinctrl */
	lpi->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR_OR_NULL(lpi->pinctrl)) {
		pr_err("[PS][cm36686 error]%s: Target does not use pinctrl\n", __func__);
		retval = PTR_ERR(lpi->pinctrl);
		lpi->pinctrl = NULL;
		return retval;
	}

	lpi->gpio_state_init = pinctrl_lookup_state(lpi->pinctrl, "cm36686_ps_init");
	if (IS_ERR_OR_NULL(lpi->gpio_state_init)) {
		pr_err("[PS][cm36686 error]%s: Cannot get pintctrl state\n", __func__);
		retval = PTR_ERR(lpi->gpio_state_init);
		lpi->pinctrl = NULL;
		return retval;
	}

	ret = pinctrl_select_state(lpi->pinctrl, lpi->gpio_state_init);
	if (ret) {
		pr_err("[PS][cm36686 error]%s: Cannot init INT gpio\n", __func__);
		return ret;
	}

	return 0;
}

static void ls_initial_cmd(struct cm36686_info *lpi)
{
	char cmd[3] = {0};

	cmd[0] = (lpi->ls_cmd | CM36686_ALS_SD);
	cmd[1] = 0;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address,
			ALS_config_cmd, cmd, 3);

	_cm36686_I2C_Read2(lpi->cm36686_slave_address, ALS_config_cmd, cmd, 2);
	D("[LS][cm36686] %s, cmd[0] = 0x%x, cmd[1] = 0x%x\n", __func__, cmd[0], cmd[1]);
}

static void psensor_intelligent_cancel_cmd(struct cm36686_info *lpi)
{
	char cmd[2] = {0};
	cmd[0] = (uint8_t) lpi->inte_ps_canc & 0xFF;
	cmd[1] = (uint8_t) (lpi->inte_ps_canc >> 8) & 0xFF;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_CANC, cmd, 3);
}

static void psensor_initial_cmd(struct cm36686_info *lpi)
{
	char cmd[2] = {0};

	cmd[0] = lpi->ps_conf1_val;
	cmd[1] = lpi->ps_conf2_val;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config, cmd, 3);

	cmd[0] = lpi->ps_conf3_val;
	cmd[1] = lpi->ps_ms_val;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config_ms, cmd, 3);

	cmd[0] = (uint8_t) lpi->ps_thd_set & 0xFF;
	cmd[1] = (uint8_t) (lpi->ps_thd_set >> 8) & 0xFF;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_low, cmd, 3);

	if (lpi->ps_thh_diff <= 0) {
		cmd[0] = (uint8_t) (lpi->ps_thd_set + 1) & 0xFF;
		cmd[1] = (uint8_t) ((lpi->ps_thd_set + 1) >> 8) & 0xFF;
	} else {
		cmd[0] = (uint8_t) (lpi->ps_thd_set + lpi->ps_thh_diff) & 0xFF;
		cmd[1] = (uint8_t) ((lpi->ps_thd_set + lpi->ps_thh_diff) >> 8) & 0xFF;
	}
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_high, cmd, 3);

	psensor_intelligent_cancel_cmd(lpi);
	D("[PS][cm36686] %s, finish\n", __func__);
}

static int psensor_enable(struct cm36686_info *lpi)
{
	int ret;
	char ps_conf[4];
	char cmd[2];
	uint16_t ps_adc = 0;
	int index = 0;
	mutex_lock(&ps_enable_mutex);
	p_status = 1;
	D("[PS][cm36686] %s lpi->dynamical_threshold :%d,lpi->mfg_mode:%d",
			__func__, lpi->dynamical_threshold, lpi->mfg_mode);

	if (lpi->ps_enable) {
		D("[PS][cm36686] %s: already enabled %d\n", __func__, lpi->ps_enable);
		lpi->ps_enable++;
		report_psensor_input_event(lpi, 0);
		mutex_unlock(&ps_enable_mutex);
		return 0;
	}
	sensor_lpm_power(0);
//	blocking_notifier_call_chain(&psensor_notifier_list, 1 + oncall, NULL);/*FIXME*/
	lpi->j_start = jiffies;
	/*D("%s: j_start = %lu", __func__, lpi->j_start);*/

	/* dummy report */
	input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, -1);
	input_sync(lpi->ps_input_dev);

	psensor_initial_cmd(lpi);
	/*======== dynamic threshold adjustment ====*/

	if (lpi->dynamical_threshold == 1 && lpi->mfg_mode != MFG_MODE &&
			pocket_mode_flag != 1 && psensor_enable_by_touch != 1 ) {
		// default report FAR
		D("[PS][cm36686] default report FAR ");
		input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, 1);
		input_sync(lpi->ps_input_dev);
//		blocking_notifier_call_chain(&psensor_notifier_list, 1 + 2 + oncall, NULL);/*FIXME*/
	} else
		report_psensor_input_event(lpi, 0);
	/*======== dynamic threshold adjustment ====*/

	cmd[0] = lpi->ps_conf1_val | CM36686_PS_SD;
	cmd[1] = lpi->ps_conf2_val;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address,
			PS_config, cmd, 3);

	psensor_intelligent_cancel_cmd(lpi);

	ps_conf[0] = lpi->ps_conf1_val;
	ps_conf[1] = lpi->ps_conf2_val | CM36686_PS_INT_BOTH;
	ps_conf[2] = lpi->ps_conf3_val;
	ps_conf[3] = lpi->ps_ms_val;

	enable_ps_interrupt(ps_conf);

	ret = irq_set_irq_wake(lpi->irq, 1);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: fail to enable irq %d as wake interrupt\n", __func__, lpi->irq);
		mutex_unlock(&ps_enable_mutex);
		return ret;
	}
	/*======== dynamic threshold adjustment ====*/
	if (lpi->dynamical_threshold == 1 && lpi->mfg_mode != MFG_MODE &&
			pocket_mode_flag != 1 && psensor_enable_by_touch != 1 ) {

		msleep(40);
		ret = get_stable_ps_adc_value(&ps_adc);
		while (index <= 10 && ps_adc == 0) {
			D("[PS][cm36686]ps_adca = 0 retry");
			ret = get_stable_ps_adc_value(&ps_adc);
			if (ps_adc != 0) {
				D("[PS][cm36686]retry work");
				break;
			}
			mdelay(1);
			index++;
		}

		D("[PS][cm36686] INITIAL ps_adc = 0x%02X\n", ps_adc);
		min_adc = PS_max;

		lpi->ps_base_index = lpi->mapping_size - 1;
		if (ret == 0 && lpi->mapping_table != NULL ){
			queue_delayed_work(lpi->lp_wq, &polling_work,
					msecs_to_jiffies(POLLING_DELAY));
		}
	}
	/*======== dynamic threshold adjustment ====*/
	mutex_unlock(&ps_enable_mutex);
	D("[PS][cm36686] %s -\n", __func__);
	return ret;
}

static int psensor_disable(struct cm36686_info *lpi)
{
	int ret = -EIO;
	int i;
	char cmd[2];
	mutex_lock(&ps_enable_mutex);

	D("[PS][cm36686] %s %d\n", __func__, lpi->ps_enable);
	if (lpi->ps_enable != 1) {
		if (lpi->ps_enable > 1)
			lpi->ps_enable--;
		else
			D("[PS][cm36686] %s: already disabled\n", __func__);
		mutex_unlock(&ps_enable_mutex);
		return 0;
	}
	lpi->ps_conf1_val = lpi->ps_conf1_val_from_board;
	lpi->ps_conf2_val = lpi->ps_conf2_val_from_board;
	lpi->ps_pocket_mode = 0;
	ret = irq_set_irq_wake(lpi->irq, 0);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: fail to disable irq %d as wake interrupt\n", __func__, lpi->irq);
		mutex_unlock(&ps_enable_mutex);
		return ret;
	}

	cmd[0] = lpi->ps_conf1_val | CM36686_PS_SD;
	cmd[1] = lpi->ps_conf2_val;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config, cmd, 3);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: disable psensor fail\n", __func__);
		mutex_unlock(&ps_enable_mutex);
		return ret;
	}

	cmd[0] = lpi->ps_conf3_val;
	cmd[1] = lpi->ps_ms_val;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config_ms, cmd, 3);

//	blocking_notifier_call_chain(&psensor_notifier_list, 0 + oncall, NULL);/*FIXME*/
	lpi->ps_enable = 0;

	if (lpi->dynamical_threshold == 1 && lpi->mfg_mode != MFG_MODE && pocket_mode_flag != 1 && psensor_enable_by_touch != 1 ) {
		cancel_delayed_work(&polling_work);
		if ((call_count >= 2) && (record_adc[5] < Max_open_value)) {
			for (i=0;i<5;i++)
				record_adc[i] = record_adc[i+1];
		}
		D("[PS][cm36686] %s: record_adc[0-4]: %d, %d, %d, %d, %d\n", __func__, record_adc[0], record_adc[1], record_adc[2], record_adc[3], record_adc[4]);

		min_adc = PS_max;

		lpi->ps_base_index = (lpi->mapping_size - 1);
		if (lpi->ps_thd_set > Max_open_value) {
			lpi->ps_thd_set = lpi->original_ps_thd_set;

			cmd[0] = (uint8_t) lpi->ps_thd_set & 0xFF;
			cmd[1] = (uint8_t) (lpi->ps_thd_set >> 8) & 0xFF;
			_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_low, cmd, 3);

			if (lpi->ps_thh_diff == 0) {
				cmd[0] = (uint8_t) (lpi->ps_thd_set + 1) & 0xFF;
				cmd[1] = (uint8_t) ((lpi->ps_thd_set + 1) >> 8) & 0xFF;
			} else {
				cmd[0] = (uint8_t) (lpi->ps_thd_set + lpi->ps_thh_diff) & 0xFF;
				cmd[1] = (uint8_t) ((lpi->ps_thd_set + lpi->ps_thh_diff) >> 8) & 0xFF;
			}
			_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_thd_high, cmd, 3);

			D("[PS][cm36686] %s: restore lpi->ps_thd_set = %d \n", __func__, lpi->ps_thd_set);
		}
	}
	p_status = 9;
	mutex_unlock(&ps_enable_mutex);
	D("[PS][cm36686] %s --%d\n", __func__, lpi->ps_enable);
	return ret;
}

static int psensor_open(struct inode *inode, struct file *file)
{
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686] %s, calibration:%d\n", __func__, psensor_cali);

	if (lpi->psensor_opened)
		return -EBUSY;

	lpi->psensor_opened = 1;
	psensor_disable(lpi);
	return 0;
}

static int psensor_release(struct inode *inode, struct file *file)
{
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686] %s\n", __func__);
	phone_status = 0;
	lpi->psensor_opened = 0;

	return ps_hal_enable ? psensor_disable(lpi) : 0 ;
}

static long psensor_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int val, err;
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686] %s cmd %d\n", __func__, _IOC_NR(cmd));

	switch (cmd) {
		case CAPELLA_CM3602_IOCTL_ENABLE:
			if (get_user(val, (unsigned long __user *)arg))
				return -EFAULT;
			if (val) {
				err = psensor_enable(lpi);
				if (!err)
					ps_hal_enable = 1;
				return err;
			} else {
				err = psensor_disable(lpi);
				if (!err)
					ps_hal_enable = 0;
				return err;
			}
			break;
		case CAPELLA_CM3602_IOCTL_GET_ENABLED:
			return put_user(lpi->ps_enable, (unsigned long __user *)arg);
			break;
		default:
			pr_err("[PS][cm36686 error]%s: invalid cmd %d\n",
					__func__, _IOC_NR(cmd));
			return -EINVAL;
	}
}

static const struct file_operations psensor_fops = {
	.owner = THIS_MODULE,
	.open = psensor_open,
	.release = psensor_release,
	.unlocked_ioctl = psensor_ioctl
};

static struct miscdevice psensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "cm3602",
	.fops = &psensor_fops
};

static void lightsensor_set_kvalue(struct cm36686_info *lpi)
{
	if (!lpi) {
		pr_err("[LS][cm36686 error]%s: ls_info is empty\n", __func__);
		return;
	}

	D("[LS][cm36686] %s: ALS calibrated als_kadc=0x%x\n", __func__, lpi->emmc_als_kadc);

	if (lpi->emmc_als_kadc >> 16 == ALS_CALIBRATED) {
		lpi->als_kadc = lpi->emmc_als_kadc & 0xFFFF;
		lightsensor_cali = 1;
	} else {
		lpi->als_kadc = 0;
		lightsensor_cali = 0;
		D("[LS][cm36686] %s: no ALS calibrated\n", __func__);
	}
	current_lightsensor_kadc = lpi->als_kadc;
	if (lpi->als_kadc && lpi->golden_adc > 0) {
		lpi->als_kadc = (lpi->als_kadc > 0) ? lpi->als_kadc : lpi->golden_adc;
		lpi->als_gadc = lpi->golden_adc;
	} else {
		lpi->als_kadc = 1;
		lpi->als_gadc = 1;
	}
	D("[LS][cm36686] %s: als_kadc=0x%x, als_gadc=0x%x\n", __func__, lpi->als_kadc, lpi->als_gadc);
}

static void psensor_set_kvalue(struct cm36686_info *lpi)
{
	uint8_t ps_conf1_val;

	D("[PS][cm36686] %s: PS calibrated ps_kparam1 = 0x%04X, "
			"ps_kparam2 = 0x%04X\n", __func__, lpi->ps_kparam1, lpi->ps_kparam2);
	ps_conf1_val = lpi->ps_conf1_val;
	/* Only use ps_kparam2 for CM36282/CM36686 */
	if (lpi->ps_kparam1 >> 16 == PS_CALIBRATED) {
		psensor_cali = 1;
		lpi->inte_ps_canc = (uint16_t) (lpi->ps_kparam2 & 0xFFFF);
		lpi->mfg_thd = (uint16_t) ((lpi->ps_kparam2 >> 16) & 0xFFFF);

		lpi->ps_thd_set = lpi->mfg_thd;

		D("[PS][cm36686] %s: PS calibrated inte_ps_canc = 0x%02X, "
				"mfg_thd = 0x%02X, ((ps_kparam2 >> 16) & 0xFF) = 0x%X\n", __func__,
				lpi->inte_ps_canc, mfg_thd, ((lpi->ps_kparam2 >> 16) & 0xFF));
	} else {
		psensor_cali = 0;
		lpi->ps_thd_set = lpi->ps_thd_no_cal;
		D("[PS][cm36686] %s: PS_THD=%d, no calibration\n", __func__, lpi->ps_thd_set);
		D("[PS][cm36686] %s: Proximity NOT calibrated\n", __func__);
	}

}

static int lightsensor_update_table(struct cm36686_info *lpi)
{
	uint16_t data[10];
	int i;
	for (i = 0; i < 10; i++) {
		if (*(lpi->adc_table + i) < 0xFFFF) {
			data[i] = *(lpi->adc_table + i)
				* lpi->als_kadc / lpi->als_gadc;
		} else {
			data[i] = *(lpi->adc_table + i);
		}
		D("[LS][cm36686] %s: Calibrated adc_table: data[%d], %x\n", __func__, i, data[i]);
	}
	memcpy(lpi->cali_table, data, 20);
	return 0;
}

static void lightsensor_enable_work_func(struct work_struct *work)
{
	struct cm36686_info *lpi = lp_info;
	int ret = 0;
	char cmd[3] = {0};

	mutex_lock(&als_enable_mutex);
	sensor_lpm_power(0);
	D("[LS][cm36686] %s\n", __func__);

	cmd[0] = (CM36686_ALS_IT_80ms | CM36686_ALS_PERS_1);/* disable CM36686_ALS_INT_EN */
	cmd[1] = 0;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address,
			ALS_config_cmd, cmd, 3);
	if (ret < 0)
		pr_err("[LS][cm36686 error]%s: set auto light sensor fail\n", __func__);
	else {
		if (lpi->mfg_mode != MFG_MODE)
			msleep(160);
		else
			msleep(85);

		input_report_abs(lpi->ls_input_dev, ABS_MISC, -1);
		input_sync(lpi->ls_input_dev);
		report_lsensor_input_event(lpi, 1);
	}

	mutex_unlock(&als_enable_mutex);
}

static int lightsensor_enable(struct cm36686_info *lpi)
{
	D("[LS][cm36686] %s++\n", __func__);
	schedule_work(&lpi->lightsensor_enable_work);
	D("[LS][cm36686] %s--\n", __func__);

	return 0;
}

static int lightsensor_disable(struct cm36686_info *lpi)
{
	int ret = 0;
	char cmd[3] = {0};
	mutex_lock(&als_disable_mutex);

	D("[LS][cm36686] %s\n", __func__);

	cmd[0] = lpi->ls_cmd | CM36686_ALS_SD;
	cmd[1] = 0;
	ret = _cm36686_I2C_Write2(lpi->cm36686_slave_address,
			ALS_config_cmd, cmd, 3);

	if (ret < 0)
		pr_err("[LS][cm36686 error]%s: disable auto light sensor fail\n", __func__);
	else
		lpi->als_enable = 0;

	mutex_unlock(&als_disable_mutex);
	return ret;
}

static int lightsensor_open(struct inode *inode, struct file *file)
{
	struct cm36686_info *lpi = lp_info;
	int rc = 0;

	D("[LS][cm36686] %s, calibration:%d\n", __func__, lightsensor_cali);
	if (lpi->lightsensor_opened) {
		pr_err("[LS][cm36686 warning]%s: already opened\n", __func__);
	}
	lpi->lightsensor_opened = 1;
	return rc;
}

static int lightsensor_release(struct inode *inode, struct file *file)
{
	struct cm36686_info *lpi = lp_info;

	D("[LS][cm36686] %s\n", __func__);
	lpi->lightsensor_opened = 0;
	return 0;
}

static long lightsensor_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int rc, val;
	struct cm36686_info *lpi = lp_info;

	/*D("[cm36686] %s cmd %d\n", __func__, _IOC_NR(cmd));*/

	switch (cmd) {
		case LIGHTSENSOR_IOCTL_ENABLE:
			if (get_user(val, (unsigned long __user *)arg)) {
				rc = -EFAULT;
				break;
			}
			D("[LS][cm36686] %s LIGHTSENSOR_IOCTL_ENABLE, value = %d\n", __func__, val);
			rc = val ? lightsensor_enable(lpi) : lightsensor_disable(lpi);
			break;
		case LIGHTSENSOR_IOCTL_GET_ENABLED:
			val = lpi->als_enable;
			D("[LS][cm36686] %s LIGHTSENSOR_IOCTL_GET_ENABLED, enabled %d\n", __func__, val);
			rc = put_user(val, (unsigned long __user *)arg);
			break;
		default:
			pr_err("[LS][cm36686 error]%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
			rc = -EINVAL;
	}

	return rc;
}

static const struct file_operations lightsensor_fops = {
	.owner = THIS_MODULE,
	.open = lightsensor_open,
	.release = lightsensor_release,
	.unlocked_ioctl = lightsensor_ioctl
};

static struct miscdevice lightsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &lightsensor_fops
};


static ssize_t ps_adc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	uint16_t ps_adc = 0;
	int ret;
	struct cm36686_info *lpi = lp_info;
	int int_gpio;

	int_gpio = gpio_get_value_cansleep(lpi->intr_pin);
	get_ps_adc_value(&ps_adc);
	D("[PS][cm36686] %s: PS_ADC=0x%04X\n" , __func__, ps_adc);

	ret = sprintf(buf, "ADC[0x%04X], ENABLE = %d, intr_pin = %d, ps_pocket_mode = %d, model = CM36686\n",
			ps_adc, lpi->ps_enable, int_gpio, lpi->ps_pocket_mode);

	return ret;
}

static ssize_t ps_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ps_en, err;
	struct cm36686_info *lpi = lp_info;

	ps_en = -1;
	sscanf(buf, "%d", &ps_en);

	if (ps_en != 0 && ps_en != 1
			&& ps_en != 10 && ps_en != 13 && ps_en != 16)
		return -EINVAL;

	D("[PS][cm36686] %s: ps_en=%d\n", __func__, ps_en);

	if (ps_en && !ps_drv_enable) {
		err = psensor_enable(lpi);
		if (!err)
			ps_drv_enable = 1;
	} else if (!ps_en && ps_drv_enable) {
		ps_drv_enable = 0;
		psensor_disable(lpi);
	}

	return count;
}

static DEVICE_ATTR(ps_adc, 0664, ps_adc_show, ps_enable_store);

static ssize_t ps_flush_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;

	ret = sprintf(buf, "%d\n", 1);

	return ret;
}

static ssize_t ps_flush_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686] %s++:\n", __func__);
	input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, -1);
	input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, 7);
	input_sync(lpi->ps_input_dev);

	return count;
}

static DEVICE_ATTR(flush, 0664, ps_flush_show, ps_flush_store);


static int kcalibrated;
static ssize_t ps_kadc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	int ret = 0;
	struct cm36686_info *lpi = lp_info;

	if ((lpi->ps_kparam1 >> 16 == PS_CALIBRATED) || kcalibrated == 1)
		ret = sprintf(buf, "P-sensor calibrated,"
				"INTE_PS1_CANC = (0x%02X), "
				"INTE_PS2_CANC = (0x%02X)\n",
				lpi->inte_ps_canc, lpi->mfg_thd);
	else
		ret = sprintf(buf, "P-sensor NOT calibrated,"
				"INTE_PS1_CANC = (0x%02X), "
				"INTE_PS2_CANC = (0x%02X)\n",
				lpi->inte_ps_canc, lpi->mfg_thd);

	return ret;
}

static ssize_t ps_kadc_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int param1, param2;
	char ps_conf[4];
	struct cm36686_info *lpi = lp_info;
	uint8_t ps_conf1_val;

	sscanf(buf, "0x%x 0x%x", &param1, &param2);
	D("[PS][cm36686]%s: store value = 0x%X, 0x%X\n", __func__, param1, param2);
	ps_conf1_val = lpi->ps_conf1_val;

	ps_canc_set = lpi->inte_ps_canc = (param2 & 0xFFFF);
	mfg_thd = lpi->mfg_thd = ((param2 >> 16) & 0xFFFF);
	psensor_intelligent_cancel_cmd(lpi);
	lpi->ps_thd_set = mfg_thd;

	if (lpi->ps_enable) {
		ps_conf[0] = lpi->ps_conf1_val;
		ps_conf[1] = lpi->ps_conf2_val | CM36686_PS_INT_BOTH;
		ps_conf[2] = lpi->ps_conf3_val;
		ps_conf[3] = lpi->ps_ms_val;

		enable_ps_interrupt(ps_conf);
	}


	D("[PS]%s: inte_ps_canc = 0x%02X, mfg_thd = 0x%02X, lpi->ps_conf1_val  = 0x%02X\n",
			__func__, lpi->inte_ps_canc, lpi->mfg_thd, lpi->ps_conf1_val);
	kcalibrated = 1;
	return count;
}

static DEVICE_ATTR(ps_kadc, 0664, ps_kadc_show, ps_kadc_store);


static ssize_t ps_canc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;

	ret = sprintf(buf, "PS1_CANC = 0x%02X, PS2_CANC = 0x%02X\n",
			lpi->inte_ps_canc, lpi->mfg_thd);

	return ret;
}
static ssize_t ps_canc_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ps1_canc = 0;
	int ps2_canc = 0;
	struct cm36686_info *lpi = lp_info;

	sscanf(buf, "0x%x 0x%x", &ps1_canc, &ps2_canc);

	lpi->inte_ps_canc = (uint16_t) ps1_canc;
	lpi->mfg_thd = (uint16_t) ps2_canc;
	psensor_intelligent_cancel_cmd(lpi);

	D("[PS] %s: PS1_CANC = 0x%02X, PS2_CANC = 0x%02X\n",
			__func__, lpi->inte_ps_canc, lpi->mfg_thd);

	return count;
}
static DEVICE_ATTR(ps_canc, 0664, ps_canc_show, ps_canc_store);

static ssize_t ps_i2c_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0, i;
	struct cm36686_info *lpi = lp_info;
	uint8_t chip_id[3] = {0};
	uint8_t data[26] = {0};

	for (i = 0; i <= CH_ID; i++) {
		_cm36686_I2C_Read2(lpi->cm36686_slave_address, ALS_config_cmd + i, chip_id, 2);
		data[i*2] = chip_id[0];
		data[(i*2)+1] = chip_id[1];
	}
	ret = sprintf(buf,
			"0x0L=0x%02X, 0x0H=0x%02X, 0x1L=0x%02X, 0x1H=0x%02X, "
			"0x2L=0x%02X, 0x2H=0x%02X, 0x3L=0x%02X, 0x3H=0x%02X,\n"
			"0x4L=0x%02X, 0x4H=0x%02X, 0x5L=0x%02X, 0x5H=0x%02X, "
			"0x6L=0x%02X, 0x6H=0x%02X, 0x7L=0x%02X, 0x7H=0x%02X,\n"
			"0x8L=0x%02X, 0x8H=0x%02X, 0x9L=0x%02X, 0x9H=0x%02X, "
			"0xaL=0x%02X, 0xaH=0x%02X, 0xbL=0x%02X, 0xbH=0x%02X,\n"
			"0xcL=0x%02X, 0xcH=0x%02X.\n",
			data[0], data[1], data[2], data[3],
			data[4], data[5], data[6], data[7],
			data[8], data[9], data[10], data[11],
			data[12], data[13], data[14], data[15],
			data[16], data[17], data[18], data[19],
			data[20], data[21], data[22], data[23],
			data[24], data[25]);

	return ret;
}
static ssize_t ps_i2c_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;
	char *token[10];
	int i, ret = 0;
	uint8_t reg = 0, value[3] = {0}, read_value[3] = {0};
	unsigned long ul_reg = 0, ul_value[3] = {0};

	printk(KERN_INFO "[CM36686_] %s\n", buf);

	for (i = 0; i < 3; i++) {
		token[i] = strsep((char **)&buf, " ");
		D("%s: token[%d] = %s\n", __func__, i, token[i]);
	}

	ret = strict_strtoul(token[0], 16, &ul_reg);
	ret = strict_strtoul(token[1], 16, &(ul_value[0]));
	ret = strict_strtoul(token[2], 16, &(ul_value[1]));

	reg = ul_reg;
	value[0] = ul_value[0];
	value[1] = ul_value[1];

	_cm36686_I2C_Write2(lpi->cm36686_slave_address,
			reg, value, 3);

	D("[CM36686] Set REG=0x%x, value[0]=0x%x, value[1]=0x%x\n",
			reg, value[0], value[1]);

	_cm36686_I2C_Read2(lpi->cm36686_slave_address, reg, read_value, 2);

	D("[CM36686] Get REG=0x%x, value[0]=0x%x, value[1]=0x%x\n",
			reg, read_value[0], read_value[1]);

	if (reg == PS_thd_low)
		lpi->ps_thd_set = (value[1] << 8) | value[0];

	return count;
}
static DEVICE_ATTR(ps_i2c, 0664, ps_i2c_show, ps_i2c_store);

static ssize_t ps_hw_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;

	ret = sprintf(buf, "PS: ps_conf1_val=0x%x, ps_thd_set=0x%x, "
			"inte_ps_canc=0x%02X, mfg_thd=0x%02X, "
			"ps_conf2_val=0x%x, LS: ls_cmd=0x%x\n",
			lpi->ps_conf1_val, lpi->ps_thd_set, lpi->inte_ps_canc,
			lpi->mfg_thd, lpi->ps_conf2_val, lpi->ls_cmd);

	return ret;
}
static ssize_t ps_hw_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int code;
	struct cm36686_info *lpi = lp_info;

	sscanf(buf, "0x%x", &code);

	D("[PS]%s: store value = 0x%x\n", __func__, code);
	if (code == 1) {
		lpi->inte_ps_canc = 0;
		lpi->mfg_thd = 0;
		psensor_intelligent_cancel_cmd(lpi);
		D("[PS]%s: Reset ps1_canc=%d, ps2_canc=%d\n",
				__func__, lpi->inte_ps_canc, lpi->mfg_thd);
	} else {
		lpi->inte_ps_canc = ps_canc_set;
		lpi->mfg_thd = mfg_thd;
		psensor_intelligent_cancel_cmd(lpi);
		D("[PS]%s: Recover ps1_canc=%d, ps2_canc=%d\n", __func__, lpi->inte_ps_canc,
				lpi->mfg_thd);
	}

	return count;
}
static DEVICE_ATTR(ps_hw, 0664, ps_hw_show, ps_hw_store);

static ssize_t ps_headset_bt_plugin_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;

	ret = sprintf(buf, "ps_conf1_val = 0x%02X, ps_conf2_val = 0x%02X\n",
			lpi->ps_conf1_val, lpi->ps_conf2_val);

	return ret;
}
static ssize_t ps_headset_bt_plugin_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int headset_bt_plugin = 0;
	struct cm36686_info *lpi = lp_info;
	char cmd[2] = {0};

	sscanf(buf, "%d", &headset_bt_plugin);
	D("[PS] %s: headset_bt_plugin = %d\n", __func__, headset_bt_plugin);

	if (lpi->no_need_change_setting == 1) {
		D("[PS] %s: no_need_change_setting = 0x%x.\n", __func__, lpi->no_need_change_setting);
		return count;
	} else {
		if (headset_bt_plugin == 1) {
			D("[PS][cm36686] %s, Headset or BT or Speaker ON\n", __func__);

			_cm36686_I2C_Read2(lpi->cm36686_slave_address, PS_config, cmd, 2);
			D("[PS][cm36686] %s, read value => cmd[0] = 0x%x, cmd[1] = 0x%x\n",
					__func__, cmd[0], cmd[1]);

			D("[PS][cm36686] %s, Before setting: ps_conf1_val = 0x%x\n",
					__func__, lpi->ps_conf1_val);
			lpi->ps_conf1_val = (cmd[0] & 0x3) | (CM36686_PS_DR_1_320 |
					CM36686_PS_IT_1_5T |
					CM36686_PS_PERS_1);
			D("[PS][cm36686] %s, After setting: ps_conf1_val = 0x%x\n",
					__func__, lpi->ps_conf1_val);

			D("[PS][cm36686] %s, Before setting: ps_conf2_val = 0x%x\n",
					__func__, lpi->ps_conf2_val);
			lpi->ps_conf2_val = cmd[1] & 0xF;
			D("[PS][cm36686] %s, After setting: ps_conf2_val = 0x%x\n",
					__func__, lpi->ps_conf2_val);

			cmd[0] = lpi->ps_conf1_val;
			cmd[1] = lpi->ps_conf2_val;
			D("[PS][cm36686] %s, write cmd[0] = 0x%x, cmd[1] = 0x%x\n",
					__func__, cmd[0], cmd[1]);
			_cm36686_I2C_Write2(lpi->cm36686_slave_address,
					PS_config, cmd, 3);

			_cm36686_I2C_Read2(lpi->cm36686_slave_address, PS_config, cmd, 2);
			D("[PS][cm36686] %s, read 0x3 cmd value after set =>"
					" cmd[0] = 0x%x, cmd[1] = 0x%x\n",
					__func__, cmd[0], cmd[1]);
		} else {
			D("[PS][cm36686] %s, Headset or BT or Speaker OFF\n", __func__);

			_cm36686_I2C_Read2(lpi->cm36686_slave_address, PS_config, cmd, 2);
			D("[PS][cm36686] %s, read value => cmd[0] = 0x%x, cmd[1] = 0x%x\n",
					__func__, cmd[0], cmd[1]);

			lpi->ps_conf1_val = lpi->ps_conf1_val_from_board;
			lpi->ps_conf2_val = lpi->ps_conf2_val_from_board;

			cmd[0] = ((cmd[0] & 0x3) | lpi->ps_conf1_val);
			cmd[1] = ((cmd[1] & 0xF) | lpi->ps_conf2_val);
			D("[PS][cm36686] %s, write cmd[0] = 0x%x, cmd[1] = 0x%x\n",
					__func__, cmd[0], cmd[1]);
			_cm36686_I2C_Write2(lpi->cm36686_slave_address,
					PS_config, cmd, 3);

			_cm36686_I2C_Read2(lpi->cm36686_slave_address, PS_config, cmd, 2);
			D("[PS][cm36686] %s, read 0x3 cmd value after set =>"
					" cmd[0] = 0x%x, cmd[1] = 0x%x\n",
					__func__, cmd[0], cmd[1]);
		}

	}

	return count;
}
static DEVICE_ATTR(ps_headset_bt_plugin, 0664, ps_headset_bt_plugin_show, ps_headset_bt_plugin_store);

static ssize_t ls_adc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct cm36686_info *lpi = lp_info;

	report_lsensor_input_event(lpi, 0);

	D("[LS][cm36686] %s: ADC = 0x%04X, Level = %d \n", __func__, lpi->current_adc, lpi->current_level);
	ret = sprintf(buf, "ADC[0x%04X] => level %d\n",	lpi->current_adc, lpi->current_level);

	return ret;
}

static DEVICE_ATTR(ls_adc, 0664, ls_adc_show, NULL);

static ssize_t ls_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;

	ret = sprintf(buf, "Light sensor Auto Enable = %d\n", lpi->als_enable);

	return ret;
}

static ssize_t ls_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret = 0;
	int ls_auto;
	struct cm36686_info *lpi = lp_info;

	ls_auto = -1;
	sscanf(buf, "%d", &ls_auto);

	if (ls_auto != 0 && ls_auto != 1 && ls_auto != 147 && ls_auto != 148 && ls_auto != 149) {
		return -EINVAL;
	}
	if (ls_auto) {
		lpi->lightsensor_opened = 1;
		ret = lightsensor_enable(lpi);
	} else {
		lpi->lightsensor_opened = 0;
		ret = lightsensor_disable(lpi);
	}
	lpi->ls_calibrate = (ls_auto == 147) ? 1 : 0;

	D("[LS][cm36686] %s: lpi->als_enable = %d, lpi->ls_calibrate = %d, ls_auto=%d\n",
			__func__, lpi->als_enable, lpi->ls_calibrate, ls_auto);

	if (ret < 0)
		pr_err("[LS][cm36686 error]%s: set auto light sensor fail\n", __func__);

	return count;
}

static DEVICE_ATTR(ls_auto, 0664, ls_enable_show, ls_enable_store);

static ssize_t ls_kadc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cm36686_info *lpi = lp_info;
	int ret;

	ret = sprintf(buf, "kadc = 0x%x, gadc = 0x%x, kadc while this boot = 0x%x\n",
			lpi->als_kadc, lpi->als_gadc, als_kadc);
	return ret;
}

static ssize_t ls_kadc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;
	int kadc_temp = 0;

	sscanf(buf, "%d", &kadc_temp);
	printk(KERN_INFO "[LS]%s: kadc_temp=0x%x \n", __func__, kadc_temp);
	if (kadc_temp <= 0 || lpi->golden_adc <= 0) {
		printk(KERN_ERR "[LS][cm36686 error] %s: kadc_temp=0x%x, als_gadc=0x%x\n",
				__func__, kadc_temp, lpi->golden_adc);
		return -EINVAL;
	}
	mutex_lock(&als_get_adc_mutex);
	lpi->als_kadc = kadc_temp;
	lpi->als_gadc = lpi->golden_adc;
	printk(KERN_INFO "[LS]%s: als_kadc=0x%x, als_gadc=0x%x\n", __func__, lpi->als_kadc, lpi->als_gadc);
	if (lightsensor_update_table(lpi) < 0)
		printk(KERN_ERR "[LS][cm36686 error] %s: update ls table fail\n", __func__);

	mutex_unlock(&als_get_adc_mutex);
	return count;
}

static DEVICE_ATTR(ls_kadc, 0664, ls_kadc_show, ls_kadc_store);

static ssize_t ls_adc_table_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length = 0;
	int i;

	for (i = 0; i < 10; i++) {
		length += sprintf(buf + length,
				"[cm36686]Get adc_table[%d] =  0x%x ; %d, Get cali_table[%d] =  0x%x ; %d, \n",
				i, *(lp_info->adc_table + i),
				*(lp_info->adc_table + i),
				i, *(lp_info->cali_table + i),
				*(lp_info->cali_table + i));
	}
	return length;
}

static ssize_t ls_adc_table_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{

	struct cm36686_info *lpi = lp_info;
	char *token[10];
	unsigned long tempdata[10];
	int i, ret;

	printk(KERN_INFO "[LS][cm36686]%s\n", buf);
	for (i = 0; i < 10; i++) {
		token[i] = strsep((char **)&buf, " ");
		ret = strict_strtoul(token[i], 16, &(tempdata[i]));
		if (tempdata[i] < 1 || tempdata[i] > 0xffff) {
			printk(KERN_ERR "[LS][cm36686 error] adc_table[%d] =  0x%lx Err\n", i, tempdata[i]);
			return count;
		}
	}
	mutex_lock(&als_get_adc_mutex);
	for (i = 0; i < 10; i++) {
		lpi->adc_table[i] = tempdata[i];
		printk(KERN_INFO "[LS][cm36686]Set lpi->adc_table[%d] =  0x%x\n", i, *(lp_info->adc_table + i));
	}
	if (lightsensor_update_table(lpi) < 0)
		printk(KERN_ERR "[LS][cm36686 error] %s: update ls table fail\n", __func__);
	mutex_unlock(&als_get_adc_mutex);
	D("[LS][cm36686] %s\n", __func__);

	return count;
}

static DEVICE_ATTR(ls_adc_table, 0664,
		ls_adc_table_show, ls_adc_table_store);


static ssize_t ls_fLevel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "fLevel = %d\n", f_cm36686_level);
}
static ssize_t ls_fLevel_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;
	int value = 0;
	sscanf(buf, "%d", &value);
	(value >= 0)?(value = min(value, 10)):(value = max(value, -1));
	f_cm36686_level = value;
	input_report_abs(lpi->ls_input_dev, ABS_MISC, f_cm36686_level);
	input_sync(lpi->ls_input_dev);
	printk(KERN_INFO "[LS]set fLevel = %d\n", f_cm36686_level);

	msleep(1000);
	f_cm36686_level = -1;
	return count;
}
static DEVICE_ATTR(ls_flevel, 0664, ls_fLevel_show, ls_fLevel_store);


static ssize_t ps_workaround_table_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cm36686_info *lpi = lp_info;
	int i = 0;
	char table_str[952] = "";
	char temp_str[64] = "";

	sprintf(table_str, "mapping table size = %d\n", lpi->mapping_size);
	printk(KERN_DEBUG "%s: table_str = %s\n", __func__, table_str);
	for (i = 0; i < lpi->mapping_size; i++) {
		memset(temp_str, 0, 64);
		if ((i == 0) || ((i % 10) == 1))
			sprintf(temp_str, "[%d] = 0x%x", i, lpi->mapping_table[i]);
		else
			sprintf(temp_str, ", [%d] = 0x%x", i, lpi->mapping_table[i]);
		strcat(table_str, temp_str);
		printk(KERN_DEBUG "%s: [%d]: table_str = %s\n", __func__, i, table_str);
		if ((i != 0) && (i % 10) == 0)
			strcat(table_str, "\n");
	}

	return sprintf(buf, "%s\n", table_str);
}
static ssize_t ps_workaround_table_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;
	int index = 0;
	unsigned int value = 0;

	sscanf(buf, "%d 0x%x", &index, &value);

	D("%s: input: index = %d, value = 0x%x\n", __func__, index, value);

	if ((index < lpi->mapping_size) && (index >= 0) && (value <= PS_max) && (index >= 0))
		lpi->mapping_table[index] = value;

	if ((index < lpi->mapping_size) && (index >= 0)) {
		printk(KERN_INFO "%s: lpi->mapping_table[%d] = 0x%x, lpi->mapping_size = %d\n",
				__func__, index, lpi->mapping_table[index], lpi->mapping_size);
	}

	return count;
}
static DEVICE_ATTR(ps_workaround_table, 0664, ps_workaround_table_show, ps_workaround_table_store);


static ssize_t ps_fixed_thd_add_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cm36686_info *lpi = lp_info;

	return sprintf(buf, "Fixed added threshold = %d\n", lpi->ps_th_add);
}
static ssize_t ps_fixed_thd_add_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;
	int value = 0;

	sscanf(buf, "%d", &value);

	D("%s: input: value = %d\n", __func__, value);

	if ((value >= 0) && (value <= PS_max))
		lpi->ps_th_add = value;

	D("%s: lpi->ps_th_add = %d\n", __func__, lpi->ps_th_add);

	return count;
}
static DEVICE_ATTR(ps_fixed_thd_add, 0664, ps_fixed_thd_add_show, ps_fixed_thd_add_store);

static ssize_t p_status_show(struct device *dev,
               struct device_attribute *attr, char *buf)
{
       return sprintf(buf,"%d\n",p_status);
}
static DEVICE_ATTR(p_status, 0444, p_status_show, NULL);
/*======== dynamic threshold adjustment ====*/
static ssize_t phone_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = sprintf(buf, "phone_status = %d\n", phone_status);

	return ret;
}
static ssize_t phone_status_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int phone_status1 = 0;
	struct cm36686_info *lpi = lp_info;

	sscanf(buf, "%d" , &phone_status1);

	phone_status = phone_status1;
	/*
phone_status:
call end = 0
call out = 1
call connect = 2
call in = 3
	 */
	D("[PS][cm36686] %s: phone_status = %d\n", __func__, phone_status);
	/*	if (phone_status == 0)
		oncall = 0;
		else
		oncall = 4;*/


	if ((phone_status == 1 || phone_status == 3) && (call_count < 2))
		call_count++;

	if (phone_status == 1 || phone_status == 2) {  // reset polling index
		min_adc = PS_max;
		lpi->ps_base_index = (lpi->mapping_size - 1);
	}

	return count;
}
static DEVICE_ATTR(PhoneApp_status, 0666, phone_status_show, phone_status_store);
/*======== dynamic threshold adjustment ====*/

static ssize_t ls_dark_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36686_info *lpi = lp_info;

	ret = sprintf(buf, "LS_dark_level = %d\n", lpi->dark_level);

	return ret;
}
static ssize_t ls_dark_level_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ls_dark_level = 0;
	struct cm36686_info *lpi = lp_info;

	sscanf(buf, "%d" , &ls_dark_level);

	lpi->dark_level = (uint8_t) ls_dark_level;

	D("[LS] %s: LS_dark_level = %d\n", __func__, lpi->dark_level);

	return count;
}
static DEVICE_ATTR(ls_dark_level, 0664, ls_dark_level_show, ls_dark_level_store);

static ssize_t ls_leveltolux_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
	struct cm36686_info *lpi = lp_info;
        size_t count = 0;
        int i;

        for (i = 0; i < 10; i++) {
                count += snprintf(buf + count, PAGE_SIZE, "%u ", lpi->lux_table[i]);
        }
        return count;
}
static DEVICE_ATTR(ls_leveltolux, 0444, ls_leveltolux_show, NULL);

static ssize_t ls_flush_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;

	ret = sprintf(buf, "%d\n", 1);

	return ret;
}

static ssize_t ls_flush_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cm36686_info *lpi = lp_info;

	D("[LS][cm36686] %s++:\n", __func__);
	input_report_abs(lpi->ls_input_dev, ABS_MISC, -1);
	input_report_abs(lpi->ls_input_dev, ABS_MISC, 77);
	input_sync(lpi->ls_input_dev);

	return count;
}

static DEVICE_ATTR(ls_flush, 0664, ls_flush_show, ls_flush_store);

static int lightsensor_setup(struct cm36686_info *lpi)
{
	int ret;

	lpi->ls_input_dev = input_allocate_device();
	if (!lpi->ls_input_dev) {
		pr_err("[LS][cm36686 error]%s: could not allocate ls input device\n", __func__);
		return -ENOMEM;
	}
	lpi->ls_input_dev->name = "lightsensor-level";
	set_bit(EV_ABS, lpi->ls_input_dev->evbit);
	input_set_abs_params(lpi->ls_input_dev, ABS_MISC, 0, 9, 0, 0);

	ret = input_register_device(lpi->ls_input_dev);
	if (ret < 0) {
		pr_err("[LS][cm36686 error]%s: can not register ls input device\n", __func__);
		goto err_free_ls_input_device;
	}

	ret = misc_register(&lightsensor_misc);
	if (ret < 0) {
		pr_err("[LS][cm36686 error]%s: can not register ls misc device\n", __func__);
		goto err_unregister_ls_input_device;
	}

	return ret;

err_unregister_ls_input_device:
	input_unregister_device(lpi->ls_input_dev);
err_free_ls_input_device:
	input_free_device(lpi->ls_input_dev);
	return ret;
}

static int psensor_setup(struct cm36686_info *lpi)
{
	int ret;

	lpi->ps_input_dev = input_allocate_device();
	if (!lpi->ps_input_dev) {
		pr_err("[PS][cm36686 error]%s: could not allocate ps input device\n", __func__);
		return -ENOMEM;
	}
	lpi->ps_input_dev->name = "proximity";
	set_bit(EV_ABS, lpi->ps_input_dev->evbit);
	input_set_abs_params(lpi->ps_input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	ret = input_register_device(lpi->ps_input_dev);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: could not register ps input device\n", __func__);
		goto err_free_ps_input_device;
	}

	ret = misc_register(&psensor_misc);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: could not register ps misc device\n", __func__);
		goto err_unregister_ps_input_device;
	}

	return ret;

err_unregister_ps_input_device:
	input_unregister_device(lpi->ps_input_dev);
err_free_ps_input_device:
	input_free_device(lpi->ps_input_dev);
	return ret;
}

int power_key_check_in_pocket(void)
{
	struct cm36686_info *lpi = lp_info;
	int ls_dark;
	uint32_t ls_adc = 0;
	int ls_level = 0;
	int i;
	if (!is_probe_success) {
		D("[cm36686] %s return by cm36686 probe fail\n", __func__);
		return 0;
	}
	pocket_mode_flag = 1;
	D("[cm36686] %s +++\n", __func__);
	/* get p-sensor status */
	psensor_enable(lpi);
	D("[cm36686] %s ps_near %d\n", __func__, ps_near);
	psensor_disable(lpi);

	/* get light sensor status */
	mutex_lock(&als_get_adc_mutex);
	get_ls_adc_value(&ls_adc, 0);
	enable_als_interrupt();
	mutex_unlock(&als_get_adc_mutex);
	for (i = 0; i < 10; i++) {
		if (ls_adc <= (*(lpi->adc_table + i))) {
			ls_level = i;
			if (*(lpi->adc_table + i))
				break;
		}
		if (i == 9) {		/*avoid  i = 10, because 'cali_table' of size is 10 */
			ls_level = i;
			break;
		}
	}
	D("[cm36686] %s ls_adc %d, ls_level %d\n", __func__, ls_adc, ls_level);
	ls_dark = (ls_level <= lpi->dark_level) ? 1 : 0;

	D("[cm36686] %s --- ls_dark %d\n", __func__, ls_dark);
	pocket_mode_flag = 0;
	return (ls_dark && ps_near);
}

int psensor_enable_by_touch_driver(int on)
{
	struct cm36686_info *lpi = lp_info;

	if (!is_probe_success) {
		D("[PS][cm36686] %s return by cm36686 probe fail\n", __func__);
		return 0;
	}
	psensor_enable_by_touch = 1;

	D("[PS][cm36686] %s on:%d\n", __func__, on);
	if (on) {
		psensor_enable(lpi);
	} else {
		psensor_disable(lpi);
	}

	psensor_enable_by_touch = 0;
	return 0;
}
static int cm36686_read_chip_id(struct cm36686_info *lpi)
{
	uint8_t chip_id[3] = {0};
	int ret = 0;

	als_power(0);
	msleep(5);
	als_power(1);
	msleep(5);

	ret = _cm36686_I2C_Read2(lpi->cm36686_slave_address, CH_ID, chip_id, 2);
	if (ret >= 0) {
		if ((chip_id[0] != 0x29) && (chip_id[0] != 0x92) 
				&& (chip_id[0] != 0x82) && (chip_id[0] != 0x83) && (chip_id[0] != 0x86)) {
			ret = -1;
			D("[PS][cm36686] %s, chip_id  Err value = 0x%x, 0x%x, ret %d\n",
					__func__, chip_id[0], chip_id[1], ret);
		} else
			D("[PS][cm36686] %s, chip_id value = 0x%x, 0x%x, ret %d\n",
					__func__, chip_id[0], chip_id[1], ret);
	} else
		D("[PS][cm36686] %s, read chip_id i2c err ret %d\n",
				__func__, ret);
	sensor_chipId[0] = chip_id[0];

	return ret;
}

static int cm36686_setup(struct cm36686_info *lpi)
{
	int ret = 0;
	char cmd[3] = {0};

	ret = gpio_request(lpi->intr_pin, "gpio_cm36686_intr");
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: gpio %d request failed (%d)\n",
				__func__, lpi->intr_pin, ret);
		return ret;
	}

	ret = gpio_direction_input(lpi->intr_pin);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: fail to set gpio %d as input (%d)\n", __func__, lpi->intr_pin, ret);
		goto fail_free_intr_pin;
	}

	cm36686_pinctrl_init(lpi);

	ls_initial_cmd(lpi);
	psensor_initial_cmd(lpi);

	/* Disable P-sensor by default */
	cmd[0] = lpi->ps_conf1_val | CM36686_PS_SD;
	cmd[1] = lpi->ps_conf2_val;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config, cmd, 3);

	cmd[0] = lpi->ps_conf3_val;
	cmd[1] = lpi->ps_ms_val;
	_cm36686_I2C_Write2(lpi->cm36686_slave_address, PS_config_ms, cmd, 3);

	//	ret = request_any_context_irq(lpi->irq,
	ret = request_irq(lpi->irq,
			cm36686_irq_handler,
			IRQF_TRIGGER_LOW,
			"cm36686",
			lpi);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: req_irq(%d) fail for gpio %d (%d)\n",
				__func__, lpi->irq, lpi->intr_pin, ret);
		goto fail_free_intr_pin;
	}

	return ret;

fail_free_intr_pin:
	gpio_free(lpi->intr_pin);
	return ret;
}

static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct cm36686_info *lpi=
		container_of(self, struct cm36686_info, fb_notif);

	pr_info("[PS][cm36686] %s\n", __func__);
	if (evdata && evdata->data && event == FB_EVENT_BLANK && lpi && lpi->i2c_client) {
		blank = evdata->data;
		switch (*blank) {
			case FB_BLANK_UNBLANK:
				sensor_lpm_power(0);
				break;
			case FB_BLANK_POWERDOWN:
			case FB_BLANK_HSYNC_SUSPEND:
			case FB_BLANK_VSYNC_SUSPEND:
			case FB_BLANK_NORMAL:
				if (lpi->ps_enable == 0)
					sensor_lpm_power(1);
				else
					D("[PS][cm36686] %s: Psensor enable, so did not enter lpm\n", __func__);
				break;
		}
	}

	return 0;
}

static int cm36686_ldo_init(int init)
{
	int rc = 0;
	struct cm36686_info *lpi = lp_info;
	if (lpi == NULL) {
		pr_err("[PS][cm36686] %s: lpi == NULL\n", __func__);
		return -1;
	}
	if (!init) {
		//per HW request, correct sr_2v85 changes to 3v for A11
		if(lpi->SR_3v_used)
			regulator_set_voltage(lpi->sr_2v85, 0, 3000000);
		else
			regulator_set_voltage(lpi->sr_2v85, 0, 2850000);
		return 0;
	}
	if (lp_info->use__PS2v85) {
		lpi->sr_2v85 = devm_regulator_get(&lpi->i2c_client->dev, "PS_2v85");
		if (IS_ERR(lpi->sr_2v85)) {
			lpi->sr_2v85 = NULL;
			pr_err("[PS][cm36686] %s: Unable to get sr 2v85\n", __func__);
			return PTR_ERR(lpi->sr_2v85);
		}
	} else {
		lpi->sr_2v85 = devm_regulator_get(&lpi->i2c_client->dev, "SR_2v85");
		if (IS_ERR(lpi->sr_2v85)) {
			lpi->sr_2v85 = NULL;
			pr_err("[PS][cm36686] %s: Unable to get sr 2v85\n", __func__);
			return PTR_ERR(lpi->sr_2v85);
		}
	}

	D("[PS][cm36686] %s: lpi->sr_2v85 = 0x%p\n", __func__, lpi->sr_2v85);
	//per HW request, correct sr_2v85 changes to 3v for A11
	if(lpi->SR_3v_used)
		rc = regulator_set_voltage(lpi->sr_2v85, 3000000, 3000000);
	else
		rc = regulator_set_voltage(lpi->sr_2v85, 2850000, 2850000);
	if (rc) {
		pr_err("[PS][cm36686] %s: unable to set voltage for sr 2v85\n", __func__);
		return rc;
	}
	return 0;
}

static int cm36686_sr_lpm(int on)
{
	int rc = 0;
	struct cm36686_info *lpi = lp_info;
	D("[PS][cm36686] %s++: vreg (%s)\n", __func__, on ? "LPM" : "HPM");
	if (lpi->sr_2v85 == NULL) {
		D("[PS][cm36686] %s: Regulator not available, return!!\n", __func__);
		return 0;
	}
	if (on) {
		rc = regulator_set_optimum_mode(lpi->sr_2v85, 100);
		if (rc < 0)
			pr_err("[PS][cm36686] Unable to set LPM of regulator sr_2v85\n");
		rc = regulator_enable(lpi->sr_2v85);
		if (rc)
			pr_err("[PS][cm36686] Unable to enable sr_2v85 111\n");
		D("[PS][cm36686] %s: Set SR_2v85 to LPM--\n", __func__);
	} else {
		rc = regulator_set_optimum_mode(lpi->sr_2v85, 100000);
		if (rc < 0)
			pr_err("[PS][cm36686] Unable to set HPM of regulator sr_2v85\n");
		rc = regulator_enable(lpi->sr_2v85);
		if (rc)
			pr_err("[PS][cm36686] Unable to enable sr_2v85 222\n");
		D("[PS][cm36686] %s: Set SR_2v85 to HPM--\n", __func__);
	}
	return rc < 0 ? rc : 0;
}

static int cm36686_parse_dt(struct device *dev, struct cm36686_platform_data *pdata)
{
	struct property *prop;
	struct device_node *dt = dev->of_node;
	struct device_node *offset = NULL;
	int cali_size = 0;
	unsigned char *cali_data = NULL;
	int i = 0;
	uint32_t temp = 0;

	D("[PS][cm36686] %s: +\n", __func__);

	prop = of_find_property(dt, "cm36686,use__PS2v85", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,use__PS2v85", &pdata->use__PS2v85);
	}

	prop = of_find_property(dt, "cm36686,SR_3v_used", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,SR_3v_used", &pdata->SR_3v_used);
	}

	prop = of_find_property(dt, "cm36686,levels", NULL);
	if (prop) {
		of_property_read_u32_array(dt, "cm36686,levels", adctable, 10);
		pdata->levels = &adctable[0];
	}

	prop = of_find_property(dt, "cm36686,leveltolux", NULL);
	if (prop) {
		of_property_read_u32_array(dt, "cm36686,leveltolux", luxtable, 10);
	}
	pdata->leveltolux = &luxtable[0];

	prop = of_find_property(dt, "cm36686,correction", NULL);
	if (prop) {
		of_property_read_u32_array(dt, "cm36686,correction", correction_table, 10);
	} else {
		correction_table[0] = 0;
	}
	pdata->correction = &correction_table[0];

	prop = of_find_property(dt, "cm36686,golden_adc", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,golden_adc", &pdata->golden_adc);
	}

	prop = of_find_property(dt, "cm36686,cm36686_slave_address", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,cm36686_slave_address", &pdata->cm36686_slave_address);
	}

	prop = of_find_property(dt, "cm36686,ps1_thd_set", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps1_thd_set", &pdata->ps_thd_set);
	}

	prop = of_find_property(dt, "cm36686,ps1_thd_no_cal", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps1_thd_no_cal", &pdata->ps_thd_no_cal);
	}

	prop = of_find_property(dt, "cm36686,ps_th_add", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps_th_add", &pdata->ps_th_add);
	}

	prop = of_find_property(dt, "cm36686,dynamical_threshold", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,dynamical_threshold", &pdata->dynamical_threshold);
	}

	prop = of_find_property(dt, "cm36686,dark_level", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,dark_level", &pdata->dark_level);
	}

	prop = of_find_property(dt, "cm36686,ps_duty", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps_duty", &temp);
	}
	pdata->ps_conf1_val = 0;
	pdata->ps_conf1_val |= (temp << 6);

	prop = of_find_property(dt, "cm36686,ps_pers", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps_pers", &temp);
	}
	pdata->ps_conf1_val |= (temp << 4);

	prop = of_find_property(dt, "cm36686,ps_it", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps_it", &temp);
	}
	pdata->ps_conf1_val |= (temp << 1);

	prop = of_find_property(dt, "cm36686,ps_hd", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps_hd", &temp);
	}
	pdata->ps_conf2_val = 0;
	pdata->ps_conf2_val |= (temp << 3);
	if (temp == 0)
		PS_max = 0xFFF;
	else if (temp == 1)
		PS_max = 0xFFFF;
	pdata->ps_conf2_val |= CM36686_PS_INT_DIS;

	pdata->ps_conf3_val = 0;
	pdata->ps_conf3_val = CM36686_PS_SMART_PRES_EN | CM36686_PS_AUTO | CM36686_PS_TRIG_NO;

	prop = of_find_property(dt, "cm36686,ps_led_current", NULL);
	if (prop) {
		of_property_read_u32(dt, "cm36686,ps_led_current", &temp);
	}
	pdata->ps_ms_val = CM36686_PS_MS_NORMAL;
	pdata->ps_ms_val |= (temp << 0);
	pdata->emmc_als_kadc = 0;

	if ((offset = of_find_node_by_path(CALIBRATION_DATA_PATH))) {
		cali_data = (unsigned char*) of_get_property(offset, LIGHT_SENSOR_FLASH_DATA, &cali_size);

		D("%s: Light sensor cali_size = %d", __func__, cali_size);
		if (cali_data) {
			for (i = 0; (i < cali_size) && (i < 4); i++) {
				D("cali_data[%d] = %02x ", i, cali_data[i]);
				pdata->emmc_als_kadc |= (cali_data[i] << (i * 8));
			}
		}
	} else
		D("%s: Light sensor calibration data offset not found", __func__);

	pdata->emmc_ps_kadc1 = 0;
	pdata->emmc_ps_kadc2 = 0;

	if ((offset = of_find_node_by_path(CALIBRATION_DATA_PATH))) {
		cali_data = (unsigned char*) of_get_property(offset, PSENSOR_FLASH_DATA, &cali_size);

		D("%s: Psensor cali_size = %d", __func__, cali_size);
		if (cali_data) {
			for (i = 0; (i < cali_size) && (i < 4); i++) {
				D("cali_data[%d] = %02x ", i, cali_data[i]);
				pdata->emmc_ps_kadc1 |= (cali_data[i] << (i * 8));
			}
			for (i = 4; (i < cali_size) && (i < 8); i++) {
				D("cali_data[%d] = %02x ", i, cali_data[i]);
				pdata->emmc_ps_kadc2 |= (cali_data[i] << ((i-4) * 8));
			}
		}
	} else
		D("%s: Psensor calibration data offset not found", __func__);

	pdata->intr = of_get_named_gpio_flags(dt, "cm36686,irq-gpio", 0, &pdata->irq_gpio_flags);
	pdata->lpm_power = cm36686_sr_lpm;
	return 0;
}

static void cm36686_fb_register(struct work_struct *work)
{
	int ret = 0;
	struct cm36686_info *lpi = container_of(work, struct cm36686_info, work_fb.work);
	pr_info("[PS][cm36686] %s in", __func__);
	lpi->fb_notif.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&lpi->fb_notif);
	if (ret)
		pr_err("[PS][cm36686][warning]:Unable to register fb_notifier: %d\n", ret);
}

static int cm36686_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct cm36686_info *lpi;
	struct cm36686_platform_data *pdata;

	D("[PS][cm36686] %s\n", __func__);


	lpi = kzalloc(sizeof(struct cm36686_info), GFP_KERNEL);
	if (!lpi)
		return -ENOMEM;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	if (client->dev.of_node) {
		ret = cm36686_parse_dt(&client->dev, pdata);

	} else {
		pdata = client->dev.platform_data;
		if (!pdata) {
			pr_err("[PS][cm36686 error]%s: Assign platform_data error!!\n",
					__func__);
			ret = -EBUSY;
			goto err_platform_data_null;
		}
	}

	lpi->i2c_client = client;
	client->irq = gpio_to_irq(pdata->intr);
	lpi->irq = client->irq;
	//lpi->mfg_mode = board_mfg_mode(); /*FIXME*/
	i2c_set_clientdata(client, lpi);
	lpi->intr_pin = pdata->intr;
	lpi->adc_table = pdata->levels;
	lpi->lux_table = pdata->leveltolux;
	lpi->golden_adc = pdata->golden_adc;
	lpi->power = pdata->power;
	lpi->lpm_power = pdata->lpm_power;
	lpi->cm36686_slave_address = pdata->cm36686_slave_address;
	lpi->ps_thd_set = pdata->ps_thd_set;
	lpi->ps_thh_diff = pdata->ps_thh_diff;
	lpi->ps_conf1_val = pdata->ps_conf1_val;
	lpi->ps_conf2_val = pdata->ps_conf2_val;
	lpi->ps_conf1_val_from_board = pdata->ps_conf1_val;
	lpi->ps_conf2_val_from_board = pdata->ps_conf2_val;
	lpi->ps_conf3_val = pdata->ps_conf3_val;
	lpi->ps_ms_val = pdata->ps_ms_val;
	lpi->j_start = 0;
	lpi->j_end = 0;
	lpi->mapping_table = cm36686_mapping_table;
	lpi->mapping_size = ARRAY_SIZE(cm36686_mapping_table);
	lpi->ps_base_index = (ARRAY_SIZE(cm36686_mapping_table) - 1);
	lpi->dynamical_threshold = pdata->dynamical_threshold;
	lpi->ps_thd_no_cal = pdata->ps_thd_no_cal;
	lpi->ls_cmd  = pdata->ls_cmd;
	lpi->ps_debounce = pdata->ps_debounce;
	lpi->ps_delay_time = pdata->ps_delay_time;
	lpi->no_need_change_setting = pdata->no_need_change_setting;
	lpi->ps_th_add = (pdata->ps_th_add) ? pdata->ps_th_add : TH_ADD;
	lpi->dark_level = pdata->dark_level;
	lpi->correction_table = pdata->correction;
	lpi->emmc_als_kadc = pdata->emmc_als_kadc;
	lpi->emmc_ps_kadc1 = pdata->emmc_ps_kadc1;
	lpi->emmc_ps_kadc2 = pdata->emmc_ps_kadc2;
	lpi->use__PS2v85 = pdata->use__PS2v85;
	lpi->SR_3v_used = pdata->SR_3v_used;
	lpi->ps_stop_polling = 0;
	lp_info = lpi;

	ret = cm36686_read_chip_id(lpi);
	if (ret < 0) {
		pr_err("[PS_ERR][cm36686 error]%s: cm36686_read_chip_id error!\n", __func__);
		ret = -ENXIO;
		goto err_cm36686_read_chip_id;
	}

	if (pdata->ls_cmd == 0) {
		lpi->ls_cmd  = CM36686_ALS_IT_320ms | CM36686_ALS_PERS_1;
		pr_info("[PS][cm36686]%s: lp_info->ls_cmd = 0x%x!\n", __func__, lp_info->ls_cmd);
	}
	D("[PS][cm36686] %s: ls_cmd 0x%x, ps_debounce=0x%x, ps_thh_diff %d\n",
			__func__, lpi->ls_cmd, lpi->ps_debounce, lpi->ps_thh_diff);

	mutex_init(&als_enable_mutex);
	mutex_init(&als_disable_mutex);
	mutex_init(&als_get_adc_mutex);
	mutex_init(&ps_enable_mutex);

	ps_hal_enable = ps_drv_enable = 0;

	ret = lightsensor_setup(lpi);
	if (ret < 0) {
		pr_err("[LS][cm36686 error]%s: lightsensor_setup error!!\n", __func__);
		goto err_lightsensor_setup;
	}

	ret = psensor_setup(lpi);
	if (ret < 0) {
		pr_err("[PS][cm36686 error]%s: psensor_setup error!!\n", __func__);
		goto err_psensor_setup;
	}
	if (!(client->dev.of_node)) {
		lpi->emmc_als_kadc = als_kadc;
		D("[PS][cm36686] %s: Light sensor use ATAG Calibration data\n", __func__);
	}

	lightsensor_set_kvalue(lpi);
	ret = lightsensor_update_table(lpi);
	if (ret < 0) {
		pr_err("[LS][cm36686 error]%s: update ls table fail\n",
				__func__);
		goto err_lightsensor_update_table;
	}

	lpi->lp_wq = create_singlethread_workqueue("cm36686_wq");
	if (!lpi->lp_wq) {
		pr_err("[PS][cm36686 error]%s: can't create workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_create_singlethread_workqueue;
	}

	wake_lock_init(&(lpi->ps_wake_lock), WAKE_LOCK_SUSPEND, "proximity");

	if (!(client->dev.of_node)) {
		lpi->ps_kparam1 = ps_kparam1;
		lpi->ps_kparam2 = ps_kparam2;
		D("%s: Psensor use ATAG Calibration data\n", __func__);
	} else {
		lpi->ps_kparam1 = lpi->emmc_ps_kadc1;
		lpi->ps_kparam2 = lpi->emmc_ps_kadc2;
		D("%s: Psensor use DT Calibration data\n", __func__);
	}


	psensor_set_kvalue(lpi);
	if (lpi->dynamical_threshold == 1) {
		lpi->ps_thd_set = lpi->ps_thd_set + 50;
		lpi->original_ps_thd_set = lpi->ps_thd_set;
	}
	ret = cm36686_setup(lpi);
	if (ret < 0) {
		pr_err("[PS_ERR][cm36686 error]%s: cm36686_setup error!\n", __func__);
		goto err_cm36686_setup;
	}
	ps_canc_set = lpi->inte_ps_canc;
	mfg_thd = lpi->mfg_thd;
	lpi->cm36686_class = class_create(THIS_MODULE, "optical_sensors");
	if (IS_ERR(lpi->cm36686_class)) {
		ret = PTR_ERR(lpi->cm36686_class);
		lpi->cm36686_class = NULL;
		goto err_create_class;
	}

	lpi->ls_dev = device_create(lpi->cm36686_class, NULL, 0, "%s", "lightsensor");
	if (unlikely(IS_ERR(lpi->ls_dev))) {
		ret = PTR_ERR(lpi->ls_dev);
		lpi->ls_dev = NULL;
		goto err_create_ls_device;
	}

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_adc);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_auto);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_kadc);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_adc_table);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_flevel);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_dark_level);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_leveltolux);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_flush);
	if (ret)
		goto err_create_ls_device_file;

	lpi->ps_dev = device_create(lpi->cm36686_class,	NULL, 0, "%s", "proximity");
	if (unlikely(IS_ERR(lpi->ps_dev))) {
		ret = PTR_ERR(lpi->ps_dev);
		lpi->ps_dev = NULL;
		goto err_create_ls_device_file;
	}

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_adc);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_kadc);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_canc);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_hw);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_i2c);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_headset_bt_plugin);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_workaround_table);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_fixed_thd_add);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_p_status);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_PhoneApp_status);
	if (ret)
		goto err_create_ps_device;

	ret = device_create_file(lpi->ps_dev, &dev_attr_flush);
	if (ret)
		goto err_create_ps_device;

	lpi->cm36686_fb_wq = create_singlethread_workqueue("CM36686_FB");
	if (!lpi->cm36686_fb_wq) {
		pr_err("[PS][cm36686] allocate cm36686_fb_wq failed\n");
		ret = -ENOMEM;
		goto err_create_cm36686_fb_workqueue_failed;
	}
	INIT_WORK(&lpi->lightsensor_enable_work, lightsensor_enable_work_func);
	INIT_DELAYED_WORK(&lpi->work_fb, cm36686_fb_register);
	queue_delayed_work(lpi->cm36686_fb_wq, &lpi->work_fb, msecs_to_jiffies(30000));
	ret = cm36686_ldo_init(1);
	if (ret) {
		pr_err(" [PS][cm36686] Sensor vreg configuration failed\n");
		goto err_lpm_init;
	}
	ret = cm36686_sr_lpm(0);
	if (ret)
		pr_err("[PS][cm36686] %s: cm36686_sr_lpm failed 111\n", __func__);
	ret = cm36686_sr_lpm(1);
	if (ret)
		pr_err("[PS][cm36686] %s: cm36686_sr_lpm failed 222\n", __func__);

	D("[PS][cm36686] %s: Probe success!\n", __func__);
	is_probe_success = 1;
	return ret;
err_lpm_init:
	devm_regulator_put(lpi->sr_2v85);
err_create_cm36686_fb_workqueue_failed:
err_create_ps_device:
	device_unregister(lpi->ps_dev);
err_create_ls_device_file:
	device_unregister(lpi->ls_dev);
err_create_ls_device:
	class_destroy(lpi->cm36686_class);
err_create_class:
err_cm36686_setup:
	destroy_workqueue(lpi->lp_wq);
	wake_lock_destroy(&(lpi->ps_wake_lock));
	input_unregister_device(lpi->ls_input_dev);
	input_free_device(lpi->ls_input_dev);
	input_unregister_device(lpi->ps_input_dev);
	input_free_device(lpi->ps_input_dev);
err_create_singlethread_workqueue:
err_lightsensor_update_table:
	misc_deregister(&psensor_misc);
err_psensor_setup:
	misc_deregister(&lightsensor_misc);
err_lightsensor_setup:
	mutex_destroy(&als_enable_mutex);
	mutex_destroy(&als_disable_mutex);
	mutex_destroy(&als_get_adc_mutex);
err_cm36686_read_chip_id:
err_platform_data_null:
err_alloc_data_failed:
	kfree(lpi);
	return ret;

}
static int cm36686_suspend(struct device *dev)
{
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686] cm36686_suspend\n");
	lpi->ps_stop_polling = 1;
	cancel_delayed_work(&polling_work);
	return 0;
}
static int cm36686_resume(struct device *dev)
{
	struct cm36686_info *lpi = lp_info;

	D("[PS][cm36686] cm36686_resume++\n");
	lpi->ps_stop_polling = 0;
	if (lpi->dynamical_threshold == 1 && lpi->mfg_mode != MFG_MODE
			&& lpi->ps_enable && pocket_mode_flag != 1 && psensor_enable_by_touch != 1) {
		queue_delayed_work(lpi->lp_wq, &polling_work,
				msecs_to_jiffies(POLLING_DELAY));
	}
	D("[PS][cm36686] cm36686_resume--\n");
	return 0;
}

static const struct dev_pm_ops cm36686_pm_ops = {
	.suspend = cm36686_suspend,
	.resume = cm36686_resume
};

static struct i2c_device_id cm36686_i2c_id[] = {
	{"CM36686", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cm36686_i2c_id);

static struct of_device_id cm36686_match_table[] = {
	{.compatible = "CM36686"},
	{},
};

static struct i2c_driver cm36686_driver = {
	.driver = {
		.name = "CM36686",
		.owner = THIS_MODULE,
		.pm = &cm36686_pm_ops,
#ifdef CONFIG_OF
		.of_match_table = cm36686_match_table,
#endif
	},
	.probe = cm36686_probe,
	.id_table = cm36686_i2c_id,

};
module_i2c_driver(cm36686_driver);
#ifndef CONFIG_OF
static int __init cm36686_init(void)
{
	return i2c_add_driver(&cm36686_driver);
}

static void __exit cm36686_exit(void)
{
	i2c_del_driver(&cm36686_driver);
}

module_init(cm36686_init);
module_exit(cm36686_exit);
#endif
MODULE_DESCRIPTION("cm36686 Driver");
MODULE_LICENSE("GPL");
