/* drivers/input/touchscreen/atmel.c - ATMEL Touch driver
 *
 * Copyright (C) 2011 Huawei Corporation.
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

#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/mach-types.h>
#include <linux/jiffies.h>
#include <linux/stat.h>
#include <linux/slab.h>
/*add lock for hw access*/
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/otg.h>
#include "touch_platform_config.h"
#include "atmel_qt602240.h"
#ifdef TS_ATMEL_DEBUG
#define TS_DEBUG_ATMEL(fmt, args...) printk(KERN_INFO fmt, ##args)
#else
#define TS_DEBUG_ATMEL(fmt, args...)
#endif

#define ATMEL_EN_SYSFS
#define ATMEL_I2C_RETRY_TIMES 10

#define TOUCH_OMAP4430_RESET_PIN 36
/* config_setting */
#define NONE                                    0
#define CONNECTED                               1
int Unlock_flag=0;
u32 time_check = 0;
EXPORT_SYMBOL(time_check);

u8 touch_is_pressed;
EXPORT_SYMBOL(touch_is_pressed);

struct atmel_ts_data {
    struct i2c_client *client;
    struct input_dev *input_dev;
    struct workqueue_struct *atmel_wq;
    struct work_struct work;
    int (*power) (int on);
    struct early_suspend early_suspend;
    struct info_id_t *id;
    struct object_t *object_table;
    uint8_t finger_count;
    uint16_t abs_x_min;
    uint16_t abs_x_max;
    uint16_t abs_y_min;
    uint16_t abs_y_max;
    uint8_t abs_pressure_min;
    uint8_t abs_pressure_max;
    uint8_t abs_width_min;
    uint8_t abs_width_max;
    uint8_t first_pressed;
    uint8_t debug_log_level;
    struct atmel_finger_data finger_data[10];
    uint8_t finger_type;
    uint8_t finger_support;
    uint16_t finger_pressed;
    uint8_t face_suppression;
    uint8_t grip_suppression;
    uint8_t noise_status[2];
    uint16_t *filter_level;
    uint8_t calibration_confirm;
    uint64_t timestamp;
    struct atmel_config_data config_setting[2];
    int8_t noise_config[3];
    uint8_t status;
    uint8_t GCAF_sample;
    uint8_t *GCAF_level;
    uint8_t noisethr;
    uint8_t noisethr_config;
    uint8_t diag_command;
    uint8_t *ATCH_EXT;
    int8_t *ATCH_NOR;
    int8_t *ATCH_NOR_20S;
    int pre_data[11];
    struct work_struct usb_work;
	unsigned long		event;
	struct otg_transceiver	*otg;
	struct notifier_block	nb_otg;
    struct mutex lock;
#ifdef ATMEL_EN_SYSFS
    struct device dev;
#endif
    atomic_t keypad_enable;
};

static struct atmel_ts_data *private_ts;
static int atmel_ts_charger_state(int state);
#ifdef CONFIG_HAS_EARLYSUSPEND
static void atmel_ts_early_suspend(struct early_suspend *h);
static void atmel_ts_late_resume(struct early_suspend *h);
#endif
int i2c_atmel_read(struct i2c_client *client, uint16_t address, uint8_t *data, uint8_t length)
{
    int retry;
    uint8_t addr[2];

    struct i2c_msg msg[] = {
        {
            .addr = client->addr,
            .flags = 0,
            .len = 2,
            .buf = addr,
        },
        {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = length,
            .buf = data,
        }
    };
    addr[0] = address & 0xFF;
    addr[1] = (address >> 8) & 0xFF;

    for (retry = 0; retry < ATMEL_I2C_RETRY_TIMES; retry++) {
        if (i2c_transfer(client->adapter, msg, 2) == 2)
            break;
        mdelay(10);
    }
    if (retry == ATMEL_I2C_RETRY_TIMES) {
        printk(KERN_ERR "i2c_read_block retry over %d\n",
            ATMEL_I2C_RETRY_TIMES);
        return -EIO;
    }
    return 0;

}

int i2c_atmel_write(struct i2c_client *client, uint16_t address, uint8_t *data, uint8_t length)
{
    int retry, loop_i;
    uint8_t buf[length + 2];

    struct i2c_msg msg[] = {
        {
            .addr = client->addr,
            .flags = 0,
            .len = length + 2,
            .buf = buf,
        }
    };

    buf[0] = address & 0xFF;
    buf[1] = (address >> 8) & 0xFF;

    for (loop_i = 0; loop_i < length; loop_i++)
        buf[loop_i + 2] = data[loop_i];

    for (retry = 0; retry < ATMEL_I2C_RETRY_TIMES; retry++) {
        if (i2c_transfer(client->adapter, msg, 1) == 1)
            break;
        mdelay(10);
    }

    if (retry == ATMEL_I2C_RETRY_TIMES) {
        printk(KERN_ERR "i2c_write_block retry over %d\n",
            ATMEL_I2C_RETRY_TIMES);
        return -EIO;
    }
    return 0;

}

int i2c_atmel_write_byte_data(struct i2c_client *client, uint16_t address, uint8_t value)
{
    i2c_atmel_write(client, address, &value, 1);
    return 0;
}

uint16_t get_object_address(struct atmel_ts_data *ts, uint8_t object_type)
{
    uint8_t loop_i;
    for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
        if (ts->object_table[loop_i].object_type == object_type)
            return ts->object_table[loop_i].i2c_address;
    }
    return 0;
}
uint8_t get_object_size(struct atmel_ts_data *ts, uint8_t object_type)
{
    uint8_t loop_i;
    for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
        if (ts->object_table[loop_i].object_type == object_type)
            return ts->object_table[loop_i].size;
    }
    return 0;
}

uint8_t get_rid(struct atmel_ts_data *ts, uint8_t object_type)
{
    uint8_t loop_i;
    for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
        if (ts->object_table[loop_i].object_type == object_type)
            return ts->object_table[loop_i].report_ids;
    }
    return 0;
}

#ifdef ATMEL_EN_SYSFS
static ssize_t atmel_gpio_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int ret = 0;
    struct atmel_ts_data *ts_data;
    struct atmel_i2c_platform_data *pdata;

    ts_data = private_ts;
    pdata = ts_data->client->dev.platform_data;

    ret = gpio_get_value(pdata->gpio_irq);
    printk(KERN_DEBUG "GPIO_TP_INT_N=%d\n", pdata->gpio_irq);
    sprintf(buf, "GPIO_TP_INT_N=%d\n", ret);
    ret = strlen(buf) + 1;
    return ret;
}
static DEVICE_ATTR(gpio, S_IRUGO, atmel_gpio_show, NULL);
static ssize_t atmel_vendor_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int ret = 0;
    struct atmel_ts_data *ts_data;
    ts_data = private_ts;
    sprintf(buf, "%s_x%4.4X_x%4.4X\n", "ATMEL",
        ts_data->id->family_id, ts_data->id->version);
    ret = strlen(buf) + 1;
    return ret;
}

static DEVICE_ATTR(vendor, S_IRUGO, atmel_vendor_show, NULL);

static uint16_t atmel_reg_addr;

