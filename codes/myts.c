#include "myts.h"

static struct regulator *analog_regulator;
#define OV5640_VOLTAGE_ANALOG               2800000
static struct my_ts_data *gt9xx_ts_parse_devtree(struct i2c_client *client)  
{  
	struct device *dev = &client->dev;  
	struct device_node *node;  
	struct my_ts_data *pdata;  

	node = dev->of_node;  
	if (!node) {  
		dev_err(dev, "The of_node is NULL.\n");  
		return ERR_PTR(-ENODEV);  
	}  

	pdata = devm_kzalloc(dev, sizeof(struct device_node), GFP_KERNEL);  
	if (!pdata) {  
		dev_err(dev, "No enough memory left.\n");  
		return ERR_PTR(-ENOMEM);  
	}  

	/* pinctrl-1 */
	pdata->rst_pin = of_get_gpio(node, 1);  
	if (gpio_is_valid(pdata->rst_pin))
		printk("myts rst pin valid\n");
	else
		printk("myts rst pin not valid\n");

	/* pinctrl-0 */
	pdata->int_pin = of_get_gpio(node, 0);  
	if (gpio_is_valid(pdata->int_pin))
		printk("myts int pin valid\n");
	else
		printk("myts int pin not valid\n");

	printk("myts rst = %d, int = %d\n", pdata->rst_pin, pdata->int_pin);

	return pdata;  
}  

void my_int_sync(struct i2c_clinet *client, int ms)
{
	struct my_ts_data *pdata = (struct my_ts_data*)i2c_get_clientdata(client);  

    gpio_direction_output(pdata->int_pin, 0);
    msleep(ms);
    gpio_direction_input(pdata->int_pin);
}

void my_reset_guitar(struct i2c_client *client, int ms)
{
	struct my_ts_data *pdata = (struct my_ts_data*)i2c_get_clientdata(client);  
	int gpio;
	int ret;
	struct device_node *np = client->dev.of_node;

    printk("Guitar reset\n");

    gpio_direction_output(pdata->rst_pin, 0);   // begin select I2C slave addr
    msleep(ms);                         // T2: > 10ms
    gpio_direction_output(pdata->int_pin, 1);

    msleep(2);                          // T3: > 100us
    gpio_direction_output(pdata->rst_pin, 1);
    
    msleep(6);                          // T4: > 5ms

    gpio_direction_input(pdata->rst_pin);    // end select I2C slave addr

    my_int_sync(client, 50);  
}

int my_i2c_write(struct i2c_client *client, unsigned char *buf,int len)
{
    struct i2c_msg msg;
    int ret = -1;
    int retries = 0;

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len;
    msg.buf   = buf;

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret == 1)break;
        retries++;
    }
    if((retries >= 5))
    {
        printk("I2C Write: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);
		my_reset_guitar(client, 10);  
    }
    return ret;
}

int my_i2c_read(struct i2c_client *client, unsigned char *buf, int len)
{
    struct i2c_msg msgs[2];
    int ret = -1;
    int retries = 0;

    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = client->addr;
    msgs[0].len   = 2;
    msgs[0].buf   = &buf[0];
    
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = client->addr;
    msgs[1].len   = len - 2;
    msgs[1].buf   = &buf[2];

    while (retries < 5)
    {
        ret = i2c_transfer(client->adapter, msgs, 2);
        if(ret == 2)
			break;
        retries++;
    }

    if(retries >= 5)
	{
        printk("I2C Read: 0x%04X, %d bytes failed, errcode: %d", (((unsigned short)(buf[0] << 8)) | buf[1]), len-2, ret);
		my_reset_guitar(client, 10);  
	}
    return ret;
}

static const struct i2c_device_id myts_id[] = {
    { "Goodix-TS", 0 },
    { }
};

static int myts_remove(struct i2c_client *client)
{
    struct my_ts_data *ts = i2c_get_clientdata(client);
    struct my_ts_data *pdata = ts;
	printk("%s, %d\n", __FUNCTION__, __LINE__);
    
	gpio_direction_input(pdata->int_pin);
	gpio_free(pdata->int_pin);
	free_irq(client->irq, ts);
    
    i2c_set_clientdata(client, NULL);
    input_unregister_device(ts->input_dev);

    return 0;
}

static void gtp_touch_down(struct my_ts_data* ts,int id,int x,int y,int w)
{
    input_report_key(ts->input_dev, BTN_TOUCH, 1);
    input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
    input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
    input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
    input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
    input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_event(ts->input_dev, EV_SYN, SYN_MT_REPORT, 0);
}

