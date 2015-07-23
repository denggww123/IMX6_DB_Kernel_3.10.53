//##########################################################################
//#                                                                        #
//#        Copyright (C) 2011 by Beijing IRTOUCHSYSTEMS Co., Ltd           #
//#                                                                        #
//#        IRTOUCH Optical USB Touchscreen                                 #    
//#                                                                        #
//#        Create by Yin Xingjie                                             #
//#                                                                        #
//#        Email: yinxj@unitop.cc                                             #
//#                                                                        #
//##########################################################################
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/cdev.h>
#include <asm/uaccess.h> 

/*
 * Version Information
 */
#define DRIVER_VERSION "v14.2.0.0002 for Linux"
#define DRIVER_AUTHOR "Beijing IRTOUCH SYSTEMS Co., Ltd"
#define DRIVER_DESC "IRTOUCH USB touchscreen driver for dual point"
#define DRIVER_LICENSE "GPL"
 

//Device Protocol
#define SET_COMMAND 0x01
#define GET_COMMAND 0x02
#define SET_COMMAND_MODE 0x71
#define SET_WORK_MODE	0x93
#define GET_FIRMWARE_VER 0x32
#define GET_SERIAL_NUMBER 0x17
//#define GET_SERIAL_NUMBER 0x29
#define GET_BIOS_VER	0x07
#define	GET_CALIB_X	0x2D
#define	SET_CALIB_X	0x2E
#define	GET_CALIB_Y	0x2B
#define	SET_CALIB_Y	0x2C
#define	GET_DEVICE_CONFIG	0x30
#define	SET_DEVICE_CONFIG	0x31
#define	GET_DEVICE_SN	0x34
#define	GET_DEVICE_BUS	0x1C


#define COMMAND_RETRY_COUNT  5

#define MAJOR_DEVICE_NUMBER 47
#define MINOR_DEVICE_NUMBER 192
#define USB_TRANSFER_LENGTH			64
#define TOUCH_DEVICE_NAME "irtouch"

//Control code for calibration
#define CTLCODE_GET_COORDINATE 0xC0
#define CTLCODE_SET_CALIB_PARA_X 0xC1
#define CTLCODE_SET_CALIB_PARA_Y 0xC2
#define CTLCODE_GET_CALIB_PARA_X 0xC3
#define CTLCODE_GET_CALIB_PARA_Y 0xC4
#define CTLCODE_START_CALIB 0xC5
#define CTLCODE_GET_CALIB_STATUS 0xC6
#define CTLCODE_DEVICE_RESET 0xC7
#define CTLCODE_GET_SCR_PARA 0xc8
#define CTLCODE_GET_PRODUCT_SN 0xc9
#define CTLCODE_GET_PRODUCT_ID 0xca
#define CTLCODE_SET_SCAN_MODE 0xcb

//Control code for firmware update
#define CTLCODE_GET_FIRMWARE_VERSION 0xD0
#define CTLCODE_GET_SERIAL_NUMBER 0xD1
#define CTLCODE_SND_MSG 0xD2
#define CTLCODE_RCV_MSG 0xD3
#define CTLCODE_GET_BIOS_VERSION 0xD4
#define CTLCODE_GET_DRIVER_VERSION 0xD5

