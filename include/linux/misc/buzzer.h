/*
 * buzzer.h -- support  gpio buzzer
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

 
#ifndef __BUZZER_H
#define __BUZZER_H

struct buzzer_platform_data
{
	unsigned long pin_number;             // the pin for buzzer
};

#endif

