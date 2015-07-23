/*
 * watchdog.h -- support  gpio watchdog
 *
 *  Author 		jio
 *  Email   		385426564@qq.com
 *  Create time 	2012-11-28
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

 
#ifndef __WATCHDOG_H
#define __WATCHDOG_H

struct watchdog_platform_data
{
	unsigned long enable_pin;             // the pin for enable watchdog 
	unsigned long feeddog_pin;            // the pin for feed watchdog
};

#endif