#define touch_err(format, arg...)					\
	printk(KERN_ERR KBUILD_MODNAME ": " format "\n", ##arg)

#pragma pack(1)
struct irtouch_point
{
	unsigned char	identifier;
	short		x;
	short		y;
	short		z;
	short		width;
	short		height;
	unsigned char	status;
	unsigned char	reserver;
};

struct irtouch_pkt
{
	unsigned char	startBit;
	unsigned char	commandType;
	unsigned char	command;
	unsigned char	pkgLength;
	unsigned char	deviceId;
	struct irtouch_point	points[3];
	unsigned char	actualCounter;
	unsigned char	CRC;	
};

struct points
{
	int	x;
	int	y;
};

struct calib_param{
	long	A00;
	long	A01;
	long	A10;
	unsigned char reserve[46];
};

struct	device_config{
	unsigned char deviceID;
	unsigned char monitorID;
	unsigned char reserved[8];
	unsigned char calibrateStatus;
	unsigned char reserved1[47];
};

struct AlgContext
{
	bool	bMultiPoints;
	int	actualCounter;
	struct irtouch_point inPoint[4];
	struct irtouch_point outPoint[2];
};

#pragma pack()

struct irtouchusb {
	char name[128];
	char phys[64];
	short productId;
	struct usb_device *usbdev;
	struct input_dev *dev;
	struct urb *irq;

	struct irtouch_pkt * irpkt;
	dma_addr_t data_dma;
};

struct device_context{
	bool startCalib;
	bool isCalibX;
	bool isCalibY;
	char biosVer[10];
	char firmwareVer[10];
	char serialNumber[33];
	u16  productid;
	struct points points;
	struct AlgContext algCtx;
	struct calib_param calibX;
	struct calib_param calibY;
	struct device_config devConfig;
};

struct cdev irtouch_cdev;
struct device_context *irtouchdev_context;
static struct class *irser_class;
static struct usb_driver irtouch_driver;
static struct file_operations irtouch_fops;

static struct usb_class_driver irtouch_class_driver = {
	.name = "irtouch",
	.fops = &irtouch_fops,
	.minor_base = MINOR_DEVICE_NUMBER,
};

/*
static void irtouch_translatePoint(struct irtouch_point pt, int *x, int *y)
{
	*x = ((irtouchdev_context->calibX.A01 * pt.y) / 10000) + ((irtouchdev_context->calibX.A10 * pt.x) / 10000) + irtouchdev_context->calibX.A00;
	*y = ((irtouchdev_context->calibY.A01 * pt.y) / 10000) + ((irtouchdev_context->calibY.A10 * pt.x) / 10000) + irtouchdev_context->calibY.A00;
}
*/

static int irtouch_build_packet(
		unsigned char 	command_type, 
		unsigned char 	command, 
		int 		device_id, 
		unsigned char * data, 
		int 		length, 
		unsigned char * out_data
		)
{
	unsigned char 	tmp = 0x55;
	unsigned char 	package[64] = {0};
	int		out_length = 0;
	int i;

	package[0] = 0xaa;
	package[1] = command_type;
	package[2] = command;
	package[3] = (unsigned char)length + 1;
	package[4] = (unsigned char)device_id;

	memcpy(&package[5],data,length);

	for(i = 0; i < 5 + length; i++)
	{
		tmp = package[i] + tmp;
	}

	package[5 + length] = (unsigned char)tmp;

	out_length = 6 + length;

	memcpy(out_data, package, out_length);

	return out_length;
}

unsigned char irtouch_send_command(struct usb_device * udev,
			unsigned char 	command_type,
			unsigned char * in_data,
			unsigned char * out_data,
			int 		length
		)
{
	unsigned char	buf[64];
	int		count = 0;
	int		ret = 0;

	memset(buf,0,sizeof(buf));

	do{
		ret = usb_control_msg(udev, 
					usb_sndctrlpipe(udev, 0), 
					0, 
					0x40,
					0, 
					0, 
					(char *)in_data,
					length, 
					1000);
		msleep(10);

		ret = usb_control_msg(udev, 
					usb_rcvctrlpipe(udev, 0), 
					0, 
					0xc0, 
					0, 
					0, 
					(char *)buf, 
					64, 
					1000);
		msleep(10);

		count++;

	}while((buf[1] != 0x10) && (count < COMMAND_RETRY_COUNT));

	if(count >= COMMAND_RETRY_COUNT)
	{
		return false;
	}

	memcpy(out_data,buf,sizeof(buf));

	return true;
	
}

static void irtouch_get_device_param(struct usb_device * udev)
{
	unsigned char	inData[58];
	unsigned char	outData[64];
	unsigned char	getData[64];
	int status = -1;
	int length = 0;

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	//set touchscreen to command mode
	inData[0] = 0x01;

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("IRTOUCH:    %s  Set command mode failed.",__func__);
		return;
	}
	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	inData[0] = 0xF4;

	length = irtouch_build_packet(SET_COMMAND,SET_WORK_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("IRTOUCH:    %s  Set device to work status failed.",__func__);
		return;
	}

	//get BIOS version
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_BIOS_VER,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		sprintf(irtouchdev_context->biosVer,"%d.%d.%02d%02d",getData[6],getData[5],getData[8],getData[7]);
	}
	else
	{
		touch_err("IRTOUCH:    %s  get BIOS version failed.",__func__);
	}

	//get firmware version
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_FIRMWARE_VER,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		memset(irtouchdev_context->firmwareVer,0,sizeof(irtouchdev_context->firmwareVer));		
		if(getData[3] == 3)
		{
			sprintf(irtouchdev_context->firmwareVer, "%d.%d",getData[6],getData[5]);
		}
		else if(getData[3] == 5)
		{
			sprintf(irtouchdev_context->firmwareVer, "%d.%d.%04d",getData[6],getData[5],(getData[8]<<8|getData[7]));
		}
	}
	else
	{
		touch_err("IRTOUCH:    %s  get firmware version failed.",__func__);
	}

	//get barcode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_SERIAL_NUMBER,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		memset(irtouchdev_context->serialNumber,0,sizeof(irtouchdev_context->serialNumber));
		memcpy(irtouchdev_context->serialNumber,&getData[5],33);
	}
	else
	{
		touch_err("IRTOUCH:    %s  get serial number failed.",__func__);
	}

	//get device configuration	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,1,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		memcpy(&irtouchdev_context->devConfig,&getData[5],sizeof(struct device_config));
	}
	else
	{
		touch_err("IRTOUCH:    %s  Get device configuration failed.",__func__);
	}

	//get calib x parameter
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_CALIB_X,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		memcpy(&irtouchdev_context->calibX,&getData[5],sizeof(struct calib_param));
	}
	else
	{
		touch_err("IRTOUCH:    %s  Get calib X param failed.",__func__);
	}

	//get calib y parameter
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_CALIB_Y,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(status)
	{
		memcpy(&irtouchdev_context->calibY,&getData[5],sizeof(struct calib_param));
	}
	else
	{
		touch_err("IRTOUCH:    %s  Get calib Y param failed.",__func__);
	}
	
	//set touchscreen to work mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("IRTOUCH:    %s  Set to work mode failed.",__func__);
	}
}

