/*
 * Copyright (C) 2009
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * Copyright (C) 2011
 * Stefano Babic, DENX Software Engineering, <sbabic@denx.de>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include "common.h"
#include "imx-regs.h"
#include "gpio.h"
#include "../io.h"
#include "../unimplemented.h"
#include <ethdrivers/raw_iface.h>
#include "../../../os_iface.h"

enum mxc_gpio_direction {
	MXC_GPIO_DIRECTION_IN,
	MXC_GPIO_DIRECTION_OUT,
};

#define GPIO_TO_PORT(n)		(n / 32)

#define GPIO_SIZE 0x4000

/* GPIO port description */
static unsigned long gpio_ports[] = {
	[0] = 0,
	[1] = 0,
	[2] = 0,
	[3] = 0,
	[4] = 0,
	[5] = 0,
	[6] = 0,
};


static unsigned long gpio_paddr[] = {
	[0] = GPIO1_BASE_ADDR,
	[1] = GPIO2_BASE_ADDR,
	[2] = GPIO3_BASE_ADDR, /* Used by ethernet */
	[3] = GPIO4_BASE_ADDR,
	[4] = GPIO5_BASE_ADDR,
	[5] = GPIO6_BASE_ADDR,
	[6] = GPIO7_BASE_ADDR,
};

static int mxc_gpio_direction(unsigned int gpio,
	enum mxc_gpio_direction direction)
{
	unsigned int port = GPIO_TO_PORT(gpio);
	struct gpio_regs *regs;
	uint32_t l;

	if (port >= ARRAY_SIZE(gpio_ports))
		return -1;

	gpio &= 0x1f;

    if(gpio_ports[port] == 0){
        eth_paddr_t gpio_phys = (eth_paddr_t)gpio_paddr[port];
        eth_vaddr_t gpio_virt = os_ioremap(gpio_phys, GPIO_SIZE);
        gpio_ports[port] = (unsigned long)gpio_virt;
    }

	regs = (struct gpio_regs *)gpio_ports[port];
	l = readl(&regs->gpio_dir);

	switch (direction) {
	case MXC_GPIO_DIRECTION_OUT:
		l |= 1 << gpio;
		break;
	case MXC_GPIO_DIRECTION_IN:
		l &= ~(1 << gpio);
	}
	writel(l, &regs->gpio_dir);

	return 0;
}

int gpio_set_value(unsigned gpio, int value)
{
	unsigned int port = GPIO_TO_PORT(gpio);
	struct gpio_regs *regs;
	uint32_t l;

	if (port >= ARRAY_SIZE(gpio_ports))
		return -1;

	gpio &= 0x1f;

	regs = (struct gpio_regs *)gpio_ports[port];

	l = readl(&regs->gpio_dr);
	if (value)
		l |= 1 << gpio;
	else
		l &= ~(1 << gpio);
	writel(l, &regs->gpio_dr);

	return 0;
}

int gpio_get_value(unsigned gpio)
{
	unsigned int port = GPIO_TO_PORT(gpio);
	struct gpio_regs *regs;
	uint32_t val;

	if (port >= ARRAY_SIZE(gpio_ports))
		return -1;

	gpio &= 0x1f;

	regs = (struct gpio_regs *)gpio_ports[port];

	val = (readl(&regs->gpio_psr) >> gpio) & 0x01;

	return val;
}

int gpio_request(unsigned gpio, const char *label)
{
	unsigned int port = GPIO_TO_PORT(gpio);
	if (port >= ARRAY_SIZE(gpio_ports))
		return -1;
	return 0;
}

int gpio_free(unsigned gpio)
{
	return 0;
}

int gpio_direction_input(unsigned gpio)
{
	return mxc_gpio_direction(gpio, MXC_GPIO_DIRECTION_IN);
}

int gpio_direction_output(unsigned gpio, int value)
{
	int ret = mxc_gpio_direction(gpio, MXC_GPIO_DIRECTION_OUT);

	if (ret < 0)
		return ret;

	return gpio_set_value(gpio, value);
}