static void my_ts_work_func(struct work_struct *work)
{
    unsigned char  end_cmd[3] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF, 0};
    unsigned char  point_data[2 + 1 + 8 * 5 + 1]={GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF};
    unsigned char  touch_num = 0; /* 按压触摸屏的手指数 */
    unsigned char  finger = 0;
    unsigned char* coor_data = NULL;
    int input_x = 0;
    int input_y = 0;
    int input_w = 0;
    int id = 0;
    int ret = -1;
    struct my_ts_data *ts = NULL;

    ts = container_of(work, struct my_ts_data, work);

    ret = my_i2c_read(ts->client, point_data, 12);
    if (ret < 0)
    {
        printk("I2C transfer error. errno:%d\n ", ret);
		enable_irq(ts->client->irq);
        return;
    }
   
	/* PINTDATA 的最后一个数据表示的是手指的数量 */ 
    finger = point_data[GTP_ADDR_LENGTH];

	/* 没有手指按下什么都不出处理 */
    if (finger == 0x00)
	{
		enable_irq(ts->client->irq);
        return;
	}

	/* 手指已经离开触摸屏 */
    if((finger & 0x80) == 0)
        goto exit_work_func;

	/* 低4位表示手指头数目 */
    touch_num = finger & 0x0f;

	/* 有手指按下 */
    if (touch_num)
    {
		printk("%d fingers touch down\n", touch_num);
		coor_data = &point_data[3];

		id = coor_data[0] & 0x0F;
		input_x  = coor_data[1] | (coor_data[2] << 8);
		input_y  = coor_data[3] | (coor_data[4] << 8);
		input_w  = coor_data[5] | (coor_data[6] << 8);
		printk("input_x = %d, input_y = %d, input_w = 0x%08x\n", input_x, input_y, input_w);
		/* 上报按压点的X，Y和压力值 */
		gtp_touch_down(ts, id, input_x, input_y, input_w);
    }
	/* 手指离开了触摸屏 */
	else
	{
		printk("finger touch up\n");
    	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	}

	input_event(ts->input_dev, EV_SYN, SYN_REPORT, 0);

exit_work_func:
	ret = my_i2c_write(ts->client, end_cmd, 3);
	if (ret < 0)
		printk("I2C write end_cmd error!");
	enable_irq(ts->client->irq);
}

static char my_request_io_port(struct i2c_client *client)
{
	int error;
    struct my_ts_data *pdata = i2c_get_clientdata(client);

    int ret = 0;
	ret = devm_gpio_request(&client->dev, pdata->int_pin, "gt9xx_int_pin");  
    if (ret < 0) 
    {
        printk("Failed to request GPIO:%d, ERRNO:%d", (int)pdata->int_pin, ret);
        ret = -ENODEV;
    }
    else
    {
        gpio_direction_output(pdata->int_pin, 1);  
		gpio_set_value(pdata->int_pin, 1);
        gpio_direction_input(pdata->int_pin);  
        pdata->client->irq = gpio_to_irq(pdata->int_pin);
    }

	ret = devm_gpio_request(&client->dev, pdata->rst_pin, "gt9xx_rst_pin");  
    if (ret < 0) 
    {
        printk("Failed to request GPIO:%d, ERRNO:%d",(int)pdata->rst_pin,ret);
        ret = -ENODEV;
    }

    gpio_direction_input(pdata->rst_pin);

    gpio_direction_output(pdata->rst_pin, 0);   // begin select I2C slave addr
    msleep(20);                         // T2: > 10ms
    gpio_direction_output(pdata->int_pin, 1);
    msleep(2);                          // T3: > 100us
    gpio_direction_output(pdata->rst_pin, 1);
    msleep(6);                          // T4: > 5ms
    gpio_direction_input(pdata->rst_pin);    // end select I2C slave addr

    gpio_direction_output(pdata->int_pin, 0);
    msleep(50);
    gpio_direction_input(pdata->int_pin);
    
    return error;
}

int gtp_send_cfg(struct i2c_client *client)
{
    int ret = 2;

    int retry = 0;
    struct my_ts_data *ts = i2c_get_clientdata(client);

    if (ts->fixed_cfg)
        return 0;
    else if (ts->pnl_init_error)
        return 0;
    
    for (retry = 0; retry < 5; retry++)
    {
        ret = my_i2c_write(client, config , GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);
        if (ret > 0)
            break;
    }

    return ret;
}