static ssize_t atmel_register_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int ret = 0;
    uint8_t ptr[1] = { 0 };
    struct atmel_ts_data *ts_data;
    ts_data = private_ts;
    if (i2c_atmel_read(ts_data->client, atmel_reg_addr, ptr, 1) < 0) {
        printk(KERN_WARNING "%s: read fail\n", __func__);
        return ret;
    }
    ret += sprintf(buf, "addr: %d, data: %d\n", atmel_reg_addr, ptr[0]);
    return ret;
}

static ssize_t atmel_register_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = 0;
    struct atmel_ts_data *ts_data;
    char buf_tmp[4], buf_zero[200];
    uint8_t write_da;

    ts_data = private_ts;
    memset(buf_tmp, 0x0, sizeof(buf_tmp));
    if ((buf[0] == 'r' || buf[0] == 'w') && buf[1] == ':' &&
        (buf[5] == ':' || buf[5] == '\n')) {
        memcpy(buf_tmp, buf + 2, 3);
        atmel_reg_addr = simple_strtol(buf_tmp, NULL, 10);
        printk(KERN_DEBUG "read addr: 0x%X\n", atmel_reg_addr);
        if (!atmel_reg_addr) {
            printk(KERN_WARNING "%s: string to number fail\n",
                                __func__);
            return count;
        }
        printk(KERN_DEBUG "%s: set atmel_reg_addr is: %d\n",
                        __func__, atmel_reg_addr);
        if (buf[0] == 'w' && buf[5] == ':' && buf[9] == '\n') {
            memcpy(buf_tmp, buf + 6, 3);
            write_da = simple_strtol(buf_tmp, NULL, 10);
            printk(KERN_DEBUG "write addr: 0x%X, data: 0x%X\n",
                        atmel_reg_addr, write_da);
            ret = i2c_atmel_write_byte_data(ts_data->client,
                        atmel_reg_addr, write_da);
            if (ret < 0) {
                printk(KERN_ERR "%s: write fail(%d)\n",
                                __func__, ret);
            }
        }
    }
    if ((buf[0] == '0') && (buf[1] == ':') && (buf[5] == ':')) {
        memcpy(buf_tmp, buf + 2, 3);
        atmel_reg_addr = simple_strtol(buf_tmp, NULL, 10);
        memcpy(buf_tmp, buf + 6, 3);
        memset(buf_zero, 0x0, sizeof(buf_zero));
        ret = i2c_atmel_write(ts_data->client, atmel_reg_addr,
            buf_zero, simple_strtol(buf_tmp, NULL, 10) - atmel_reg_addr + 1);
        if (buf[9] == 'r') {
            i2c_atmel_write_byte_data(ts_data->client,
                get_object_address(ts_data, GEN_COMMANDPROCESSOR_T6) +
                T6_CFG_BACKUPNV, 0x55);
            i2c_atmel_write_byte_data(ts_data->client,
                get_object_address(ts_data, GEN_COMMANDPROCESSOR_T6) +
                T6_CFG_RESET, 0x11);
        }
    }

    return count;
}

static DEVICE_ATTR(register, (S_IWUSR|S_IRUGO),
    atmel_register_show, atmel_register_store);

static ssize_t atmel_regdump_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int count = 0, ret_t = 0;
    struct atmel_ts_data *ts_data;
    uint16_t loop_i;
    uint8_t ptr[1] = { 0 };
    ts_data = private_ts;
    if (ts_data->id->version >= 0x14) {
        for (loop_i = 230; loop_i <= 425; loop_i++) {
            ret_t = i2c_atmel_read(ts_data->client, loop_i, ptr, 1);
            if (ret_t < 0) {
                printk(KERN_WARNING "dump fail, addr: %d\n",
                                loop_i);
            }
            count += sprintf(buf + count, "addr[%3d]: %3d, ",
                                loop_i , *ptr);
            if (((loop_i - 230) % 4) == 3)
                count += sprintf(buf + count, "\n");
        }
        count += sprintf(buf + count, "\n");
    }
    return count;
}

static ssize_t atmel_regdump_dump(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    struct atmel_ts_data *ts_data;
    ts_data = private_ts;
    if (buf[0] >= '0' && buf[0] <= '9' && buf[1] == '\n')
        ts_data->debug_log_level = buf[0] - '0';

    return count;

}

static DEVICE_ATTR(regdump, (S_IWUSR|S_IRUGO),
    atmel_regdump_show, atmel_regdump_dump);

static ssize_t atmel_debug_level_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct atmel_ts_data *ts_data;
    size_t count = 0;
    ts_data = private_ts;

    count += sprintf(buf, "%d\n", ts_data->debug_log_level);

    return count;
}

static ssize_t atmel_debug_level_dump(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    struct atmel_ts_data *ts_data;
    ts_data = private_ts;
    if (buf[0] >= '0' && buf[0] <= '9' && buf[1] == '\n')
        ts_data->debug_log_level = buf[0] - '0';

    return count;
}

static DEVICE_ATTR(debug_level, (S_IWUSR|S_IRUGO),
    atmel_debug_level_show, atmel_debug_level_dump);

static ssize_t atmel_diag_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct atmel_ts_data *ts_data;
    size_t count = 0;
    uint8_t data[T37_PAGE_SIZE];
    uint8_t loop_i, loop_j;
    int16_t rawdata;
    int x, y;
    ts_data = private_ts;
    memset(data, 0x0, sizeof(data));

    if (ts_data->diag_command != T6_CFG_DIAG_CMD_DELTAS &&
        ts_data->diag_command != T6_CFG_DIAG_CMD_REF)
        return count;

    i2c_atmel_write_byte_data(ts_data->client,
        get_object_address(ts_data, GEN_COMMANDPROCESSOR_T6) + T6_CFG_DIAG,
        ts_data->diag_command);

    x = T28_CFG_MODE0_X + ts_data->config_setting[NONE].config_T28[T28_CFG_MODE];
    y = T28_CFG_MODE0_Y - ts_data->config_setting[NONE].config_T28[T28_CFG_MODE];
    count += sprintf(buf, "Channel: %d * %d\n", x, y);

    for (loop_i = 0; loop_i < 4; loop_i++) {
        for (loop_j = 0;
            !(data[T37_MODE] == ts_data->diag_command && data[T37_PAGE] == loop_i) && loop_j < 10; loop_j++) {
            msleep(5);
            i2c_atmel_read(ts_data->client,
                get_object_address(ts_data, DIAGNOSTIC_T37), data, 2);
        }
        if (loop_j == 10)
            printk(KERN_ERR "%s: Diag data not ready\n", __func__);

        i2c_atmel_read(ts_data->client,
            get_object_address(ts_data, DIAGNOSTIC_T37) +
            T37_DATA, data, T37_PAGE_SIZE);
        for (loop_j = 0; loop_j < T37_PAGE_SIZE - 1; loop_j += 2) {
            if ((loop_i * 64 + loop_j / 2) >= (x * y)) {
                count += sprintf(buf + count, "\n");
                return count;
            } else {
                rawdata = data[loop_j+1] << 8 | data[loop_j];
                count += sprintf(buf + count, "%5d", rawdata);
                if (((loop_i * 64 + loop_j / 2) % y) == (y - 1))
                    count += sprintf(buf + count, "\n");
            }
        }
        i2c_atmel_write_byte_data(ts_data->client,
            get_object_address(ts_data, GEN_COMMANDPROCESSOR_T6) +
            T6_CFG_DIAG, T6_CFG_DIAG_CMD_PAGEUP);

    }

    return count;
}

