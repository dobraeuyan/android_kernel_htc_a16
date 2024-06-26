/*
 * Copyright (C) 2011 HTC, Inc.
 * Author: Dyson Lee <Dyson@intel.com>
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

static int manual_serialno_flag = 0;
static char mfg_whiteline_serialno[] = "000000000000";
/*++ 2014/11/25 USB Team, PCN00051 ++*/
/*++ 2015/03/04 USB Team, PCN00070 ++*/
unsigned int usb_project_pid;
/*-- 2015/03/04 USB Team, PCN00070 --*/
/*-- 2014/11/25 USB Team, PCN00051 --*/
/*++ 2015/01/05 USB Team, PCN00063 ++*/
static unsigned int vzw_unmount_cdrom_enable;
/*-- 2015/01/05 USB Team, PCN00063 --*/
/*++ 2014/11/01 USB Team, PCN00036 ++*/
static int usb_disable;
/*-- 2014/11/01 USB Team, PCN00036 --*/

/*++ 2015/05/27, USB Team, Tracfone, PCN00001 ++*/
static unsigned int tracfone_flag;
/*-- 2015/05/27, USB Team, Tracfone, PCN00001 --*/

/*++ 2014/10/17, USB Team, PCN00016 ++*/
void htc_setprop(char *func)
{
	int	call_us_ret = -1;

	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/system/sbin:/system/bin:/system/xbin",
		NULL,
	};
	char *exec_path[1] = {"/system/bin/setprop"};
	char *argv_setprop[] = { exec_path[0], "sys.usb.config", func, NULL,};
	call_us_ret = call_usermodehelper(exec_path[0],
				argv_setprop, envp, UMH_WAIT_PROC);
}
/*-- 2014/10/17, USB Team, PCN00016 --*/

void android_set_serialno(char *serialno)
{
	strings_dev[STRING_SERIAL_IDX].s = serialno;
}

void init_mfg_serialno(void)
{
	if(!strcmp(htc_get_bootmode(),"factory2") || !strcmp(htc_get_bootmode(),"ftm"))
		android_set_serialno(mfg_whiteline_serialno);
	return;
}

/*++ 2014/11/12, USB Team, PCN00040 ++*/
int mfg_check_white_accessory(int accessory_type)
/*-- 2014/11/12, USB Team, PCN00040 --*/
{
	static int previous_type = 0;
	int reset_flag = 0;
/*++ 2014/11/12, USB Team, PCN00040 ++*/
	struct android_dev *dev;
	struct usb_composite_dev *cdev;

	if (manual_serialno_flag || (strcmp(htc_get_bootmode(),"factory2") && strcmp(htc_get_bootmode(),"ftm")))
		return 0;

	if (!_android_dev)
		return -EAGAIN;

	dev = _android_dev;
	cdev = dev->cdev;
/*-- 2014/11/12, USB Team, PCN00040 --*/

	printk("%s : accessory type %d , previous type %d, connected %d\n",__func__,accessory_type,previous_type,dev->connected);
	switch (accessory_type)
	{
		case DOCK_STATE_CAR:
			mfg_whiteline_serialno[11] = '3';
			android_set_serialno(mfg_whiteline_serialno);
			if (previous_type != accessory_type) {
				previous_type = accessory_type;
				reset_flag = 1;
			}
			break;
		case DOCK_STATE_USB_HEADSET:
			mfg_whiteline_serialno[11] = '4';
			android_set_serialno(mfg_whiteline_serialno);
			if (previous_type != accessory_type) {
				previous_type = accessory_type;
				reset_flag = 1;
			}
			break;
		case DOCK_STATE_DMB:
			mfg_whiteline_serialno[11] = '5';
			android_set_serialno(mfg_whiteline_serialno);
			if (previous_type != accessory_type) {
				previous_type = accessory_type;
				reset_flag = 1;
			}
			break;
		default:
			mfg_whiteline_serialno[11] = '0';
			android_set_serialno(mfg_whiteline_serialno);
			if (previous_type != 0) {
				previous_type = 0;
				reset_flag = 1;
			}
			break;
	}
	if (reset_flag) {
		if (dev->connected) {
			schedule_delayed_work(&cdev->request_reset,REQUEST_RESET_DELAYED);
		}
	}
/*++ 2014/11/12, USB Team, PCN00040 ++*/
	return 0;
/*-- 2014/11/12, USB Team, PCN00040 --*/
}

