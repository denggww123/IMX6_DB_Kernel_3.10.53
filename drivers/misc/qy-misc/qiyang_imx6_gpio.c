/*
 * gpio driver for imx6
 *
 * Copyright (C) 2013  Hangzhou QiYang inc.
 * John Yao <yaobr@qiyangtech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/platform_device.h>


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <asm/uaccess.h>

typedef struct {
	int  gpio_index;
	int  gpio_state;
} qiyang_ioctl_arg_gpio;

/* ioctl cmd */
#define	IOCTL_GPIO_LEVEL_GET	        _IOW('Q', 0x02, int)  //get gpio state
#define	IOCTL_GPIO_LEVEL_SET          _IOR('Q', 0x03, int)	//set gpio output state
#define	IOCTL_GPIO_DIR_INPUT_SET	    _IOW('Q', 0x04, int)	//set gpio as a input
        
#define GPIO_NAME_MAX                   20

#define GPIO_LEVEL_LOW                  0
#define GPIO_LEVEL_HIGHT                1


unsigned int usb_rst_pin;
unsigned int phy_rst_pin;

static int imx6_gpio_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int request_one_gpio(qiyang_ioctl_arg_gpio *gpio)
{
    char gpio_name[GPIO_NAME_MAX]; 

    gpio_name[0] = '\0';
    snprintf(gpio_name, sizeof(gpio_name), "user_gpio_%d", gpio->gpio_index);
    
    if (gpio_request(gpio->gpio_index, gpio_name)) {
		printk(KERN_ERR "imx6_gpio:request gpio %d failed.\n", gpio->gpio_index);
        return -EBUSY;
    }

    return 0;
}

static void free_requested_gpio(qiyang_ioctl_arg_gpio *gpio)
{
    gpio_free(gpio->gpio_index);
}

static int get_gpio_value(qiyang_ioctl_arg_gpio *gpio, unsigned long arg)
{
    int ret;
    if (request_one_gpio(gpio)){
        return -EBUSY;
    }

    ret = gpio_direction_input(gpio->gpio_index);
		if(ret < 0)
		printk(KERN_ERR"imx6_gpio:gpio %d set input error\n", gpio->gpio_index);
		
    gpio->gpio_state = gpio_get_value(gpio->gpio_index);

    free_requested_gpio(gpio);
    if (copy_to_user((qiyang_ioctl_arg_gpio *)arg, gpio, sizeof (qiyang_ioctl_arg_gpio))){
		printk(KERN_ERR "imx6_gpio:copy datas to user space failed!\n");
        return -EINVAL;
    }
    
    return 0;
}

static int set_gpio_value(qiyang_ioctl_arg_gpio *gpio)
{
    if ((GPIO_LEVEL_LOW != gpio->gpio_state) && (GPIO_LEVEL_HIGHT != gpio->gpio_state)){
    		printk(KERN_ERR "imx6_gpio:gpio arg gpio_state invalid:%d\n", gpio->gpio_state);
            return -EINVAL;
    }
    
    if (request_one_gpio(gpio)){
        return -EBUSY;
    }
	
    gpio_direction_output(gpio->gpio_index, 0);
	  gpio_set_value(gpio->gpio_index, gpio->gpio_state);

    free_requested_gpio(gpio);

    return 0;
}

static long imx6_gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
	  qiyang_ioctl_arg_gpio pin;

    if (0 == arg)
        return -EINVAL;
    
    memset(&pin, 0, sizeof(pin));
    if (copy_from_user(&pin, (qiyang_ioctl_arg_gpio *)arg, sizeof (qiyang_ioctl_arg_gpio))){
		printk(KERN_ERR "imx6_gpio:copy datas from user failed\n");
        return -EINVAL;
    }
    
    if (!gpio_is_valid(pin.gpio_index)){
		printk(KERN_ERR "imx6_gpio: gpio arg gpio_index is invalid.\n");
        return -EINVAL;
    }

	switch(cmd) {
    	case IOCTL_GPIO_LEVEL_GET:
    		ret = get_gpio_value(&pin, arg);
    		break;
            
    	case IOCTL_GPIO_LEVEL_SET:
    		ret = set_gpio_value(&pin);
            break;
            
    	default:
			printk(KERN_ERR "imx6_gpio:unsupport ioctl cmd!\n");
            ret = -EINVAL;
    		break;
	}
    
	return ret;
}

static int imx6_gpio_release(struct inode *node, struct file *file)
{
    return 0;
}

static const struct file_operations gpio_fops = {
	.owner		= THIS_MODULE,
  .unlocked_ioctl = imx6_gpio_ioctl,
	.open		= imx6_gpio_open,
	.release	= imx6_gpio_release,
};

static struct miscdevice gpio_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "qiyang_imx6_gpio",
	.fops		= &gpio_fops,
};

static int imx6_gpio_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;
	usb_rst_pin = of_get_named_gpio(np,"usb-rst-gpios",0);
	phy_rst_pin = of_get_named_gpio(np,"phy-rst-gpios",0);

  ret = misc_register(&gpio_dev);
  if (ret != 0) {
	printk(KERN_ERR "imx6_gpio:misc register failed: %d\n", ret);
	return -ENODEV;
	}

	if(!gpio_is_valid(usb_rst_pin))  //add by deng @2015/7/23
		{
     printk("QiYang_GPIO Error : invalid usb rst pin number\n");
		 return -EINVAL;
	  }

	if(!gpio_is_valid(phy_rst_pin))
		{
     printk("QiYang_GPIO Error : invalid phy rst pin number\n");
		 return -EINVAL;
	  }
  
   gpio_request(usb_rst_pin,"usb_rst_pin");
	 gpio_request(phy_rst_pin,"phy_rst_pin");

	 gpio_direction_output(usb_rst_pin,0);
	 gpio_direction_output(phy_rst_pin,0);
	 msleep(30);
	 gpio_direction_output(usb_rst_pin,1);
	 gpio_direction_output(phy_rst_pin,1);

	return 0;
}

static int imx6_gpio_remove(struct platform_device *pdev)
{
	misc_deregister(&gpio_dev);
	gpio_free(usb_rst_pin);
	gpio_free(phy_rst_pin);

	return 0;
}

static const struct of_device_id imx6_gpio_dt_ids[] = {
	{ .compatible = "fsl,imx6q-qiyang-gpio" }, 
	{ /* sentinel */ },	
};

static struct platform_driver imx6_gpio_driver = {
	.probe		= imx6_gpio_probe,
	.remove		= imx6_gpio_remove,
	.driver		= {
		.name	= "qiyang_imx6_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = imx6_gpio_dt_ids,
	},
};

static int __init imx6_gpio_init(void)
{
  int ret;

	ret = platform_driver_register(&imx6_gpio_driver);
	if (ret)
		printk(KERN_ERR "imx6_gpio: probe failed: %d\n", ret);

	return ret;
}

static void __exit imx6_gpio_exit(void)
{
	platform_driver_unregister(&imx6_gpio_driver);
}

module_init(imx6_gpio_init);
module_exit(imx6_gpio_exit);

MODULE_AUTHOR("dengww");
MODULE_DESCRIPTION("Imx6 user gpio driver");
MODULE_LICENSE("GPL");


