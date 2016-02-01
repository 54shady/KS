#ifndef _TEST_H_
#define _TEST_H_

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/irq.h>
#include <linux/regulator/consumer.h>

/* Registers define */
#define GTP_READ_COOR_ADDR    0x814E
#define GTP_REG_SLEEP         0x8040
#define GTP_REG_SENSOR_ID     0x814A
#define GTP_REG_CONFIG_DATA   0x8047
#define GTP_REG_VERSION       0x8140
#define RESOLUTION_LOC        3
#define TRIGGER_LOC           8

#define GTP_CUSTOM_CFG        1
#define GTP_MAX_HEIGHT   800
#define GTP_MAX_WIDTH    1280
#define GTP_INT_TRIGGER  0            // 0: Rising 1: Falling

/* pin config */
#define GTP_INT_PORT	IMX_GPIO_NR(5, 30)
#define GTP_RST_PORT   IMX_GPIO_NR(5, 19)

/* data struct for my ts */
struct my_ts_data {
    struct i2c_client *client;
    struct input_dev  *input_dev;
    struct hrtimer timer;
    struct work_struct  work;
    int irq_is_disable;
    unsigned short abs_x_max;
    unsigned short abs_y_max;
    unsigned char max_touch_num;
    unsigned char int_trigger_type;
    unsigned char green_wake_mode;
    unsigned char enter_update;
    unsigned char gtp_is_suspend;
    unsigned char gtp_rawdiff_mode;
    unsigned char gtp_cfg_len;
    unsigned char fixed_cfg;
    unsigned char fw_error;
    unsigned char pnl_init_error;

	unsigned int rst_pin;
	unsigned int int_pin;
};


/* misc stuff */
#define GTP_POLL_TIME         10    
#define GTP_ADDR_LENGTH       2
#define GTP_CONFIG_MIN_LENGTH 186
#define GTP_CONFIG_MAX_LENGTH 240
#define FAIL                  0
#define SUCCESS               1
#define SWITCH_OFF            0
#define SWITCH_ON             1
#define GTP_CUSTOM_CFG        1
#define GTP_CHANGE_X2Y        0
#define GTP_DRIVER_SEND_CFG   1
#define GTP_HAVE_TOUCH_KEY    1
#define GTP_POWER_CTRL_SLEEP  0
#define GTP_ICS_SLOT_REPORT   0
#define GTP_KEY_TAB  {KEY_MENU, KEY_HOME, KEY_BACK}
#define GTP_IRQ_TAB                     {IRQ_TYPE_EDGE_RISING, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_LEVEL_LOW, IRQ_TYPE_LEVEL_HIGH}


// STEP_1(REQUIRED): Define Configuration Information Group(s)
// Sensor_ID Map:
/* sensor_opt1 sensor_opt2 Sensor_ID
    GND         GND         0 
    VDDIO       GND         1 
    NC          GND         2 
    GND         NC/300K     3 
    VDDIO       NC/300K     4 
    NC          NC/300K     5 
*/
// TODO: define your own default or for Sensor_ID == 0 config here. 
// The predefined one is just a sample config, which is not suitable for your tp in most cases.
#define CTP_CFG_GROUP1 {\
    0x46,0xE0,0x01,0x56,0x03,0x02,0xF1,0x01,0x02,0x44,\
    0x00,0x04,0x46,0x32,0x03,0x00,0x00,0x00,0x00,0x00,\
    0x00,0x11,0x04,0x26,0x01,0x74,0x77,0x05,0x00,0x88,\
    0x64,0x0F,0xD0,0x07,0x05,0x07,0x00,0xDA,0x01,0x1D,\
    0x00,0x01,0x08,0x08,0x33,0x33,0x5D,0xAA,0x00,0x00,\
    0x00,0x32,0x96,0x54,0xC5,0x03,0x02,0x00,0x00,0x01,\
    0xC8,0x38,0x00,0xA0,0x45,0x00,0x91,0x57,0x00,0x80,\
    0x6C,0x00,0x61,0x87,0x00,0x61,0x10,0x0B,0x08,0x00,\
    0x51,0x40,0x30,0xFF,0xFF,0x00,0x04,0x00,0x00,0x1E,\
    0x0A,0x00,0x06,0x0B,0x09,0x0F,0x08,0x07,0x01,0x03,\
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
    0x00,0x00,0x00,0x03,0x02,0x05,0x04,0x07,0x06,0x09,\
    0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,\
    0x00,0x00,0x08,0x09,0x0A,0x0D,0x0E,0xFF,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,\
    0x00,0x00,0x00,0xFF,0x0B,0x0C,0xFF,0xFF,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0x6C,0xB2,0xB2,0x6C,0xFF,0x00,\
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x80,\
    0x8C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
    0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
    0x00,0x00,0x00,0x00,0x00,0x00,0xF6,0x01\
    }
    
// TODO: define your config for Sensor_ID == 1 here, if needed
#define CTP_CFG_GROUP2 {\
    }

// TODO: define your config for Sensor_ID == 2 here, if needed
#define CTP_CFG_GROUP3 {\
    }

// TODO: define your config for Sensor_ID == 3 here, if needed
#define CTP_CFG_GROUP4 {\
}
// TODO: define your config for Sensor_ID == 4 here, if needed
#define CTP_CFG_GROUP5 {\
    }

// TODO: define your config for Sensor_ID == 5 here, if needed
#define CTP_CFG_GROUP6 {\
    }

#define CFG_GROUP_LEN(p_cfg_grp)  (sizeof(p_cfg_grp) / sizeof(p_cfg_grp[0]))

static const char *goodix_ts_name = "goodix-ts";
static const unsigned short touch_key_array[] = GTP_KEY_TAB;
#define GTP_MAX_KEY_NUM  (sizeof(touch_key_array)/sizeof(touch_key_array[0]))
static struct workqueue_struct *goodix_wq;
unsigned char grp_cfg_version = 0;
unsigned char config[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
                = {GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff};
#endif