static ssize_t atmel_diag_dump(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    struct atmel_ts_data *ts_data;
    ts_data = private_ts;
    if (buf[0] == '1')
        ts_data->diag_command = T6_CFG_DIAG_CMD_DELTAS;
    if (buf[0] == '2')
        ts_data->diag_command = T6_CFG_DIAG_CMD_REF;

    return count;
}

static DEVICE_ATTR(diag, (S_IWUSR|S_IRUGO),
    atmel_diag_show, atmel_diag_dump);
/*merge reg debug interface from msm8960 platform*/
/*add atmel object show and store attribute*/
static u8 atmel_obj_type;
static ssize_t atmel_obj_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
    int count = 0;
    int i;
	u8 obj_size;
    u16 obj_addr;
    u8 reg_val[60];
	struct atmel_ts_data *ts_data;
	ts_data = private_ts;

    if(atmel_obj_type>48)
    {
        ret = sprintf(buf, "current obj type[%d] is invalid\n", atmel_obj_type);
        return ret;
    }

    obj_addr = get_object_address(ts_data,atmel_obj_type);
    obj_size = get_object_size(ts_data,atmel_obj_type);
    if(obj_size>60)
    {
        ret = sprintf(buf, "objsize is too large max is 60\n");
        return ret;
    }

    if (i2c_atmel_read(ts_data->client, obj_addr, reg_val, obj_size) < 0) {
		ret = sprintf(buf, "read obj[%d] fail\n", atmel_obj_type);
		return ret;
	}

    ret = sprintf(buf,"current obj[%d] addr[%x]: size[%d]\n",atmel_obj_type,obj_addr,obj_size);
    buf += ret;
    count += ret;
    for(i=0;i<obj_size;i++)
    {
        ret = sprintf(buf,"0x%02x ",reg_val[i]);
        buf += ret;
        count += ret;
    }
	ret = sprintf(buf, "\n");
    buf += ret;
    count += ret;
	return count;
}

static ssize_t atmel_obj_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct atmel_ts_data *ts_data;
	char buf_tmp[4];
	u8 data,offset;
	u8 obj_size;
    u16 obj_addr;

	ts_data = private_ts;
	memset(buf_tmp, 0x0, sizeof(buf_tmp));
	if ((buf[0] == 's')&&(buf[1] == ':')&&(buf[4] == '\n')) {
		memcpy(buf_tmp, buf + 2, 2);
		atmel_obj_type = simple_strtol(buf_tmp, NULL, 10);
		printk(KERN_DEBUG "current obj:%d\n", atmel_obj_type);
		if (atmel_obj_type>48) {
			printk(KERN_ERR "input obj:%d is invalid\n",atmel_obj_type);
			return count;
		}
    }
    else if((buf[0] == 'w')&&(buf[1] == ':')&&(buf[4] == ':')&&(buf[9] == '\n'))
    {
        ret = sscanf(buf,"w:%d:0x%x",(int *)&offset,(int *)&data);
        if(ret != 2)
        {
			printk(KERN_ERR "input[%s] is invalid ret:%d\n",buf,ret);
			return ret;
        }
        obj_addr = get_object_address(ts_data,atmel_obj_type);
        obj_size = get_object_size(ts_data,atmel_obj_type);
        if(obj_size>60)
        {
            printk(KERN_ERR "obj size is too large max is 60\n");
            return ret;
        }
        i2c_atmel_write_byte_data(ts_data->client,
            obj_addr+offset, data);
        printk("write obj[%d] offset[%d] data[0x%02x] success\n",atmel_obj_type,offset,data);
    }
    else
    {
        printk(KERN_ERR "input[%s] is invalid\n",buf);
        return count;
    }
        return count;
}

static DEVICE_ATTR(obj, (S_IWUSR|S_IRUGO),
	atmel_obj_show, atmel_obj_store);

static ssize_t keypad_enable_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct atmel_ts_data *ts;
    ts = private_ts;

    return sprintf(buf, "%d\n", atomic_read(&ts->keypad_enable));
}

static ssize_t keypad_enable_store(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t count)
{
    struct atmel_ts_data *ts;
    ts = private_ts;

    unsigned int val = 0;
    sscanf(buf, "%d", &val);
    val = (val == 0 ? 0 : 1);
    atomic_set(&ts->keypad_enable, val);
    if (val) {
        printk("---=== touchkeys enabled\n");
        set_bit(BTN_TOUCH, ts->input_dev->keybit);
        set_bit(BTN_2, ts->input_dev->keybit);
    } else {
        printk("---=== touchkeys disabled\n");
        clear_bit(BTN_TOUCH, ts->input_dev->keybit);
        clear_bit(BTN_2, ts->input_dev->keybit);
    }
    input_sync(ts->input_dev);

    return count;
}

static DEVICE_ATTR(keypad_enable, S_IRUGO | S_IWUSR | S_IWGRP, keypad_enable_show, keypad_enable_store);

static struct kobject *android_touch_kobj;