/*++ 2014/11/14, USB Team, PCN00048 ++*/
static void setup_usb_denied(int htc_mode)
{
        if (htc_mode)
                _android_dev->autobot_mode = 1;
        else
                _android_dev->autobot_mode = 0;
}

static int usb_autobot_mode(void)
{
        if (_android_dev->autobot_mode)
                return 1;
        else
                return 0;
}

void android_switch_htc_mode(void)
{
	htc_usb_enable_function("adb,mass_storage,serial,projector", 1);
}
/*-- 2014/11/14, USB Team, PCN00048 --*/

/*++ 2014/10/31, USB Team, PCN00035 ++*/
int htc_usb_enable_function(char *name, int ebl)
{
	struct android_dev *dev = _android_dev;
	struct usb_composite_dev *cdev = dev->cdev;
	char state_buf[60];
	char name_buf[60];
	char *function[3];
	if (!strcmp(name, "ffs"))
		snprintf(state_buf, sizeof(state_buf), "SWITCH_STATE=%s", "adb");
	else
		snprintf(state_buf, sizeof(state_buf), "SWITCH_STATE=%s", name);
	function[0] = state_buf;
	function[2] = NULL;

	if (ebl) {
		snprintf(name_buf, sizeof(name_buf),
				"SWITCH_NAME=%s", "function_switch_on");
		function[1] = name_buf;
		kobject_uevent_env(&cdev->sw_function_switch_on.dev->kobj, KOBJ_CHANGE,
				function);
	} else {
		snprintf(name_buf, sizeof(name_buf),
				"SWITCH_NAME=%s", "function_switch_off");
		function[1] = name_buf;
		kobject_uevent_env(&cdev->sw_function_switch_off.dev->kobj, KOBJ_CHANGE,
				function);
	}
	return 0;

}
/*-- 2014/10/31, USB Team, PCN00035 --*/

/*++ 2015/09/17 USB Team, PCN00020 ++*/
const char * change_charging_to_ums(const char *buff) {
	if (!strcmp(buff, "charging"))
		return "mass_storage";
	else if (!strcmp(buff, "adb"))
		return "mass_storage,adb";
	else if (!strcmp(buff, "ffs"))
		return "mass_storage,ffs";
	return buff;
}

void change_charging_pid_to_ums(struct usb_composite_dev *cdev) {
	switch(cdev->desc.idProduct) {
		case 0x0f0b:
			cdev->desc.idVendor = 0x0bb4;
			cdev->desc.idProduct = 0x0ff9;
			break;
		case 0x0c81:
			cdev->desc.idVendor = 0x0bb4;
			cdev->desc.idProduct = 0x0f86;
			break;
		default:
			break;
	}
	return;
}
/*-- 2015/09/17 USB Team, PCN00020 --*/

/*++ 2014/10/29 USB Team, PCN00029 ++*/
const char * add_usb_radio_debug_function(const char *buff) {
	if (!strcmp(buff, "mtp,adb,mass_storage")) /* 0bb4/0f24 */
		return "adb,diag,modem,rmnet";
	else if (!strcmp(buff, "mtp,adb,mass_storage,acm")) /* 0bb4/0f24 */
		return "adb,diag,modem,rmnet";
	else if (!strcmp(buff, "mtp,mass_storage")) /* 0bb4/0fd9 */
		return "mass_storage,diag,modem,rmnet";
	else if (!strcmp(buff, "mtp,mass_storage,acm")) /* 0bb4/0fd9 */
		return "mass_storage,diag,modem,rmnet";
	else if (!strcmp(buff, "ffs,acm")) /* 0bb4/0f17 */
		return "adb,diag,modem,acm";
	else if (!strcmp(buff, "mtp,adb,mass_storage,diag,modem,rmnet")) /* 0bb4/0f24 */
		return "adb,diag,modem,rmnet";
	/* add new combinations for M60 */
	else if (!strcmp(buff, "mtp,adb")) /* 0bb4/0f24*/
		return "adb,diag,modem,rmnet";
	else if (!strcmp(buff, "mtp")) /* 0bb4/0f12*/
		return "mtp,diag,modem,rmnet";
	else if (!strcmp(buff, "mass_storage,adb")) /* 0bb4/0fd8 */
		return "mass_storage,adb,diag,modem,rmnet";
	else if (!strcmp(buff, "mass_storage")) /* 0bb4/0fd9 */
		return "mass_storage,diag,modem,rmnet";
	else if (!strcmp(buff, "rndis")) /* 0bb4/0f82 */
		return "rndis,diag,modem";
	else if (!strcmp(buff, "rndis,adb")) /* 0bb4/0f83 */
		return "rndis,adb,diag,modem";
	/* end of M60 */
	printk(KERN_INFO "[USB] no match the USB combinations when enable radio flag,");
	printk(KERN_INFO " switch to original function:%s\n", buff);
	return buff;
}