int my_i2c_read_dbl_check(struct i2c_client *client, u16 addr, unsigned char *rxbuf, int len)
{
    unsigned char buf[16] = {0};
    unsigned char confirm_buf[16] = {0};
    unsigned char retry = 0;
    
    while (retry++ < 3)
    {
        memset(buf, 0xAA, 16);
        buf[0] = (unsigned char)(addr >> 8);
        buf[1] = (unsigned char)(addr & 0xFF);
        my_i2c_read(client, buf, len + 2);
        
        memset(confirm_buf, 0xAB, 16);
        confirm_buf[0] = (unsigned char)(addr >> 8);
        confirm_buf[1] = (unsigned char)(addr & 0xFF);
        my_i2c_read(client, confirm_buf, len + 2);
        
        if (!memcmp(buf, confirm_buf, len+2))
        {
            memcpy(rxbuf, confirm_buf+2, len);
            return 1;
        }
    }    
    printk("I2C read 0x%04X, %d bytes, double check failed!", addr, len);
    return FAIL;
}

static int gtp_get_info(struct my_ts_data *ts)
{
    unsigned char opr_buf[6] = {0};
    int ret = 0;
    
    ts->abs_x_max = GTP_MAX_WIDTH;
    ts->abs_y_max = GTP_MAX_HEIGHT;
    ts->int_trigger_type = GTP_INT_TRIGGER;
        
    opr_buf[0] = (unsigned char)((GTP_REG_CONFIG_DATA+1) >> 8);
    opr_buf[1] = (unsigned char)((GTP_REG_CONFIG_DATA+1) & 0xFF);
    
    ret = my_i2c_read(ts->client, opr_buf, 6);
    if (ret < 0)
        return 0;
    
    ts->abs_x_max = (opr_buf[3] << 8) + opr_buf[2];
    ts->abs_y_max = (opr_buf[5] << 8) + opr_buf[4];
    
    opr_buf[0] = (unsigned char)((GTP_REG_CONFIG_DATA+6) >> 8);
    opr_buf[1] = (unsigned char)((GTP_REG_CONFIG_DATA+6) & 0xFF);
    
    ret = my_i2c_read(ts->client, opr_buf, 3);
    if (ret < 0)
        return 0;

    ts->int_trigger_type = opr_buf[2] & 0x03;
    
    printk("X_MAX = %d, Y_MAX = %d, TRIGGER = 0x%02x",
            ts->abs_x_max,ts->abs_y_max,ts->int_trigger_type);
    
    return 1;    
}