static int atmel_touch_sysfs_init(void)
{
    int ret;
    android_touch_kobj = kobject_create_and_add("android_touch", NULL);
    if (android_touch_kobj == NULL) {
        printk(KERN_ERR "%s: subsystem_register failed\n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_gpio.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    atmel_reg_addr = 0;
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_register.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_regdump.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_debug_level.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_diag.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    /*merge reg debug interface from msm8960 platform*/
    /*add atmel object show and store attribute*/
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_obj.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    ret = sysfs_create_file(android_touch_kobj, &dev_attr_keypad_enable.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
        return ret;
    }
    return 0;
}

static void atmel_touch_sysfs_deinit(void)
{
    sysfs_remove_file(android_touch_kobj, &dev_attr_regdump.attr);
    sysfs_remove_file(android_touch_kobj, &dev_attr_register.attr);
    sysfs_remove_file(android_touch_kobj, &dev_attr_vendor.attr);
    sysfs_remove_file(android_touch_kobj, &dev_attr_gpio.attr);
    sysfs_remove_file(android_touch_kobj, &dev_attr_keypad_enable.attr);
    kobject_del(android_touch_kobj);
}

#endif

static void check_calibration(struct atmel_ts_data*ts)
{
    uint8_t data[T37_DATA + T37_TCH_FLAG_SIZE];
    uint8_t loop_i, loop_j, x_limit = 0, check_mask, tch_ch = 0, atch_ch = 0;

    memset(data, 0xFF, sizeof(data));
    i2c_atmel_write_byte_data(ts->client,
        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
        T6_CFG_DIAG, T6_CFG_DIAG_CMD_TCH);

    for (loop_i = 0;
        !(data[T37_MODE] == T6_CFG_DIAG_CMD_TCH && data[T37_PAGE] == T37_PAGE_NUM0) && loop_i < 10; loop_i++) {
        msleep(5);
        i2c_atmel_read(ts->client,
            get_object_address(ts, DIAGNOSTIC_T37), data, 2);
    }

    if (loop_i == 10)
        printk(KERN_ERR "%s: Diag data not ready\n", __func__);

    i2c_atmel_read(ts->client, get_object_address(ts, DIAGNOSTIC_T37), data,
        T37_DATA + T37_TCH_FLAG_SIZE);
    if (data[T37_MODE] == T6_CFG_DIAG_CMD_TCH &&
        data[T37_PAGE] == T37_PAGE_NUM0) {
        x_limit = T28_CFG_MODE0_X + ts->config_setting[NONE].config_T28[T28_CFG_MODE];
        x_limit = x_limit << 1;
        if (x_limit <= 40) {
            for (loop_i = 0; loop_i < x_limit; loop_i += 2) {
                for (loop_j = 0; loop_j < BITS_PER_BYTE; loop_j++) {
                    check_mask = BIT_MASK(loop_j);
                    if (data[T37_DATA + T37_TCH_FLAG_IDX + loop_i] &
                        check_mask)
                        tch_ch++;
                    if (data[T37_DATA + T37_TCH_FLAG_IDX + loop_i + 1] &
                        check_mask)
                        tch_ch++;
                    if (data[T37_DATA + T37_ATCH_FLAG_IDX + loop_i] &
                        check_mask)
                        atch_ch++;
                    if (data[T37_DATA + T37_ATCH_FLAG_IDX + loop_i + 1] &
                        check_mask)
                        atch_ch++;
                }
            }
        }
    }
    i2c_atmel_write_byte_data(ts->client,
        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
        T6_CFG_DIAG, T6_CFG_DIAG_CMD_PAGEUP);
    if (tch_ch && (atch_ch == 0)) {
        if (jiffies > (ts->timestamp + HZ/2) && (ts->calibration_confirm == 1)) {
            ts->calibration_confirm = 2;
        }
        if (ts->calibration_confirm < 2)
            ts->calibration_confirm = 1;
        ts->timestamp = jiffies;
    } else if (atch_ch>1 || tch_ch>8) {
        ts->calibration_confirm = 0;
        i2c_atmel_write_byte_data(ts->client,
            get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
            T6_CFG_CALIBRATE, 0x55);
    }
}

static void confirm_calibration(struct atmel_ts_data*ts)
{
    i2c_atmel_write(ts->client,
    get_object_address(ts, GEN_ACQUISITIONCONFIG_T8) +
    T8_CFG_TCHAUTOCAL, ts->ATCH_NOR_20S, 6);
    ts->pre_data[0] = 2;
}


static void msg_process_finger_data_x10y10bit(struct atmel_finger_data *fdata, uint8_t *data)
{
    fdata->x = data[T9_MSG_XPOSMSB] << 2 | data[T9_MSG_XYPOSLSB] >> 6;
    fdata->y = data[T9_MSG_YPOSMSB] << 2 | (data[T9_MSG_XYPOSLSB] & 0x0C) >>2;
    fdata->w = data[T9_MSG_TCHAREA];
    fdata->z = data[T9_MSG_TCHAMPLITUDE];
}
static void msg_process_finger_data_x10y12bit(struct atmel_finger_data *fdata, uint8_t *data)
{
    fdata->x = data[T9_MSG_XPOSMSB] << 2 | data[T9_MSG_XYPOSLSB] >> 6;
    fdata->y = data[T9_MSG_YPOSMSB] << 4 | (data[T9_MSG_XYPOSLSB] & 0x0F) ;
    fdata->w = data[T9_MSG_TCHAREA];
    fdata->z = data[T9_MSG_TCHAMPLITUDE];
}
static void msg_process_multitouch(struct atmel_ts_data *ts, uint8_t *data, uint8_t idx)
{
    if (ts->calibration_confirm < 2 && ts->id->version == 0x10)
        check_calibration(ts);
    if(ts->abs_y_max>=1024)
    {
        msg_process_finger_data_x10y12bit(&ts->finger_data[idx], data);
    }
    else
    {
        msg_process_finger_data_x10y10bit(&ts->finger_data[idx], data);
    }
    if (data[T9_MSG_STATUS] & T9_MSG_STATUS_RELEASE) {
        if (ts->grip_suppression & BIT(idx))
            ts->grip_suppression &= ~BIT(idx);
        if (ts->finger_pressed & BIT(idx)) {
            ts->finger_count--;
            ts->finger_pressed &= ~BIT(idx);
            if (!ts->first_pressed) {
                if (!ts->finger_count)
                    ts->first_pressed = 1;
                printk(KERN_INFO "E%d@%d,%d\n",
                    idx + 1, ts->finger_data[idx].x, ts->finger_data[idx].y);
            }


           if (ts->pre_data[0] < 2 && Unlock_flag!=1) {
                if (ts->finger_count)
                {
                    i2c_atmel_write_byte_data(ts->client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_CALIBRATE, 0x55);
                }
                else if (!ts->finger_count && ts->pre_data[0] == 1)
                    ts->pre_data[0] = 0;
            }
        }
    }
    else if ((data[T9_MSG_STATUS] & (T9_MSG_STATUS_DETECT|T9_MSG_STATUS_PRESS)) &&
        !(ts->finger_pressed & BIT(idx)))
    {
        if (ts->id->version >= 0x10 && ts->pre_data[0] < 2)
        {
            if(Unlock_flag==1)
            {
            	if(jiffies > (ts->timestamp + 20 * HZ))
            	{
            		confirm_calibration(ts);

            	}
            }
        }
        if (!(ts->grip_suppression & BIT(idx)))
        {
            if (!ts->first_pressed)
                printk(KERN_INFO "S%d@%d,%d\n",
                    idx + 1, ts->finger_data[idx].x, ts->finger_data[idx].y);
            ts->finger_count++;
            ts->finger_pressed |= BIT(idx);
            if (ts->id->version >= 0x10 && ts->pre_data[0] < 2)
            {
                ts->pre_data[idx + 1] = ts->finger_data[idx].x;
                ts->pre_data[idx + 2] = ts->finger_data[idx].y;
                if (ts->finger_count == ts->finger_support)
                {
                    i2c_atmel_write_byte_data(ts->client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_CALIBRATE, 0x55);
                }
                else if (!ts->pre_data[0] && ts->finger_count == 1)
                    ts->pre_data[0] = 1;
            }
        }
    }
     else if ((data[T9_MSG_STATUS] & (T9_MSG_STATUS_DETECT|T9_MSG_STATUS_PRESS)) && ts->pre_data[0] < 2 && Unlock_flag!=1)
     {
            if (ts->finger_count == 1 && ts->pre_data[0] &&
                 (idx == 0 && ((abs(ts->finger_data[idx].y - ts->pre_data[idx + 2]) > 120)
                  || (abs(ts->finger_data[idx].x - ts->pre_data[idx + 1]) > 120))))
            {
                Unlock_flag=1;
                ts->calibration_confirm=2;
                ts->timestamp = jiffies;
            }
      }
}

static void compatible_input_report(struct input_dev *idev,
                struct atmel_finger_data *fdata, uint8_t press, uint8_t last)
{
    if (!press)
    {
        time_check = (u32) ktime_to_ms(ktime_get());
        touch_is_pressed = 0;
        input_mt_sync(idev);
        input_report_key(idev,BTN_TOUCH,1);
    }
    else
    {
        TS_DEBUG_ATMEL("*** Touch report_key x = %d, y = %d, z = %d, w = %d ***\n ",fdata->x,fdata->y,fdata->z,fdata->w);
        input_report_abs(idev, ABS_MT_TOUCH_MAJOR, fdata->z);
        input_report_abs(idev, ABS_MT_WIDTH_MAJOR, fdata->w);
        input_report_abs(idev, ABS_MT_POSITION_X, fdata->x);
        input_report_abs(idev, ABS_MT_POSITION_Y, fdata->y);
        input_mt_sync(idev);
    }
}

static void multi_input_report(struct atmel_ts_data *ts)
{
    uint8_t loop_i, finger_report = 0;

    for (loop_i = 0; loop_i < ts->finger_support; loop_i++) {
        if (ts->finger_pressed & BIT(loop_i)) {
                touch_is_pressed = 1;
                compatible_input_report(ts->input_dev, &ts->finger_data[loop_i],
                                        1, (ts->finger_count == ++finger_report));
        }
    }
}

static irqreturn_t atmel_ts_irq_handler(int irq, void *dev_id)
{
    struct atmel_ts_data *ts = dev_id;
    struct atmel_finger_data *fdata = ts->finger_data;
    int ret;
    uint8_t data[7];
    int8_t report_type;
    uint8_t msg_byte_num = 7;
    int8_t usb_plug_stat;

    TS_DEBUG_ATMEL("***report atmel_ts_irq_handler begin***\n");
    mutex_lock(&ts->lock);
    memset(data, 0x0, sizeof(data));
    ret = i2c_atmel_read(ts->client, get_object_address(ts,
        GEN_MESSAGEPROCESSOR_T5), data, 7);
    report_type = data[MSG_RID] - ts->finger_type;
    if (report_type >= 0 && report_type < ts->finger_support) {
        msg_process_multitouch(ts, data, report_type);
    } else {
        if (data[MSG_RID] == get_rid(ts, GEN_COMMANDPROCESSOR_T6)) {
            if(data[1]&0x10){
            ts->timestamp = jiffies;
            }
            if(data[1]&0x80)
            {
                printk("--->Lintar test multi_input_report 100ms\n");
                msleep(100);
                i2c_atmel_write_byte_data(ts->client,
                get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                T6_CFG_CALIBRATE, 0x55);
            }
            msg_byte_num = 5;
        }
        if (data[MSG_RID] == get_rid(ts, PROCI_TOUCHSUPPRESSION_T42))
        {
            if (ts->calibration_confirm < 2 && ts->id->version == 0x10){
            i2c_atmel_write_byte_data(ts->client,
                get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                T6_CFG_CALIBRATE, 0x55);
            }
            ts->face_suppression = data[T20_MSG_STATUS];
            printk(KERN_INFO "Touch Face suppression %s: ",
            ts->face_suppression ? "Active" : "Inactive");
            msg_byte_num = 2;
        }
    }

    if (!ts->finger_count || ts->face_suppression) {
        ts->finger_pressed = 0;
        ts->finger_count = 0;
        if (atomic_read(&ts->keypad_enable)) {
            compatible_input_report(ts->input_dev, NULL, 0, 1);
        } else if (fdata->y < 1305) {
            compatible_input_report(ts->input_dev, NULL, 0, 1);
        }
    } else {
        if (atomic_read(&ts->keypad_enable)) {
            multi_input_report(ts);
        } else if (fdata->y < 1305) {
            multi_input_report(ts);
        }
    }

    input_sync(ts->input_dev);
    usb_plug_stat = get_plugin_device_status()?CONNECTED:NONE;

    if(unlikely(usb_plug_stat!= ts->status))
    {
        ts->status = usb_plug_stat;
        atmel_ts_charger_state(usb_plug_stat);
    }
    mutex_unlock(&ts->lock);

    return IRQ_HANDLED;
}

static int read_object_table(struct atmel_ts_data *ts)
{
    uint8_t i, type_count = 0;
    uint8_t data[6];
    memset(data, 0x0, sizeof(data));

    ts->object_table = kzalloc(sizeof(struct object_t)*ts->id->num_declared_objects, GFP_KERNEL);
    if (ts->object_table == NULL) {
        printk(KERN_ERR "%s: allocate object_table failed\n", __func__);
        return -ENOMEM;
    }

    for (i = 0; i < ts->id->num_declared_objects; i++) {
        i2c_atmel_read(ts->client, i * 6 + 0x07, data, 6);
        ts->object_table[i].object_type = data[OBJ_TABLE_TYPE];
        ts->object_table[i].i2c_address =
            data[OBJ_TABLE_LSB] | data[OBJ_TABLE_MSB] << 8;
        ts->object_table[i].size = data[OBJ_TABLE_SIZE] + 1;
        ts->object_table[i].instances = data[OBJ_TABLE_INSTANCES];
        ts->object_table[i].num_report_ids = data[OBJ_TABLE_RIDS];
        if (data[OBJ_TABLE_RIDS]) {
            ts->object_table[i].report_ids = type_count + 1;
            type_count += data[OBJ_TABLE_RIDS];
        }
        if (data[OBJ_TABLE_TYPE] == TOUCH_MULTITOUCHSCREEN_T9)
            ts->finger_type = ts->object_table[i].report_ids;
    }

    return 0;
}
static int atmel_ts_charger_state(int state)
{
    int ret = 0;
    struct atmel_ts_data *ts;
    struct atmel_i2c_platform_data *pdata;

    ts = private_ts;
    pdata = ts->client->dev.platform_data;
    pr_info("***USB status is : [%d]********\n",state);
    if (ts->config_setting[state].config_T8 != NULL)
        i2c_atmel_write(ts->client,
            get_object_address(ts, GEN_ACQUISITIONCONFIG_T8),
            ts->config_setting[state].config_T8,
            get_object_size(ts, GEN_ACQUISITIONCONFIG_T8));
    if (ts->config_setting[state].config_T46 != NULL)
        i2c_atmel_write(ts->client,
            get_object_address(ts, SPT_CTECONFIG_T46),
            ts->config_setting[state].config_T46,
            get_object_size(ts, SPT_CTECONFIG_T46));
    if (ts->config_setting[state].config_T48 != NULL) {
        i2c_atmel_write(ts->client,
            get_object_address(ts, PROCG_NOISESUPPRESSION_T48),
            ts->config_setting[state].config_T48,
            get_object_size(ts, PROCG_NOISESUPPRESSION_T48));
    }

    Unlock_flag=0;
    ts->calibration_confirm = 0;

    msleep(20);

    i2c_atmel_write_byte_data(ts->client,		//calibrate the baseline
    get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                            T6_CFG_CALIBRATE, 0x55);

    return ret;
}

static int atmel_ts_probe(struct i2c_client *client,
             const struct i2c_device_id *id)
{
    struct atmel_ts_data *ts;
    struct atmel_i2c_platform_data *pdata;
    int ret = 0, intr = 0;
    uint8_t loop_i;
    struct i2c_msg msg[2];
    uint8_t data[16];
    uint8_t CRC_check = 0;
    touch_is_pressed = 0;
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        printk(KERN_ERR"%s: need I2C_FUNC_I2C\n", __func__);
        ret = -ENODEV;
        goto err_check_functionality_failed;
    }

    ts = kzalloc(sizeof(struct atmel_ts_data), GFP_KERNEL);
    if (ts == NULL) {
        printk(KERN_ERR"%s: allocate atmel_ts_data failed\n", __func__);
        ret = -ENOMEM;
        goto err_alloc_data_failed;
    }

    ts->client = client;
    i2c_set_clientdata(client, ts);
    pdata = client->dev.platform_data;

    if(pdata->power_on)
    {
        ret = pdata->power_on(&client->dev);
        if(ret < 0)
        {

            printk(KERN_ERR "atmel_ts_probe: get power fail!\n");
            goto err_power_on_failed;
        }
    }
    /* tell system other tp has detected */
    if(pdata->read_touch_probe_flag)
    {
        ret = pdata->read_touch_probe_flag();
    }
    if(ret)
    {
        printk(KERN_ERR "%s: the touch driver has detected! \n", __func__);
        goto err_other_driver_detected;
    }
    else
    {
        printk(KERN_ERR "%s: it's the first touch driver! \n", __func__);
    }
    if (pdata) {
        intr = pdata->gpio_irq;
        client->irq = gpio_to_irq(intr);
    }
    if (pdata->reset)
        pdata->reset();
    ret = gpio_request(intr, "gpio_tp_intr");
    if (ret) {
        printk(KERN_ERR"atmel_ts_probe: gpio_request failed for intr %d\n", intr);
        goto err_request_gpio_failed;
    }
    ret = gpio_direction_input(intr);
    if (ret) {
        printk(KERN_ERR"atmel_ts_probe: gpio_direction_input failed for intr %d\n", intr);
        goto err_gpio_direction_failed;
    }

    for (loop_i = 0; loop_i < 10; loop_i++) {
        if (!gpio_get_value(intr))
            break;
        msleep(10);
    }

    if (loop_i == 10)
        printk(KERN_ERR "No Messages\n");

    /* read message*/
    msg[0].addr = ts->client->addr;
    msg[0].flags = I2C_M_RD;
    msg[0].len = 7;
    msg[0].buf = data;
    ret = i2c_transfer(client->adapter, msg, 1);

    if (ret < 0) {
        printk(KERN_INFO "No Atmel chip inside\n");
        goto err_detect_failed;
    }

    if (data[MSG_RID] == 0x01 &&
        (data[T6_MSG_STATUS] & (T6_MSG_STATUS_SIGERR|T6_MSG_STATUS_COMSERR))) {
        printk(KERN_INFO "atmel_ts_probe(): init err: %x\n", data[1]);
        goto err_detect_failed;
    } else {
        for (loop_i = 0; loop_i < 10; loop_i++) {
            if (gpio_get_value(intr)) {
                printk(KERN_INFO "Touch: No more message\n");
                break;
            }
            ret = i2c_transfer(client->adapter, msg, 1);
            msleep(10);
        }
    }

    /* Read the info block data. */
    ts->id = kzalloc(sizeof(struct info_id_t), GFP_KERNEL);
    if (ts->id == NULL) {
        printk(KERN_ERR"%s: allocate info_id_t failed\n", __func__);
        goto err_alloc_failed;
    }
    ret = i2c_atmel_read(client, 0x00, data, 7);

    ts->id->family_id = data[INFO_BLK_FID];
    ts->id->variant_id = data[INFO_BLK_VID];
    if (ts->id->family_id == 0x80 && ts->id->variant_id == 0x10)
        ts->id->version = data[INFO_BLK_VER] + 6;
    else
        ts->id->version = data[INFO_BLK_VER];
    ts->id->build = data[INFO_BLK_BUILD];
    ts->id->matrix_x_size = data[INFO_BLK_XSIZE];
    ts->id->matrix_y_size = data[INFO_BLK_YSIZE];
    ts->id->num_declared_objects = data[INFO_BLK_OBJS];

    /* Read object table. */
    ret = read_object_table(ts);
    if (ret < 0)
        goto err_alloc_failed;


    if (pdata) {
        ts->finger_support = pdata->config_T9[T9_CFG_NUMTOUCH];
        /* OBJECT CONFIG CRC check */
        if (pdata->object_crc[0]) {
            ret = i2c_atmel_write_byte_data(client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_CALIBRATE, 0x55);
            for (loop_i = 0; loop_i < 10; loop_i++) {
                if (!gpio_get_value(intr)) {
                    ret = i2c_atmel_read(ts->client, get_object_address(ts,
                                GEN_MESSAGEPROCESSOR_T5), data, 5);
                    if (data[MSG_RID] == get_rid(ts, GEN_COMMANDPROCESSOR_T6))
                        break;
                }
                msleep(10);
            }
            if (loop_i == 10)
                printk(KERN_INFO "Touch: No checksum read\n");
            else {
                for (loop_i = 0; loop_i < 3; loop_i++) {
                    if (pdata->object_crc[loop_i] !=
                        data[T6_MSG_CHECKSUM + loop_i]) {
                        break;
                    }
                }
                if (loop_i == 3) {
                    CRC_check = 1;
                }
            }
        }
        ts->abs_x_min = pdata->abs_x_min;
        ts->abs_x_max = pdata->abs_x_max;
        ts->abs_y_min = pdata->abs_y_min;
        ts->abs_y_max = pdata->abs_y_max;
        ts->abs_pressure_min = pdata->abs_pressure_min;
        ts->abs_pressure_max = pdata->abs_pressure_max;
        ts->abs_width_min = pdata->abs_width_min;
        ts->abs_width_max = pdata->abs_width_max;
        ts->GCAF_level = pdata->GCAF_level;
        if (ts->id->version >= 0x10) {
            ts->ATCH_EXT = &pdata->config_T8[6];
            ts->timestamp = jiffies + 60 * HZ;
        }
        ts->ATCH_NOR = pdata->ATCH_NOR;
        ts->ATCH_NOR_20S = pdata->ATCH_NOR_20S;
        ts->filter_level = pdata->filter_level;

        mutex_init(&ts->lock);

        if(get_plugin_device_status())
        {
        	ts->status = CONNECTED;
            printk("***check USB status : [CONNECTED]********\n");
        }
        else
        {
            ts->status = NONE;
            printk("***check USB status : [NONE]********\n");
        }

        ts->config_setting[NONE].config_T7
            = ts->config_setting[CONNECTED].config_T7
            = pdata->config_T7;
        ts->config_setting[NONE].config_T8 = pdata->config_T8;

        ts->config_setting[NONE].config_T9 = pdata->config_T9;
        ts->config_setting[NONE].config_T22 = pdata->config_T22;
        ts->config_setting[NONE].config_T28 = pdata->config_T28;
        ts->config_setting[NONE].config_T46 = pdata->config_T46;
        ts->config_setting[NONE].config_T48 = pdata->config_T48;

            ts->config_setting[CONNECTED].config_T8 = pdata->cable_config_T8;
            ts->config_setting[CONNECTED].config_T46 = pdata->cable_config_T46;
            ts->config_setting[CONNECTED].config_T48 = pdata->cable_config_T48;


        if (pdata->noise_config[0])
            for (loop_i = 0; loop_i < 3; loop_i++)
                ts->noise_config[loop_i] = pdata->noise_config[loop_i];

        if (pdata->cable_config[0]) {
            ts->config_setting[NONE].config[CB_TCHTHR] =
                pdata->config_T9[T9_CFG_TCHTHR];
            ts->config_setting[NONE].config[CB_NOISETHR] =
                pdata->config_T22[T22_CFG_NOISETHR];
            ts->config_setting[NONE].config[CB_IDLEGCAFDEPTH] =
                pdata->config_T28[T28_CFG_IDLEGCAFDEPTH];
            ts->config_setting[NONE].config[CB_ACTVGCAFDEPTH] =
                pdata->config_T28[T28_CFG_ACTVGCAFDEPTH];
            for (loop_i = 0; loop_i < 4; loop_i++)
                ts->config_setting[CONNECTED].config[loop_i] =
                    pdata->cable_config[loop_i];
            ts->GCAF_sample =
                ts->config_setting[CONNECTED].config[CB_ACTVGCAFDEPTH];
            if (ts->id->version >= 0x20)
                ts->noisethr = pdata->cable_config[CB_TCHTHR] -
                    pdata->config_T9[T9_CFG_TCHHYST];
            else
                ts->noisethr = pdata->cable_config[CB_TCHTHR];
            ts->noisethr_config =
                ts->config_setting[CONNECTED].config[CB_NOISETHR];
        } else {
            if (pdata->cable_config_T7[0])
                ts->config_setting[CONNECTED].config_T7 =
                    pdata->cable_config_T7;
            if (pdata->cable_config_T8[0])
                ts->config_setting[CONNECTED].config_T8 =
                    pdata->cable_config_T8;
            if (pdata->cable_config_T9[0]) {
                ts->config_setting[CONNECTED].config_T9 =
                    pdata->cable_config_T9;
                ts->config_setting[CONNECTED].config_T22 =
                    pdata->cable_config_T22;
                ts->config_setting[CONNECTED].config_T28 =
                    pdata->cable_config_T28;
                ts->GCAF_sample =
                    ts->config_setting[CONNECTED].config_T28[T28_CFG_ACTVGCAFDEPTH];
            }
            if (ts->status == CONNECTED)
                ts->noisethr = (ts->id->version >= 0x20) ?
                    pdata->cable_config_T9[T9_CFG_TCHTHR] - pdata->cable_config_T9[T9_CFG_TCHHYST] :
                    pdata->cable_config_T9[T9_CFG_TCHTHR];
            else
                ts->noisethr = (ts->id->version >= 0x20) ?
                    pdata->config_T9[T9_CFG_TCHTHR] - pdata->config_T9[T9_CFG_TCHHYST] :
                    pdata->config_T9[T9_CFG_TCHTHR];
            ts->noisethr_config = pdata->cable_config_T22[T22_CFG_NOISETHR];

        }

        if (!CRC_check) {
            printk(KERN_INFO "Touch: Config reload\n");

            i2c_atmel_write(ts->client, get_object_address(ts, SPT_CTECONFIG_T28),
                pdata->config_T28, get_object_size(ts, SPT_CTECONFIG_T28));

            ret = i2c_atmel_write_byte_data(client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_BACKUPNV, 0x55);
            msleep(10);

            ret = i2c_atmel_write_byte_data(client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_RESET, 0x11);
            msleep(100);

            i2c_atmel_write(ts->client,
                get_object_address(ts, GEN_COMMANDPROCESSOR_T6),
                pdata->config_T6,
                get_object_size(ts, GEN_COMMANDPROCESSOR_T6));
            i2c_atmel_write(ts->client,
                get_object_address(ts, GEN_POWERCONFIG_T7),
                pdata->config_T7,
                get_object_size(ts, GEN_POWERCONFIG_T7));
            i2c_atmel_write(ts->client,
                get_object_address(ts, GEN_ACQUISITIONCONFIG_T8),
                pdata->config_T8,
                get_object_size(ts, GEN_ACQUISITIONCONFIG_T8));
            i2c_atmel_write(ts->client,
                get_object_address(ts, TOUCH_MULTITOUCHSCREEN_T9),
                pdata->config_T9,
                get_object_size(ts, TOUCH_MULTITOUCHSCREEN_T9));
            i2c_atmel_write(ts->client,
                get_object_address(ts, TOUCH_KEYARRAY_T15),
                pdata->config_T15,
                get_object_size(ts, TOUCH_KEYARRAY_T15));
            i2c_atmel_write(ts->client,
                get_object_address(ts, SPT_GPIOPWM_T19),
                pdata->config_T19,
                get_object_size(ts, SPT_GPIOPWM_T19));
            i2c_atmel_write(ts->client,
                get_object_address(ts, PROCI_GRIPSUPPRESSION_T40),
                pdata->config_T40,
                get_object_size(ts, PROCI_GRIPSUPPRESSION_T40));

            i2c_atmel_write(ts->client,
                get_object_address(ts, PROCI_TOUCHSUPPRESSION_T42),
                pdata->config_T42,
                get_object_size(ts, PROCI_TOUCHSUPPRESSION_T42));
            i2c_atmel_write(ts->client,
                get_object_address(ts, PROCG_NOISESUPPRESSION_T48),
                pdata->config_T48,
                get_object_size(ts, PROCG_NOISESUPPRESSION_T48));
            i2c_atmel_write(ts->client,
                get_object_address(ts, TOUCH_PROXIMITY_T23),
                pdata->config_T23,
                get_object_size(ts, TOUCH_PROXIMITY_T23));
            i2c_atmel_write(ts->client,
                get_object_address(ts, SPT_SELFTEST_T25),
                pdata->config_T25,
                get_object_size(ts, SPT_SELFTEST_T25));
            i2c_atmel_write(ts->client,
                get_object_address(ts, SPT_CTECONFIG_T46),
                pdata->config_T46,
                get_object_size(ts, SPT_CTECONFIG_T46));
            i2c_atmel_write(ts->client,
                get_object_address(ts, PROCI_STYLUS_T47),
                pdata->config_T47,
                get_object_size(ts, PROCI_STYLUS_T47));

            ret = i2c_atmel_write_byte_data(client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_BACKUPNV, 0x55);

            for (loop_i = 0; loop_i < 10; loop_i++) {
                if (!gpio_get_value(intr))
                    break;
                printk(KERN_INFO "Touch: wait for Message(%d)\n", loop_i + 1);
                msleep(10);
            }

            i2c_atmel_read(client,
                get_object_address(ts, GEN_MESSAGEPROCESSOR_T5), data, 7);

            ret = i2c_atmel_write_byte_data(client,
                        get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                        T6_CFG_RESET, 0x11);
            msleep(100);

        }

        if (ts->status == CONNECTED) {
            printk(KERN_INFO "Touch: set cable config\n");
            if (ts->config_setting[CONNECTED].config_T8 != NULL)
                i2c_atmel_write(ts->client,
                    get_object_address(ts, GEN_ACQUISITIONCONFIG_T8),
                    ts->config_setting[CONNECTED].config_T8,
                    get_object_size(ts, GEN_ACQUISITIONCONFIG_T8));
            if (ts->config_setting[CONNECTED].config_T46 != NULL)
                i2c_atmel_write(ts->client,
                    get_object_address(ts, SPT_CTECONFIG_T46),
                    ts->config_setting[CONNECTED].config_T46,
                    get_object_size(ts, SPT_CTECONFIG_T46));
            if (ts->config_setting[CONNECTED].config_T48 != NULL) {
                i2c_atmel_write(ts->client,
                    get_object_address(ts, PROCG_NOISESUPPRESSION_T48),
                    ts->config_setting[CONNECTED].config_T48,
                    get_object_size(ts, PROCG_NOISESUPPRESSION_T48));
            }
        }
    }
    ts->calibration_confirm=0;
    ts->input_dev = input_allocate_device();
    if (ts->input_dev == NULL) {
        ret = -ENOMEM;
        dev_err(&client->dev, "Failed to allocate input device\n");
        goto err_input_dev_alloc_failed;
    }
    ts->input_dev->name = "atmel-touchscreen";
    set_bit(EV_SYN, ts->input_dev->evbit);
    set_bit(EV_KEY, ts->input_dev->evbit);
    set_bit(BTN_TOUCH, ts->input_dev->keybit);
    set_bit(BTN_2, ts->input_dev->keybit);
    set_bit(EV_ABS, ts->input_dev->evbit);
    set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
    atomic_set(&ts->keypad_enable, 1);

    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
                ts->abs_x_min, ts->abs_x_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
                ts->abs_y_min, ts->abs_y_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR,
                ts->abs_pressure_min, ts->abs_pressure_max,
                0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR,
                ts->abs_width_min, ts->abs_width_max, 0, 0);

    ret = input_register_device(ts->input_dev);
    if (ret) {
        dev_err(&client->dev,
            "atmel_ts_probe: Unable to register %s input device\n",
            ts->input_dev->name);
        goto err_input_register_device_failed;
    }

    ret = request_threaded_irq(client->irq,NULL,atmel_ts_irq_handler,
                              IRQF_TRIGGER_LOW |IRQF_ONESHOT,client->name,ts);

    TS_DEBUG_ATMEL("***gpio_atmel_mXT224E_request_irqs***: request_irq input_gpio =  %d, ",intr);
    TS_DEBUG_ATMEL("***gpio_atmel_mXT224E_request_irqs***: irq number %d\n",client->irq);
    if (ret)
    {
    	free_irq(client->irq,ts);
        dev_err(&client->dev, "request_irq failed\n");
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
    ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 1;
    ts->early_suspend.suspend = atmel_ts_early_suspend;
    ts->early_suspend.resume = atmel_ts_late_resume;
    register_early_suspend(&ts->early_suspend);
#endif
/* modify mutex init before request_irq */
    private_ts = ts;
#ifdef ATMEL_EN_SYSFS
    atmel_touch_sysfs_init();
#endif

    dev_info(&client->dev, "Start touchscreen %s in interrupt mode\n",
            ts->input_dev->name);
    /* tell system this tp has detected */
    if(pdata->set_touch_probe_flag)
    {
        pdata->set_touch_probe_flag(ret);
    }
    return 0;

err_input_register_device_failed:
    input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
err_alloc_failed:
err_detect_failed:
err_gpio_direction_failed:
    gpio_free(intr);
err_request_gpio_failed:
    if (pdata->power_off)
        pdata->power_off();
err_other_driver_detected:
    kfree(ts);
    private_ts = NULL;

err_alloc_data_failed:
err_check_functionality_failed:
err_power_on_failed:
/* delete two lines,needn't control avdd in driver */

    return ret;
}

static void atmel_ts_shutdown(struct i2c_client *client)
{
    gpio_request(TOUCH_OMAP4430_RESET_PIN,"TOUCH_RESET");
    gpio_direction_output(TOUCH_OMAP4430_RESET_PIN, 0);
    mdelay(10);
}

static int atmel_ts_remove(struct i2c_client *client)
{
    struct atmel_ts_data *ts = i2c_get_clientdata(client);
    struct atmel_i2c_platform_data *pdata;
    pdata = client->dev.platform_data;
#ifdef ATMEL_EN_SYSFS
    atmel_touch_sysfs_deinit();
#endif

    unregister_early_suspend(&ts->early_suspend);
    free_irq(client->irq, ts);

    input_unregister_device(ts->input_dev);
    kfree(ts);
    private_ts = NULL;
    printk(KERN_INFO"!!!turn_off power in %s!!!", __func__);
    if (pdata->power_off)
        pdata->power_off();
    return 0;
}

static int atmel_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
    struct atmel_ts_data *ts = i2c_get_clientdata(client);
    disable_irq(client->irq);

    mutex_lock(&ts->lock);
    ts->finger_pressed = 0;
    ts->finger_count = 0;
    ts->first_pressed = 0;
    touch_is_pressed = 0;
    if (ts->id->version >= 0x10) {
        ts->pre_data[0] = 0;
        i2c_atmel_write(ts->client,
            get_object_address(ts, GEN_ACQUISITIONCONFIG_T8) + T8_CFG_ATCHCALST,
            ts->ATCH_EXT, 4);
    }

    i2c_atmel_write_byte_data(client,
        get_object_address(ts, GEN_POWERCONFIG_T7) + T7_CFG_IDLEACQINT, 0x0);
    i2c_atmel_write_byte_data(client,
        get_object_address(ts, GEN_POWERCONFIG_T7) + T7_CFG_ACTVACQINT, 0x0);
    mutex_unlock(&ts->lock);
    return 0;
}