/* Change the PID for radio flag 8 20000 */
void check_usb_vid_pid(struct usb_composite_dev *cdev) {
	switch(cdev->desc.idProduct) {
		/* the pid of new combinations for M60 */
		case 0x0c93:
		case 0x0400:
			cdev->desc.idProduct = 0x0f12;
			break;
		case 0x0ffe:
		case 0x0402:
			cdev->desc.idProduct = 0x0f82;
			break;
		case 0x0ffc:
		case 0x0406:
			cdev->desc.idProduct = 0x0f83;
			break;
		case 0x0ff9:
			cdev->desc.idProduct = 0x0fd9;
			break;
		case 0x0f86:
			cdev->desc.idProduct = 0x0fd8;
			break;
		case 0x0f87:
		/* end of M60 */
		case 0x0f90:
		case 0x0401:
		case 0x0fa2:
		case 0x0f63:
			cdev->desc.idProduct = 0x0f24;
			break;
/*++ 2014/11/14 USB Team, PCN00047 ++*/
		case 0x0f25:
		case 0x0f26:
/*-- 2014/11/14 USB Team, PCN00047 --*/
		case 0x0f91:
		case 0x0fa3:
		case 0x0f64:
			cdev->desc.idProduct = 0x0fd9;
			break;
		case 0x0f15:
			cdev->desc.idProduct = 0x0f17;
		default:
			break;
	}
	cdev->desc.idVendor = 0x0bb4;
	return;
}
/*-- 2014/10/29 USB Team, PCN00029 --*/
/*++ 2014/11/25 USB Team, PCN00051 ++*/
/* Change to project default PID */
void check_usb_project_pid(struct usb_composite_dev *cdev) {
	if (cdev->desc.idProduct == 0x0f90 && usb_project_pid != 0x0000) {
		cdev->desc.idVendor = 0x0bb4;
		cdev->desc.idProduct = usb_project_pid;
	}
	return;
}

static int __init get_usb_project_pid(char *str)
{
	int ret = kstrtouint(str, 0, &usb_project_pid);
	pr_info("androidusb.pid %d: %08x from %26s\r\n",
			ret, usb_project_pid, str);
	return ret;
} early_param("androidusb.pid", get_usb_project_pid);
/*-- 2014/11/25 USB Team, PCN00051 --*/

/*++ 2015/05/27, USB Team, Tracfone, PCN00001 ++*/
unsigned int check_tracfone_flag(void)
{
	return tracfone_flag;
}

static int __init get_tracfone_flag(char *str)
{
	int ret = kstrtouint(str, 0, &tracfone_flag);
	pr_info("androidusb.rndis %d: %08x from %26s\r\n",
			ret, tracfone_flag, str);
	return ret;
} early_param("androidusb.rndis", get_tracfone_flag);
/*-- 2015/05/27, USB Team, Tracfone, PCN00001 --*/

/*++ 2015/01/05 USB Team, PCN00063 ++*/
static int __init get_vzw_unmount_cdrom_enable(char *str)
{
	int ret = kstrtouint(str, 0, &vzw_unmount_cdrom_enable);
	pr_info("vzw_unmount_cdrom_enable %d: %08x from %26s\r\n",
			ret, vzw_unmount_cdrom_enable, str);
	return ret;
} early_param("vzw_unmount_cdrom_enable", get_vzw_unmount_cdrom_enable);
/*-- 2015/01/05 USB Team, PCN00063 --*/

/*++ 2015/01/06 USB Team, PCN00064 ++*/
static int __init get_disk_mode_enable(char *str)
{
	int ret = kstrtouint(str, 0, &disk_mode_enable);
	pr_info("disk_mode_enable %d: %08x from %26s\r\n",
			ret, disk_mode_enable, str);
	return ret;
} early_param("disk_mode_enable", get_disk_mode_enable);
/*-- 2015/01/06 USB Team, PCN00064 --*/

/*++ 2014/11/01 USB Team, PCN00036 ++*/
static ssize_t show_usb_disable_setting(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length;

	length = sprintf(buf, "%d\n", usb_disable);
	return length;
}