static int my_init_panel(struct my_ts_data *ts)
{
    int ret = -1;

    int i = 0;
    unsigned char check_sum = 0;
    unsigned char opr_buf[16] = {0};
    unsigned char sensor_id = 0; 
    
    unsigned char cfg_info_group1[] = CTP_CFG_GROUP1;
    unsigned char cfg_info_group2[] = CTP_CFG_GROUP2;
    unsigned char cfg_info_group3[] = CTP_CFG_GROUP3;
    unsigned char cfg_info_group4[] = CTP_CFG_GROUP4;
    unsigned char cfg_info_group5[] = CTP_CFG_GROUP5;
    unsigned char cfg_info_group6[] = CTP_CFG_GROUP6;
    unsigned char *send_cfg_buf[] = {cfg_info_group1, cfg_info_group2, cfg_info_group3,
                        cfg_info_group4, cfg_info_group5, cfg_info_group6};
    unsigned char cfg_info_len[] = { CFG_GROUP_LEN(cfg_info_group1),
                          CFG_GROUP_LEN(cfg_info_group2),
                          CFG_GROUP_LEN(cfg_info_group3),
                          CFG_GROUP_LEN(cfg_info_group4),
                          CFG_GROUP_LEN(cfg_info_group5),
                          CFG_GROUP_LEN(cfg_info_group6)};

        ret = my_i2c_read_dbl_check(ts->client, 0x41E4, opr_buf, 1);
        if (1 == ret) 
        {
            if (opr_buf[0] != 0xBE)
            {
                ts->fw_error = 1;
                printk("Firmware error, no config sent!");
                return -1;
            }
        }

    if ((!cfg_info_len[1]) && (!cfg_info_len[2]) && 
        (!cfg_info_len[3]) && (!cfg_info_len[4]) && 
        (!cfg_info_len[5]))
    {
        sensor_id = 0; 
    }
    else
    {
        ret = my_i2c_read_dbl_check(ts->client, GTP_REG_SENSOR_ID, &sensor_id, 1);
        if (1 == ret)
        {
            if (sensor_id >= 0x06)
            {
                printk("Invalid sensor_id(0x%02X), No Config Sent!", sensor_id);
                ts->pnl_init_error = 1;
                return -1;
            }
        }
        else
        {
            printk("Failed to get sensor_id, No config sent!");
            ts->pnl_init_error = 1;
            return -1;
        }
        printk("Sensor_ID: %d", sensor_id);
    }
    ts->gtp_cfg_len = cfg_info_len[sensor_id];
    printk("CTP_CONFIG_GROUP%d used, config length: %d", sensor_id + 1, ts->gtp_cfg_len);
    
    if (ts->gtp_cfg_len < GTP_CONFIG_MIN_LENGTH)
    {
        printk("Config Group%d is INVALID CONFIG GROUP(Len: %d)! NO Config Sent! You need to check you header file CFG_GROUP section!", sensor_id+1, ts->gtp_cfg_len);
        ts->pnl_init_error = 1;
        return -1;
    }

    {
        ret = my_i2c_read_dbl_check(ts->client, GTP_REG_CONFIG_DATA, &opr_buf[0], 1);
        
        if (ret == 1)
        {
            printk("CFG_GROUP%d Config Version: %d, 0x%02X; IC Config Version: %d, 0x%02X", sensor_id+1, 
                        send_cfg_buf[sensor_id][0], send_cfg_buf[sensor_id][0], opr_buf[0], opr_buf[0]);
            
            if (opr_buf[0] < 90)    
            {
                grp_cfg_version = send_cfg_buf[sensor_id][0];
                send_cfg_buf[sensor_id][0] = 0x00;
                ts->fixed_cfg = 0;
            }
            else// treated as fixed config, not send config
            {
                printk("Ic fixed config with config version(%d, 0x%02X)", opr_buf[0], opr_buf[0]);
                ts->fixed_cfg = 1;
                gtp_get_info(ts);
                return 0;
            }
        }
        else
        {
            printk("Failed to get ic config version!No config sent!");
            return -1;
        }
    }
    
    memset(&config[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
    memcpy(&config[GTP_ADDR_LENGTH], send_cfg_buf[sensor_id], ts->gtp_cfg_len);

    config[RESOLUTION_LOC]     = (unsigned char)GTP_MAX_WIDTH;
    config[RESOLUTION_LOC + 1] = (unsigned char)(GTP_MAX_WIDTH>>8);
    config[RESOLUTION_LOC + 2] = (unsigned char)GTP_MAX_HEIGHT;
    config[RESOLUTION_LOC + 3] = (unsigned char)(GTP_MAX_HEIGHT>>8);
    
    if (GTP_INT_TRIGGER == 0)  //RISING
    {
        config[TRIGGER_LOC] &= 0xfe; 
    }
    else if (GTP_INT_TRIGGER == 1)  //FALLING
    {
        config[TRIGGER_LOC] |= 0x01;
    }
    
    check_sum = 0;
    for (i = GTP_ADDR_LENGTH; i < ts->gtp_cfg_len; i++)
    {
        check_sum += config[i];
    }
    config[ts->gtp_cfg_len] = (~check_sum) + 1;

    ts->gtp_cfg_len = GTP_CONFIG_MAX_LENGTH;
    ret = my_i2c_read(ts->client, config, ts->gtp_cfg_len + GTP_ADDR_LENGTH);
    if (ret < 0)
    {
        printk("Read Config Failed, Using Default Resolution & INT Trigger!");
        ts->abs_x_max = GTP_MAX_WIDTH;
        ts->abs_y_max = GTP_MAX_HEIGHT;
        ts->int_trigger_type = GTP_INT_TRIGGER;
    }
    
    if ((ts->abs_x_max == 0) && (ts->abs_y_max == 0))
    {
        ts->abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
        ts->abs_y_max = (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
        ts->int_trigger_type = (config[TRIGGER_LOC]) & 0x03; 
    }

	if (gtp_send_cfg(ts->client) < 0)
		printk("Send config error.");

	// set config version to CTP_CFG_GROUP, for resume to send config
	config[GTP_ADDR_LENGTH] = grp_cfg_version;
	check_sum = 0;

	for (i = GTP_ADDR_LENGTH; i < ts->gtp_cfg_len; i++)
		check_sum += config[i];

	config[ts->gtp_cfg_len] = (~check_sum) + 1;

	printk("X_MAX: %d, Y_MAX: %d, TRIGGER: 0x%02x", ts->abs_x_max,ts->abs_y_max,ts->int_trigger_type);
    
    msleep(10);

    return 0;
}

static char gtp_request_input_dev(struct my_ts_data *ts)
{
    char ret = -1;
    char phys[32];
    unsigned char index = 0;
  
    ts->input_dev = input_allocate_device();
    if (ts->input_dev == NULL)
    {
        printk("Failed to allocate input device.");
        return -ENOMEM;
    }

    ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
    ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
    __set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);

    for (index = 0; index < GTP_MAX_KEY_NUM; index++)
        input_set_capability(ts->input_dev, EV_KEY, touch_key_array[index]);  

    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

    sprintf(phys, "input/ts");
    ts->input_dev->name = goodix_ts_name;
    ts->input_dev->phys = phys;
    ts->input_dev->id.bustype = BUS_I2C;
    ts->input_dev->id.vendor = 0xDEAD;
    ts->input_dev->id.product = 0xBEEF;
    ts->input_dev->id.version = 10427;
    
    ret = input_register_device(ts->input_dev);
    if (ret)
    {
        printk("Register %s input device failed", ts->input_dev->name);
        return -ENODEV;
    }
    
    return 0;
}

static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
    struct my_ts_data *ts = dev_id;
 	disable_irq_nosync(ts->client->irq);
    queue_work(goodix_wq, &ts->work);
    return IRQ_HANDLED;
}

static int myts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = -1;
	int error;
	struct device *dev = &client->dev;
	struct my_ts_data *pdata = dev_get_platdata(dev);
  
	/* get power */
	analog_regulator = devm_regulator_get(dev, "VDD");
	if (!IS_ERR(analog_regulator)) {
		regulator_set_voltage(analog_regulator,
				      OV5640_VOLTAGE_ANALOG,
				      OV5640_VOLTAGE_ANALOG);
		ret = regulator_enable(analog_regulator);
		if (ret) {
			pr_err("%s:analog set voltage error\n",
				__func__);
			return ret;
		} else {
			printk("%s:analog set voltage ok\n", __func__);
		}
	} else {
		analog_regulator = NULL;
		pr_err("%s: cannot get analog voltage error\n", __func__);
	}

	if (!pdata) {  
		pdata = gt9xx_ts_parse_devtree(client);  
		if (IS_ERR(pdata)) {  
			dev_err(dev, "Get device data from device tree failed!\n");  
		}  
	}  

	pdata->client = client;  
	i2c_set_clientdata(client, pdata);  

    printk("I2C Address=> 0x%02x\n", client->addr);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
    {
        printk("I2C check functionality failed.");
        return -ENODEV;
    }

	/* config my ts data struct */
    INIT_WORK(&pdata->work, my_ts_work_func);
    pdata->client = client;
    i2c_set_clientdata(client, pdata);

	/* IO request */
    ret = my_request_io_port(client);
    if (ret < 0)
    {
        printk("GTP request IO port failed.");
        return ret;
    }

	/* panel init, it's a shit part!! ?? */ 
    ret = my_init_panel(pdata);
    if (ret < 0)
	{
		printk("init panel ERROR\n");
		return ret;
	}

    ret = gtp_request_input_dev(pdata);
    if (ret < 0)
        printk("GTP request input dev failed");
    

	error = request_threaded_irq(client->irq, NULL, goodix_ts_irq_handler, IRQF_TRIGGER_LOW | IRQF_ONESHOT, "myts", pdata);
	if (error < 0) {
		dev_err(&client->dev, "********Failed to register interrupt\n");
		//goto err_free_dev;
	}

	printk("request irq OK!!\n");

 	disable_irq_nosync(pdata->int_pin);
    
    return 0;
}

static const struct of_device_id gt9xx_ts_of_match[] = {  
	{ .compatible = "myts_gt9xx", .data = NULL },  
	{ }  
};
MODULE_DEVICE_TABLE(of, gt9xx_ts_of_match);

static struct i2c_driver myts_driver = {
    .probe      = myts_probe,
    .remove     = myts_remove,
    .id_table   = myts_id,
    .driver = {
        .name     = "Goodix-TS",
        .owner    = THIS_MODULE,
		.of_match_table = gt9xx_ts_of_match,
    },
};

static int myts_init(void)
{
    int ret;

	goodix_wq = create_singlethread_workqueue("goodix_wq");
    if (!goodix_wq)
    {
        printk("Creat workqueue failed.");
        return -ENOMEM;
    }

    ret = i2c_add_driver(&myts_driver);
    return ret; 
}

static void myts_exit(void)
{
    i2c_del_driver(&myts_driver);
	destroy_workqueue(goodix_wq);
}

module_init(myts_init);
module_exit(myts_exit);

MODULE_DESCRIPTION("GTP Series Driver");
MODULE_LICENSE("GPL");