static void irtouch_set_calib(struct usb_device * udev, bool flag)
{
	unsigned char	inData[58];
	unsigned char	outData[64];
	unsigned char	getData[64];
	int status = -1;
	int length = 0;

	//set touchscreen to command mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	inData[0] = 0x01;

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("IRTOUCH:    %s  Set to command mode failed.",__func__);
	}

	//Set device configuration
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));

	if(flag)
	{
		irtouchdev_context->devConfig.calibrateStatus = 0;
		memcpy(&inData,&irtouchdev_context->devConfig,58);
	
		length = irtouch_build_packet(SET_COMMAND,SET_DEVICE_CONFIG,1,inData,58,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("IRTOUCH:    %s  Set device configuration failed(reset).",__func__);
		}
	}
	else
	{
		irtouchdev_context->devConfig.calibrateStatus = 1;
		memcpy(&inData,&irtouchdev_context->devConfig,58);
	
		length = irtouch_build_packet(SET_COMMAND,SET_DEVICE_CONFIG,1,inData,58,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("IRTOUCH:    %s  Set device configuration failed.",__func__);
		}

		//Set calib X param
		memset(inData,0,sizeof(inData));
		memset(outData,0,sizeof(outData));

		memcpy(&inData,&irtouchdev_context->calibX,58);
	
		length = irtouch_build_packet(SET_COMMAND,SET_CALIB_X,0,inData,58,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("IRTOUCH:    %s  Set calib X param failed.",__func__);
		}

		//Set calib Y param
		memset(inData,0,sizeof(inData));
		memset(outData,0,sizeof(outData));

		memcpy(&inData,&irtouchdev_context->calibY,58);
	
		length = irtouch_build_packet(SET_COMMAND,SET_CALIB_Y,0,inData,58,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			touch_err("IRTOUCH:    %s  Set calib Y param failed.",__func__);
		}
	}

	//set touchscreen to work mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);	
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		touch_err("IRTOUCH:    %s  Set to work mode failed.",__func__);
	}
}

static int irtouch_open(struct inode * inode, struct file * filp)
{
	struct irtouchusb * irtouch;
	struct usb_interface * intf;

	intf = usb_find_interface(&irtouch_driver, MINOR_DEVICE_NUMBER);
	if(!intf)
	{
		touch_err("IRTOUCH:    %s usb_find_interface ERROR!",__func__);
		return -1;
	}

	irtouch = usb_get_intfdata(intf);
	if(!irtouch)
	{
		touch_err("IRTOUCH:    %s touch is NULL!",__func__);
		return -1;
	}

	filp->private_data = irtouch;

	return 0;
}