void msm_otg_set_disable_usb(int disable_usb_function);
static ssize_t store_usb_disable_setting(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int disable_usb_function;
	ssize_t  ret;

	ret = kstrtouint(buf, 2, &disable_usb_function);
	if (ret < 0) {
		printk(KERN_ERR "[USB] %s: %zu\n", __func__, ret);
		return count;
	}
	printk(KERN_INFO "USB_disable set %d\n", disable_usb_function);
	usb_disable = disable_usb_function;
	msm_otg_set_disable_usb(disable_usb_function);
	return count;
}
/*-- 2014/11/01 USB Team, PCN00036 --*/

static ssize_t iSerial_show(struct device *dev, struct device_attribute *attr,
	char *buf);

/*++ 2015/01/05 USB Team, PCN00063 ++*/
static int cdrom_unmount;
static ssize_t show_cdrom_unmount(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length;

	length = sprintf(buf, "%d\n", cdrom_unmount);
	return length;
}

static ssize_t store_cdrom_unmount(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct usb_composite_dev *cdev = _android_dev->cdev;
	int value;
	sscanf(buf, "%d", &value);
	cdrom_unmount = value;

	if (value == 1 && vzw_unmount_cdrom_enable) {
		printk(KERN_INFO "[USB] Trigger unmount uevent after 30 seconds\n");
		cancel_delayed_work(&cdev->cdusbcmd_vzw_unmount_work);
		cdev->unmount_cdrom_mask = 1 << 3 | 1 << 4;
		schedule_delayed_work(&cdev->cdusbcmd_vzw_unmount_work, 30 * HZ);
	}
	return count;
}
/*-- 2015/01/05 USB Team, PCN00063 --*/

/*++ 2014/11/05, USB Team, PCN00038 ++*/
static ssize_t store_dummy_usb_serial_number(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct android_dev *android_dev = _android_dev;
	struct usb_composite_dev *cdev = android_dev->cdev;
	int loop_i;

	if (size >= sizeof(serial_string)) {
		USB_INFO("%s(): input size > %zu\n",
			__func__, sizeof(serial_string));
		return -EINVAL;
	}

	for (loop_i = 0; loop_i < size; loop_i++) {
		if (buf[loop_i] >= 0x30 && buf[loop_i] <= 0x39) /* 0-9 */
			continue;
		else if (buf[loop_i] >= 0x41 && buf[loop_i] <= 0x5A) /* A-Z */
			continue;
		if (buf[loop_i] == 0x0A) /* Line Feed */
			continue;
		else {
			USB_INFO("%s(): get invaild char (0x%2.2X)\n",
					__func__, buf[loop_i]);
			return -EINVAL;
		}
	}
	strlcpy(serial_string, buf, sizeof(serial_string));
	strim(serial_string);
	android_set_serialno(serial_string);
	if (android_dev->connected)
		schedule_delayed_work(&cdev->request_reset, REQUEST_RESET_DELAYED);
	manual_serialno_flag = 1;
	return size;
}
static const char *os_to_string(int os_type)
{
	switch (os_type) {
	case OS_NOT_YET:	return "OS_NOT_YET";
	case OS_MAC:		return "OS_MAC";
	case OS_LINUX:		return "OS_LINUX";
	case OS_WINDOWS:	return "OS_WINDOWS";
	default:		return "UNKNOWN";
	}
}
/*-- 2014/11/05, USB Team, PCN00038 --*/
/* show current os type for mac or non-mac */
static ssize_t show_os_type(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length;

	length = sprintf(buf, "%d\n", os_type);
	USB_INFO("%s: %s, %s", __func__, os_to_string(os_type), buf);
	return length;
}

/*++ 2014/10/29 USB Team, PCN00032 ++*/
static ssize_t store_ats(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &usb_ats);
	return count;
}

static ssize_t show_ats(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length;

	length = sprintf(buf, "%d\n", (get_debug_flag() & 0x100) || usb_ats);
	USB_INFO("%s: %s\n", __func__, buf);
	return length;
}
/*-- 2014/10/29 USB Team, PCN00032 --*/

/*++ 2014/11/14 USB Team, PCN00048 ++*/
/* Check if USB function is available for user process */
static ssize_t show_is_usb_denied(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        unsigned length;
        int deny = 0;

        if (usb_autobot_mode()) {
                /* In HTC mode, USB function change by
                 * user space should be denied.
                 */
                deny = 1;
        }

        length = sprintf(buf, "%d\n", deny);
        USB_INFO("%s: %s\n", __func__, buf);
        return length;
}
/*-- 2014/11/14 USB Team, PCN00048 --*/