static int atmel_ts_resume(struct i2c_client *client)
{
/* delete ATCH_NOR array,get ATCH_NOR from platform data */
    struct atmel_ts_data *ts = i2c_get_clientdata(client);
    mutex_lock(&ts->lock);

    if (ts->id->version >= 0x10)
        ts->timestamp = jiffies;
    Unlock_flag=0;

    i2c_atmel_write(ts->client,
        get_object_address(ts, GEN_POWERCONFIG_T7),
        ts->config_setting[ts->status].config_T7,
        get_object_size(ts, GEN_POWERCONFIG_T7));
        ts->calibration_confirm = 0;
        msleep(1);


    i2c_atmel_write(ts->client,
        get_object_address(ts, GEN_ACQUISITIONCONFIG_T8) +
        T8_CFG_TCHAUTOCAL, ts->ATCH_NOR, 6);
            i2c_atmel_write_byte_data(client,
                get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
                T6_CFG_CALIBRATE, 0x55);
    mutex_unlock(&ts->lock);
    enable_irq(client->irq);
    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void atmel_ts_early_suspend(struct early_suspend *h)
{
    struct atmel_ts_data *ts;
    ts = container_of(h, struct atmel_ts_data, early_suspend);
    atmel_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void atmel_ts_late_resume(struct early_suspend *h)
{
    struct atmel_ts_data *ts;
    ts = container_of(h, struct atmel_ts_data, early_suspend);
    atmel_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id atml_ts_i2c_id[] = {
    { ATMEL_QT602240_NAME, 0 },
    { }
};

static struct i2c_driver atmel_ts_driver = {
    .id_table = atml_ts_i2c_id,
    .probe = atmel_ts_probe,
    .remove = atmel_ts_remove,
    .shutdown = atmel_ts_shutdown,
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend = atmel_ts_suspend,
    .resume = atmel_ts_resume,
#endif
    .driver = {
            .name = ATMEL_QT602240_NAME,
    },
};

static int __devinit atmel_ts_init(void)
{
    printk(KERN_INFO "atmel_ts_init():\n");
    return i2c_add_driver(&atmel_ts_driver);
}

static void __exit atmel_ts_exit(void)
{
    i2c_del_driver(&atmel_ts_driver);
}

module_init(atmel_ts_init);
module_exit(atmel_ts_exit);

MODULE_DESCRIPTION("ATMEL Touch driver");
MODULE_LICENSE("GPL");