static int irtouch_release(struct inode * inode, struct file * filp)
{
	//dbg("IRTOUCH:  irtouch_release");
	return 0;
}

static long irtouch_ioctl(struct file * filp, unsigned int ctl_code, unsigned long ctl_param)
{
	unsigned char buf[64];
	unsigned char value = 0;
	int ret = -1;
	struct irtouchusb *irtouch;

	memset(buf,0,sizeof(buf));

	//dbg("IRTOUCH:    irtouch_ioctl");

	if(NULL == filp->private_data)
	{
		touch_err("IRTOUCH:    %s private_data is NULL!",__func__);
		return ret;
	}

	irtouch = filp->private_data;

	switch(ctl_code)
	{
		case CTLCODE_START_CALIB:				
			ret = copy_from_user(&value, (unsigned char *)ctl_param, sizeof(unsigned char));
			if(ret == 0)
			{
				if(value == 0x01)
				{
					irtouchdev_context->startCalib = true;
				}
				else
				{
					irtouchdev_context->startCalib = false;
				}
			}	
			
			break;

		case CTLCODE_GET_COORDINATE:
			ret = copy_to_user((struct points *)ctl_param, &irtouchdev_context->points, sizeof(struct points));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_COORDINATE>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_CALIB_STATUS:
			{
				int status = irtouchdev_context->devConfig.calibrateStatus;
				ret = copy_to_user((int *)ctl_param, &status, sizeof(int));
				if(ret != 0)
				{
					touch_err("IRTOUCH:    %s <CTLCODE_GET_CALIB_STATUS>copy_to_user failed!",__func__);
				}
				break;
			}
	
		case CTLCODE_SET_CALIB_PARA_X:			
			ret = copy_from_user(&irtouchdev_context->calibX, (struct calib_param *)ctl_param, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_SET_CALIB_PARA_X>copy_from_user failed!",__func__);
			}
			else
			{
				irtouchdev_context->isCalibX = true;
			}
			break;

		case CTLCODE_SET_CALIB_PARA_Y:			
			ret = copy_from_user(&irtouchdev_context->calibY, (struct calib_param *)ctl_param, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_SET_CALIB_PARA_Y>copy_from_user failed!",__func__);
			}
			else
			{
				irtouchdev_context->isCalibY = true;
			}
			break;

		case CTLCODE_GET_CALIB_PARA_X:
			ret = copy_to_user((struct calib_param *)ctl_param, &irtouchdev_context->calibX, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_CALIB_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_CALIB_PARA_Y:
			ret = copy_to_user((struct calib_param *)ctl_param, &irtouchdev_context->calibY, sizeof(struct calib_param));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_CALIB_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_PRODUCT_ID:
			{
				int status = irtouchdev_context->productid;
				ret = copy_to_user((int *)ctl_param, &status, sizeof(int));
				if(ret != 0)
				{
					touch_err("IRTOUCH:    %s <CTLCODE_GET_PRODUCT_ID>copy_to_user failed!",__func__);
				}
				break;
			}

		case CTLCODE_DEVICE_RESET:
			ret = copy_from_user(&value, (unsigned char *)ctl_param, sizeof(unsigned char));
			if(ret == 0)
			{
				if(value == 0x01)
				{
					irtouch_set_calib(irtouch->usbdev,true);
				}
			}
			break;

		case CTLCODE_GET_FIRMWARE_VERSION:
			ret = copy_to_user((char*)ctl_param, irtouchdev_context->firmwareVer, sizeof(irtouchdev_context->firmwareVer));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_FIRMWARE_VERSION>copy_to_user failed!",__func__);
			}
			break;	

		case CTLCODE_GET_SERIAL_NUMBER:
			ret = copy_to_user((char*)ctl_param, irtouchdev_context->serialNumber, sizeof(irtouchdev_context->serialNumber));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_PRODUCT_SN>copy_to_user failed!",__func__);
			}
			break;	

		case CTLCODE_GET_BIOS_VERSION:
			ret = copy_to_user((char*)ctl_param, irtouchdev_context->biosVer, sizeof(irtouchdev_context->biosVer));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_BIOS_VERSION>copy_to_user failed!",__func__);
			}
			break;	

		case CTLCODE_GET_DRIVER_VERSION:
			ret = copy_to_user((char*)ctl_param, DRIVER_VERSION, sizeof(DRIVER_VERSION));
			if(ret != 0)
			{
				touch_err("IRTOUCH:    %s <CTLCODE_GET_DRIVER_VERSION>copy_to_user failed!",__func__);
			}
			break;	

		case CTLCODE_SND_MSG:
			ret = copy_from_user((unsigned char *)buf, (unsigned char *)ctl_param, sizeof(buf));
			if(ret == 0)
			{

				if(irtouch->productId == 0x81)
				{
					ret = usb_control_msg(irtouch->usbdev, 
							usb_sndctrlpipe(irtouch->usbdev, 0), 
							0x09, 
							0x21,
							0x03aa, 
							0x01, 
							(char *)buf,
							sizeof(buf), 
							1000);	
				}
				else
				{
					ret = usb_control_msg(irtouch->usbdev, 
						usb_sndctrlpipe(irtouch->usbdev, 0), 
						0, 
						0x40,
						0, 
						0, 
						(char *)buf,
						sizeof(buf), 
						1000);
				}				
			}	
			break;

		case CTLCODE_RCV_MSG:
			if(irtouch->productId == 0x81)
			{
				ret = usb_control_msg(irtouch->usbdev, 
					usb_rcvctrlpipe(irtouch->usbdev, 0), 
					0x01, 
					0xa1, 
					0x03aa, 
					1, 
					(char *)buf, 
					sizeof(buf), 
					1000);
			}
			else
			{
				ret = usb_control_msg(irtouch->usbdev, 
						usb_rcvctrlpipe(irtouch->usbdev, 0), 
						0, 
						0xc0, 
						0, 
						0, 
						(char *)buf, 
						sizeof(buf), 
						1000);
			}

			if(ret > 0)
			{	
				ret = copy_to_user((unsigned char *)ctl_param, buf, sizeof(buf));
			}
			break;

		default:
			break;
	}

	if(irtouchdev_context->isCalibX && irtouchdev_context->isCalibY)
	{
		irtouchdev_context->isCalibX = false;
		irtouchdev_context->isCalibY = false;
		irtouch_set_calib(irtouch->usbdev,false);
	}

	return 0;
}