static ssize_t show_usb_ac_cable_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length;
	length = sprintf(buf, "%d",usb_get_connect_type());
	return length;
}

/*++ 2014/11/11 USB Team, PCN00039 ++*/
static ssize_t show_usb_cable_connect(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned length;
	struct android_dev *and_dev = _android_dev;

	length = sprintf(buf, "%d",(and_dev->connected == 1) && !usb_disable ? 1 : 0);
	return length;
}
/*-- 2014/11/11 USB Team, PCN00039 --*/

static ssize_t store_usb_modem_enable_setting(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int usb_modem_enable;
	sscanf(buf, "%d ", &usb_modem_enable);

	USB_INFO("modem: enable %d\n", usb_modem_enable);
	htc_usb_enable_function("modem", usb_modem_enable?1:0);
	return count;
}

static DEVICE_ATTR(usb_modem_enable, 0660,NULL, store_usb_modem_enable_setting);

/*++ 2014/11/01 USB Team, PCN00036 ++*/
static DEVICE_ATTR(usb_disable, 0664,show_usb_disable_setting, store_usb_disable_setting);
/*-- 2014/11/01 USB Team, PCN00036 --*/
static DEVICE_ATTR(usb_ac_cable_status, 0444, show_usb_ac_cable_status, NULL);
/*++ 2014/11/05, USB Team, PCN00038 ++*/
static DEVICE_ATTR(dummy_usb_serial_number, 0644, iSerial_show, store_dummy_usb_serial_number);
/*-- 2014/11/05, USB Team, PCN00038 --*/
static DEVICE_ATTR(os_type, 0444, show_os_type, NULL);
/*++ 2014/10/29 USB Team, PCN00032 ++*/
static DEVICE_ATTR(ats, 0664, show_ats, store_ats);
/*-- 2014/10/29 USB Team, PCN00032 --*/
/*++ 2014/11/11 USB Team, PCN00048 ++*/
static DEVICE_ATTR(usb_denied, 0444, show_is_usb_denied, NULL);
/*-- 2014/11/14 USB Team, PCN00048 --*/
/*++ 2015/01/05 USB Team, PCN00063 ++*/
static DEVICE_ATTR(cdrom_unmount, 0644, show_cdrom_unmount, store_cdrom_unmount);
/*-- 2015/01/05 USB Team, PCN00063 --*/
/*++ 2014/11/11 USB Team, PCN00039 ++*/
static DEVICE_ATTR(usb_cable_connect, 0444, show_usb_cable_connect, NULL);
/*-- 2014/11/11 USB Team, PCN00039 --*/

static __maybe_unused struct attribute *android_htc_usb_attributes[] = {
	&dev_attr_usb_ac_cable_status.attr,
	&dev_attr_dummy_usb_serial_number.attr,
	&dev_attr_os_type.attr,
/*++ 2014/11/01 USB Team, PCN00036 ++*/
	&dev_attr_usb_disable.attr,
/*-- 2014/11/01 USB Team, PCN00036 --*/
/*++ 2014/10/29 USB Team, PCN00032 ++*/
	&dev_attr_ats.attr,
/*-- 2014/10/29 USB Team, PCN00032 --*/
/*++ 2014/11/14 USB Team, PCN00048 ++*/
	&dev_attr_usb_denied.attr,
/*-- 2014/11/14 USB Team, PCN00048 --*/
/*++ 2014/11/11 USB Team, PCN00039 ++*/
	&dev_attr_usb_cable_connect.attr,
/*-- 2014/11/11 USB Team, PCN00039 --*/
/*++ 2015/01/05 USB Team, PCN00063 ++*/
	&dev_attr_cdrom_unmount.attr,
/*-- 2015/01/05 USB Team, PCN00063 --*/
	&dev_attr_usb_modem_enable.attr,
	NULL
};

static  __maybe_unused const struct attribute_group android_usb_attr_group = {
	.attrs = android_htc_usb_attributes,
};

static void setup_vendor_info(struct android_dev *dev)
{
	if (sysfs_create_group(&dev->pdev->dev.kobj, &android_usb_attr_group))
		pr_err("%s: fail to create sysfs\n", __func__);
#if 0
	/* Link android_usb to /sys/devices/platform */
	if (sysfs_create_link(&platform_bus.kobj, &dev->pdev->dev.kobj, "android_usb"))
		pr_err("%s: fail to link android_usb to /sys/devices/platform/\n", __func__);
#endif
}
