/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>

static int cmd_enable_usb(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Enabling USB");

	int ret = usb_enable(NULL);
	if (ret != 0) {
		shell_error(sh, "Failed to enable USB %d", ret);
		return 0;
	}


	return 0;
}


int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	printf("Size of wchar is %u\n", sizeof(wchar_t));
	return 0;
}

SHELL_CMD_ARG_REGISTER(enable_usb, NULL, "Show kernel version", cmd_enable_usb, 1, 0);