static void processPoints(struct AlgContext *pAlgCtx)
{
	int dMiniDist;
	int xx,yy;
	int dDist;
	int nInd;
	int i;

	pAlgCtx->outPoint[0] = pAlgCtx->inPoint[0];
	pAlgCtx->outPoint[1] = pAlgCtx->inPoint[1];

	dMiniDist = -1.0;
	nInd = 1;
	
	if(pAlgCtx->actualCounter > 2)
	{
		for(i=1;i<=3;i++)
	    	{
			xx = (pAlgCtx->inPoint[0].x-pAlgCtx->inPoint[i].x)*(pAlgCtx->inPoint[0].x-pAlgCtx->inPoint[i].x);
			yy = (pAlgCtx->inPoint[0].y-pAlgCtx->inPoint[i].y)*(pAlgCtx->inPoint[0].y-pAlgCtx->inPoint[i].y);

			dDist = xx+yy;
			if(dDist>dMiniDist)
			{
				nInd = i;
				dMiniDist = dDist;
			}
		}
		pAlgCtx->actualCounter = 2;
		pAlgCtx->outPoint[1] = pAlgCtx->inPoint[nInd];
	}
}

static bool processInputData(struct irtouchusb *ir,struct AlgContext *pAlgCtx)
{
	int i = 0;

	if(((ir->irpkt->actualCounter & 0x1F) == 4) && ((ir->irpkt->actualCounter >> 5) == 0))
	{
		for(i=0;i<3;i++)
		{
			pAlgCtx->inPoint[i] = ir->irpkt->points[i];
		}
		
		return false;		
	}
	else if(((ir->irpkt->actualCounter & 0x1F) == 0) && (ir->irpkt->actualCounter >> 5 == 1))
	{
		pAlgCtx->inPoint[3] = ir->irpkt->points[0];
		pAlgCtx->actualCounter = 4;
	}
	else
	{
		pAlgCtx->actualCounter = ir->irpkt->actualCounter & 0x1F;

		for(i=0;i<(ir->irpkt->actualCounter & 0x1F);i++)
		{
			pAlgCtx->inPoint[i] = ir->irpkt->points[i];
		}
	}

	processPoints(pAlgCtx);
	
	return true;
}

