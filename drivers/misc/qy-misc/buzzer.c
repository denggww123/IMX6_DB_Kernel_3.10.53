/*
 * buzzer.c -- support  gpio buzzer
 *
 *  Author 		wujiajie
 *  Email   		495398825@qq.com
 *  Create time 	2014-02-12
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/miscdevice.h> 
#include <linux/delay.h> 
#include <asm/irq.h> 
#include <linux/kernel.h> 
#include <linux/module.h> 
#include <linux/init.h> 
#include <linux/mm.h> 
#include <linux/fs.h> 
#include <linux/types.h> 
#include <linux/delay.h> 
#include <linux/moduleparam.h> 
#include <linux/slab.h> 
#include <linux/errno.h> 
#include <linux/ioctl.h> 
#include <linux/cdev.h> 
#include <linux/string.h> 
#include <linux/list.h> 
#include <linux/pci.h> 
#include <linux/gpio.h> 
#include <asm/uaccess.h> 
#include <asm/atomic.h> 
#include <asm/unistd.h> 

#include <linux/version.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/completion.h>

#include <asm/gpio.h>

#include <linux/platform_device.h>
#include <linux/misc/buzzer.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>


#define MOTOR_MAGIC 'L'
#define BUZZER_ON 	_IOW(MOTOR_MAGIC, 0,int)
#define BUZZER_OFF  _IOW(MOTOR_MAGIC, 1,int)

#define DEVICE_NAME				 "qiyang_buzzer"

static struct buzzer_dev
{
	unsigned char state;			// state: 1 on 0 off
	unsigned int pin_number;
}*buzzerp;


static long buzzer_ioctl(struct file *file,  unsigned int cmd,  unsigned long arg) 
{
	unsigned long buzzer_pin;

	if(buzzerp->state != cmd)
	{
		buzzerp->state = (cmd==BUZZER_ON)?1:0;

		buzzer_pin = buzzerp->pin_number;

		gpio_set_value(buzzer_pin,buzzerp->state); 
	}

	return 0;
}


static struct file_operations buzzer_fops = { 
	 .owner = THIS_MODULE, 
	 .unlocked_ioctl = buzzer_ioctl,
}; 

 static struct miscdevice miscbuzzer = 
 {
	 .minor = MISC_DYNAMIC_MINOR,  
 	 .name = DEVICE_NAME, 
 	 .fops = &buzzer_fops, 
 };
 
static int buzzer_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int pin_number;

  struct device_node *np = pdev->dev.of_node;

	pin_number = of_get_named_gpio(np, "bzr-gpios", 0);

	if(!gpio_is_valid(pin_number))
	{
		printk("Buzzer Error : invalid pin number\n");
		return -EINVAL;
	}
		
	ret = gpio_request(pin_number, "buzzer");
	if (ret < 0) 
	{
	  printk("ERROR can not open GPIO %d\n", pin_number);
	  goto exit;
	}
	gpio_direction_output(pin_number,0); 
	
	buzzerp = kmalloc(sizeof(struct buzzer_dev), GFP_KERNEL);
	if(!buzzerp)
	{
		printk("Buzzer: no memory\n");
		ret = -ENOMEM;
		goto exit;
	}

	buzzerp->pin_number = pin_number;
	
	ret = misc_register(&miscbuzzer); 
	
	if(ret < 0)
	{
		printk("Buzzer: misc register error\n");
		goto exit_kfree;
	}

	printk("Buzzer misc register successed: \n");
	
	gpio_set_value(pin_number,1); 	
	msleep(500);		
	gpio_set_value(pin_number,0);

	return ret;
	
exit_kfree:
	kfree(buzzerp);
exit:
	return ret;
	return 0;
}

static int buzzer_remove(struct platform_device *pdev)
{
	gpio_set_value(buzzerp->pin_number,0); 
	gpio_free(buzzerp->pin_number);
	misc_deregister(&miscbuzzer);
	kfree(buzzerp);

	return 0;
}

static const struct of_device_id imx6_buzzer_dt_ids[] = {
	{ .compatible = "fsl,imx6q-qiyang-buzzer", },
	{ /* sentinel */ }
};


static struct platform_driver buzzer_driver = {
	.driver ={
   .name = DEVICE_NAME,
	 .owner = THIS_MODULE,
	 .of_match_table = imx6_buzzer_dt_ids,
	 },
	.probe = buzzer_probe,
	.remove = buzzer_remove,
};


static int __init buzzer_init(void) 
{
	return platform_driver_register(&buzzer_driver);
}

static void __exit buzzer_exit(void)
{
	platform_driver_unregister(&buzzer_driver);
}

module_init(buzzer_init); 

module_exit(buzzer_exit);

MODULE_DESCRIPTION("Driver for QiYang IMX6 Buzzer");
MODULE_AUTHOR("dengww");
MODULE_LICENSE("GPL");
MODULE_ALIAS("gpio:buzzer");


