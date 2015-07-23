#include <linux/init.h>  
#include <linux/module.h>  
#include <linux/delay.h>  
#include <linux/kernel.h>  
#include <linux/moduleparam.h>  
#include <linux/init.h>  
#include <linux/types.h>  
#include <linux/fs.h>  
//#include <mach/gpio.h>  
#include <linux/gpio.h>  
#include <linux/device.h>  
//#include <mach/hardware.h>  
#include <linux/cdev.h>  
#include <asm/uaccess.h>  
#include <linux/errno.h>  
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include <linux/misc/relay.h>

#define DEVICE_NAME		"relay"
#define RELAY_MINOR         40

#define RELAY_PIN1         relay_data[0].relay_pin_number    
#define RELAY_PIN2         relay_data[1].relay_pin_number

#define MOTOR_MAGIC 'L'
#define RELAY1 		_IOW(MOTOR_MAGIC, 0,int)
#define RELAY2		_IOW(MOTOR_MAGIC, 1,int)


struct relay_platform_data *relay_data;

static int relay_driver_open(struct inode *inode,struct file *filp){
	return 0;
}
		

static int relay_driver_ioctl(struct file *filp,unsigned int cmd,unsigned long arg){
	unsigned long relay_pin;
	char state;
	if(cmd == RELAY1){
		gpio_set_value(RELAY_PIN1,arg);
	}else{
		gpio_set_value(RELAY_PIN2,arg);
	}
	
	return 0;
}
static int relay_driver_close(struct inode *inode,struct file *filp){
	return 0;
}

static const struct file_operations relay_driver_fops ={
	.owner 			= THIS_MODULE,
	.open			= relay_driver_open,
	.unlocked_ioctl = relay_driver_ioctl,
	.release		= relay_driver_close,
};

static struct miscdevice relay_driver_misdev = {
	.name = DEVICE_NAME,
	.minor= RELAY_MINOR,
	.fops = &relay_driver_fops,
};


static int __devinit relay_driver_probe(struct platform_device *pdev){
	int ret;

	relay_data = pdev->dev.platform_data;

	ret = gpio_request(RELAY_PIN1,"RELAY_PIN1");
	if(ret != 0){
		printk("ERROR:Can't open the RELAY_PIN1!\n");
	}
	
	ret = gpio_request(RELAY_PIN2,"RELAY_PIN2");
	if(ret != 0){
		printk("ERROR:Can't open the RELAY_PIN2!\n");
	}
	gpio_direction_output(RELAY_PIN1,0);
	gpio_direction_output(RELAY_PIN2,0);

	ret = misc_register(&relay_driver_misdev);
	if(ret != 0){
		printk("RELAY register failed!!\n");
	}else{
		printk("RELAY register success!!\n");
	}
	
	return ret;
}
static int __devexit relay_driver_remove(struct platform_device *pdev){
	int ret;
	ret = misc_deregister(&relay_driver_misdev);
	if(ret != 0){
		printk("RELAY deregister failed!!\n");
	}else{
		printk("RELAY deregister success!!\n");
	}
	gpio_free(RELAY_PIN1);
	gpio_free(RELAY_PIN2);
	return ret;
}


static struct platform_driver relay_driver = {
	.driver = {
		.name 	= DEVICE_NAME,
		.owner = THIS_MODULE,
	},
	.probe = relay_driver_probe,
	.remove= relay_driver_remove,
};

static int __init relay_driver_init(void){
	return platform_driver_register(&relay_driver);
}
static __exit relay_driver_exit(){
	platform_driver_unregister(&relay_driver);
}

module_init(relay_driver_init);
module_exit(relay_driver_exit);
MODULE_LICENSE("GPL");