static void irtouch_irq(struct urb *urb)
{
	struct irtouchusb *ir = urb->context;
	struct input_dev *dev = ir->dev;
	int status;
	int x = 0, y = 0;
	int touch = 0, pressure = 0;
	
	switch (urb->status) {
		case 0:            /* success */
			break;
		case -ECONNRESET:  /* unlink */
		case -ENOENT:
		case -ESHUTDOWN:
			return;    /* -EPIPE:  should clear the halt */
		default:	   /* error */
			goto resubmit;
	}
	
	if(processInputData(ir,&irtouchdev_context->algCtx))
	{		
		if(irtouchdev_context->algCtx.outPoint[0].status == 0x07)
		{
			touch = 1;
			pressure = 255;
		}
		else
		{
			touch = 0;
			pressure = 0;
		}
		
		x = irtouchdev_context->algCtx.outPoint[0].x;
		y = irtouchdev_context->algCtx.outPoint[0].y;
		
		if(irtouchdev_context->startCalib)
		{
			irtouchdev_context->points.x = irtouchdev_context->algCtx.outPoint[0].x;
			irtouchdev_context->points.y = irtouchdev_context->algCtx.outPoint[0].y;
		}
		
		if(irtouchdev_context->devConfig.calibrateStatus && !irtouchdev_context->startCalib)
		{
			//irtouch_translatePoint(irtouchdev_context->algCtx.outPoint[0],&x,&y);
		}
			
	//	printk("status=%d, x=%d, y=%d!\n", touch, x, y);
		input_report_key(dev, BTN_TOUCH, touch);
		input_report_abs(dev, ABS_PRESSURE, pressure);
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		input_sync(dev);
	}

resubmit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		touch_err ("can't resubmit intr, %s-%s/input0, status %d",
				ir->usbdev->bus->bus_name,
				ir->usbdev->devpath, status);
}

static struct file_operations irtouch_fops = {
	.owner = THIS_MODULE,
	.open = irtouch_open,
	.unlocked_ioctl = irtouch_ioctl,
	.release = irtouch_release,
};

