/* 
 * drivers/input/touchscreen/ft5x06_ts.c
 *
 * FocalTech ft5x06 TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
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
 * VERSION      	DATE			AUTHOR
 *    1.0		  2010-01-05			WenFS
 *
 * note: only support mulititouch	Wenfs 2010-10-01
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include "ft5x06_ts.h"
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>



// add by deng @2015/07/03
static ft5x06_ctrlpins ft5x06_ctrl;

#define GPIO_INT                (ft5x06_ctrl.gpio_inte)
#define GPIO_RESET              (ft5x06_ctrl.gpio_reset)

#define ATTB                GPIO_INT
#define get_attb_value      gpio_get_value

#define RESETPIN_SET0       gpio_direction_output(GPIO_RESET,0)
#define RESETPIN_SET1       gpio_direction_output(GPIO_RESET,1)
#define RESETPIN_REQUEST    gpio_request(GPIO_RESET, "GPIO_RESET")
#define RESETPIN_FREE       gpio_free(GPIO_RESET)

static struct i2c_client *this_client;

//#define CONFIG_FT5X06_MULTITOUCH 1

struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
    u8  touch_point;
};

struct ft5x06_ts_data {
	struct input_dev	*input_dev;
	struct ts_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;
};

static int ft5x06_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	
	return ret;
}

static int ft5x06_read_reg(u8 addr, u8 *pdata)
{
	int ret;
	u8 buf[2] = {addr,0};

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= buf,
		},
	};
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	*pdata = buf[0];
	return ret;
  
}


static unsigned char ft5x06_read_fw_ver(void)
{
	unsigned char ver;
	ft5x06_read_reg(FT5X0X_REG_FIRMID, &ver);
	return(ver);
}


#define CONFIG_SUPPORT_FTS_CTP_UPG


static void ft5x06_ts_release(void)
{
	struct ft5x06_ts_data *data = i2c_get_clientdata(this_client);
#ifdef CONFIG_FT5X06_MULTITOUCH	
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
#else
	input_report_abs(data->input_dev, ABS_PRESSURE, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
#endif
	input_sync(data->input_dev);
}

static int ft5x06_read_data(void)
{
	struct ft5x06_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event; 
	u8 buf[32] = {0};
	int ret = -1;

#ifdef CONFIG_FT5X06_MULTITOUCH 
	  ret = ft5x06_i2c_rxdata(buf, 31);
#else
    ret = ft5x06_i2c_rxdata(buf, 7);
#endif
    if (ret < 0) {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}

	memset(event, 0, sizeof(struct ts_event));
 
	event->touch_point = buf[2] & 0x07;// 000 0111

    if (event->touch_point == 0) {
        ft5x06_ts_release();
        return 1; 
    }

#ifdef CONFIG_FT5X06_MULTITOUCH
    switch (event->touch_point) {
		case 5:
			event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
			event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
		case 4:
			event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
			event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
		case 3:
			event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
			event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
		case 2:
			event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
			event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
		case 1:
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
            break;
		default:
		    return -1;
	}
#else
    if (event->touch_point == 1) {
    	event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
		event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
    }
#endif
    event->pressure = 200;

	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2); 
    return 0;
}

static void ft5x06_report_value(void)
{
	struct ft5x06_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;

#ifdef CONFIG_FT5X06_MULTITOUCH
	switch(event->touch_point) {
		case 5:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y5);
			input_mt_sync(data->input_dev);
		case 4:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x4);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y4);
			input_mt_sync(data->input_dev);
		case 3:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x3);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y3);
			input_mt_sync(data->input_dev);
		case 2:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x2);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y2);
			input_mt_sync(data->input_dev);
		case 1:
		  input_report_abs(data->input_dev, ABS_MT_SLOT, 0);
		  input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, 45);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x1);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y1);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, -1);
			input_sync(data->input_dev);
		default:
			break;
	}
#else	
	if (event->touch_point == 1) {
		input_report_abs(data->input_dev, ABS_X, event->x1);
		input_report_abs(data->input_dev, ABS_Y, event->y1);
  	input_report_abs(data->input_dev, ABS_PRESSURE, event->pressure);
	}
	input_report_key(data->input_dev, BTN_TOUCH, 1);
#endif
	input_sync(data->input_dev);

	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2);
}


static void ft5x06_ts_pen_irq_work(struct work_struct *work)
{
	int ret = -1;
	ret = ft5x06_read_data();	
	if (ret == 0) {	
		ft5x06_report_value();
	}
	else  
    msleep(1);
 
}

static irqreturn_t ft5x06_ts_interrupt(int irq, void *dev_id)
{
      
	struct ft5x06_ts_data *ft5x06_ts = dev_id;

	if (!work_pending(&ft5x06_ts->pen_event_work)) {
	//	queue_work(ft5x06_ts->ts_workqueue, &ft5x06_ts->pen_event_work);
	   schedule_work(&ft5x06_ts->pen_event_work);
	} 
	return IRQ_HANDLED;
}


static int 
ft5x06_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x06_ts_data *ft5x06_ts;
	struct input_dev *input_dev;
	int err = 0;
	unsigned char uc_reg_value; 

  struct device_node *np = client->dev.of_node;
	ft5x06_ctrl.gpio_inte = of_get_named_gpio(np,"inte-gpios",0);
	ft5x06_ctrl.gpio_reset = of_get_named_gpio(np,"reset-gpios",0);

	if((!gpio_is_valid(ft5x06_ctrl.gpio_inte)) || (!gpio_is_valid(ft5x06_ctrl.gpio_reset)))
		{
       printk("Ft5x06 Error : Wrong ctrl pings \n");
			 return -EINVAL;
	  }
    RESETPIN_REQUEST;
    RESETPIN_SET1;

    gpio_set_value(GPIO_RESET,0);
    mdelay(50);
    gpio_set_value(GPIO_RESET,1);
    mdelay(100);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}
	ft5x06_ts = kzalloc(sizeof(*ft5x06_ts), GFP_KERNEL);
	if (!ft5x06_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}


	this_client = client;
	i2c_set_clientdata(client, ft5x06_ts);


	INIT_WORK(&ft5x06_ts->pen_event_work, ft5x06_ts_pen_irq_work);

/*	ft5x06_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ft5x06_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}
*/	
	this_client->irq = gpio_to_irq(GPIO_INT);
	if (this_client->irq < 0) 
	{
		printk("Unable to get irq number for GPIO_NUM\n");
		goto exit_irq_request_failed;
	}
	err = request_irq(this_client->irq, ft5x06_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x06_ts", ft5x06_ts);
	if (err < 0) {
		dev_err(&client->dev, "ft5x06_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	disable_irq(this_client->irq);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	
	ft5x06_ts->input_dev = input_dev;

#ifdef CONFIG_FT5X06_MULTITOUCH
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);      
        
#else
	set_bit(ABS_X, input_dev->absbit);
	set_bit(ABS_Y, input_dev->absbit);
	set_bit(ABS_PRESSURE, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, PRESS_MAX, 0 , 0);
#endif

	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"ft5x06_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

    msleep(50);
    uc_reg_value = ft5x06_read_fw_ver();
    printk("[FST] Firmware version = 0x%x\n", uc_reg_value); 
    enable_irq(this_client->irq);

    return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed: 
	free_irq(this_client->irq, ft5x06_ts);
exit_irq_request_failed:
	cancel_work_sync(&ft5x06_ts->pen_event_work);
//	destroy_workqueue(ft5x06_ts->ts_workqueue);
//exit_create_singlethread: 
	i2c_set_clientdata(client, NULL);
	kfree(ft5x06_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int ft5x06_ts_remove(struct i2c_client *client)
{
	struct ft5x06_ts_data *ft5x06_ts = i2c_get_clientdata(client);
	unregister_early_suspend(&ft5x06_ts->early_suspend);
	free_irq(this_client->irq, ft5x06_ts);
	gpio_free(GPIO_RESET);
	input_unregister_device(ft5x06_ts->input_dev);
	kfree(ft5x06_ts);
	cancel_work_sync(&ft5x06_ts->pen_event_work);
//	destroy_workqueue(ft5x06_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id ft5x06_ts_id[] = {
	{ FT5X0X_NAME, 0 },{ }
};

static const struct of_device_id ft5x06_dt_ids[] = {
  	{.compatible = "fsl,ft5x06_ts-i2c",},
  	{/* sentinel */},
};

MODULE_DEVICE_TABLE(of, ft5x06_dt_ids);

MODULE_DEVICE_TABLE(i2c, ft5x06_ts_id);


static struct i2c_driver ft5x06_ts_driver = {
	.probe		= ft5x06_ts_probe,
	.remove		= ft5x06_ts_remove,
	.id_table	= ft5x06_ts_id,
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = ft5x06_dt_ids,
	},
};

static int __init ft5x06_ts_init(void)
{
	int ret;
	ret = i2c_add_driver(&ft5x06_ts_driver);
	return ret;
}

static void __exit ft5x06_ts_exit(void)
{
	i2c_del_driver(&ft5x06_ts_driver);
}

module_init(ft5x06_ts_init);
module_exit(ft5x06_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x06 TouchScreen driver");
MODULE_LICENSE("GPL");