static int irtouch_open_device(struct input_dev *dev)
{
	struct irtouchusb *ir = input_get_drvdata(dev);

	ir->irq->dev = ir->usbdev;
	if (usb_submit_urb(ir->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void irtouch_close_device(struct input_dev *dev)
{
	struct irtouchusb *ir = input_get_drvdata(dev);

	usb_kill_urb(ir->irq);
}

static bool irtouch_mkdev(void)
{
	int retval;

	//create device node
	dev_t devno = MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER);

	retval = register_chrdev_region(devno,1,TOUCH_DEVICE_NAME);
	if(retval < 0)
	{
		touch_err("IRTOUCH:    %s register chrdev error.",__func__);
		return false;
	}

	cdev_init(&irtouch_cdev,&irtouch_fops);
	irtouch_cdev.owner = THIS_MODULE;
	irtouch_cdev.ops = &irtouch_fops;
	retval = cdev_add(&irtouch_cdev,devno,1);

	if(retval)
	{
		touch_err("IRTOUCH:    %s  Adding char_reg_setup_cdev error=%d",__func__,retval);
		return false;
	}

	irser_class = class_create(THIS_MODULE, TOUCH_DEVICE_NAME);
	if(IS_ERR(irser_class))
	{
		touch_err("IRTOUCH:    %s class create failed.",__func__);
		return false;
	}
	
	device_create(irser_class,NULL,MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER),NULL,TOUCH_DEVICE_NAME);

	printk("IRTOUCH:    %s success!\n",__func__);
	
	return true;
}

static int irtouch_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct irtouchusb *ir;
	struct input_dev *input_dev;
	int pipe;
	int error = -ENOMEM;

	interface = intf->cur_altsetting;

	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		return -ENODEV;
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT)
		return -ENODEV;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);

	if (!(irtouchdev_context = kmalloc(sizeof(struct device_context), GFP_KERNEL)))  
		return -ENOMEM; 
	memset(irtouchdev_context,0,sizeof(struct device_context));
	
	ir = kzalloc(sizeof(struct irtouchusb), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev)
		goto fail1;

	ir->irpkt = usb_alloc_coherent(dev, USB_TRANSFER_LENGTH, GFP_ATOMIC, &ir->data_dma);
	if (!ir->irpkt)
		goto fail1;

	ir->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!ir->irq)
		goto fail2;

	ir->usbdev = dev;
	ir->dev = input_dev;
	
	if (dev->manufacturer)
		strlcpy(ir->name, dev->manufacturer, sizeof(ir->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(ir->name, " ", sizeof(ir->name));
		strlcat(ir->name, dev->product, sizeof(ir->name));
	}

	if (!strlen(ir->name))
		snprintf(ir->name, sizeof(ir->name),
				"IRTOUCH dual-touch touchscreen %04x:%04x",
				le16_to_cpu(dev->descriptor.idVendor),
				le16_to_cpu(dev->descriptor.idProduct));

	ir->productId = dev->descriptor.idProduct;

	usb_make_path(dev, ir->phys, sizeof(ir->phys));
	strlcat(ir->phys, "/input0", sizeof(ir->phys));

	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;
	
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, 32767, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 32767, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0,255, 0, 0);
	
	input_set_drvdata(input_dev, ir);

	input_dev->open = irtouch_open_device;
	input_dev->close = irtouch_close_device;

	usb_fill_int_urb(ir->irq, dev, pipe, ir->irpkt, USB_TRANSFER_LENGTH,
			irtouch_irq, ir, endpoint->bInterval);
	ir->irq->transfer_dma = ir->data_dma;
	ir->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(ir->dev);
	

	usb_set_intfdata(intf, ir);

	error = usb_register_dev(intf, &irtouch_class_driver);
	if(error)
	{
		touch_err("IRTOUCH:     %s Not able to get a minor for this device.",__func__);
		usb_set_intfdata(intf,NULL);
		goto fail3;
	}

	msleep(1000);
	
	irtouch_get_device_param(dev);
	irtouch_mkdev();
	irtouchdev_context->productid = dev->descriptor.idProduct;
	printk("IRTOUCH id=0x%x: %s success!", irtouchdev_context->productid, __func__);
	return 0;


fail3:
	usb_free_urb(ir->irq);
fail2:
	usb_free_coherent(dev, USB_TRANSFER_LENGTH, ir->irpkt, ir->data_dma);
	
fail1:
	input_free_device(input_dev);
	kfree(ir);
	kfree(irtouchdev_context);
	return -ENOMEM;
}

static void irtouch_disconnect(struct usb_interface *intf)
{
	struct irtouchusb *ir = usb_get_intfdata (intf);

	dev_t devno = MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER);

	cdev_del(&irtouch_cdev);
	unregister_chrdev_region(devno,1);

	device_destroy(irser_class,devno);
	class_destroy(irser_class);

	usb_deregister_dev(intf, &irtouch_class_driver);

	usb_set_intfdata(intf, NULL);
	if(ir) 
	{
		usb_kill_urb(ir->irq);
		input_unregister_device(ir->dev);
		usb_free_urb(ir->irq);
		usb_free_coherent(interface_to_usbdev(intf), USB_TRANSFER_LENGTH, ir->irpkt, ir->data_dma);
		kfree(ir);
	}

	if(irtouchdev_context)
	{
		kfree(irtouchdev_context);
	}
}

static struct usb_device_id irtouch_id_table [] = {
	{ USB_DEVICE(0x6615, 0x0080) }, 
	{ USB_DEVICE(0x6615, 0x0081) }, 
	{ }    /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, irtouch_id_table);

static struct usb_driver irtouch_driver = {
	.name        = "IRTOUCH",
	.probe        = irtouch_probe,
	.disconnect    = irtouch_disconnect,
	.id_table    = irtouch_id_table,
};

static int __init irtouch_init(void)
{
	int retval = usb_register(&irtouch_driver);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit irtouch_exit(void)
{
	usb_deregister(&irtouch_driver);
}

module_init(irtouch_init);
module_exit(irtouch_exit);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(DRIVER_VERSION);
