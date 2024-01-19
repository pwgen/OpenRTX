/***************************************************************************
 *   Copyright (C) 2020 - 2023 by Federico Amedeo Izzo IU2NUO,             *
 *                                Niccolò Izzo IU2KIN                      *
 *                                Frederik Saraci IU2NRO                   *
 *                                Silvano Seva IU2KWO                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/
#include <version.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>

#define CONFIG_FS_LITTLEFS_LOOKAHEAD_SIZE 2048
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include <state.h>

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif

LOG_MODULE_REGISTER(app);

//extern void foo(void);

void timer_expired_handler(struct k_timer *timer)
{
	LOG_INF("Timer expired.");

	/* Call another module to present logging from multiple sources. */
//	foo();
}

K_TIMER_DEFINE(log_timer, timer_expired_handler, NULL);

static int cmd_log_test_start(const struct shell *sh, size_t argc,
			      char **argv, uint32_t period)
{
	ARG_UNUSED(argv);

	k_timer_start(&log_timer, K_MSEC(period), K_MSEC(period));
	shell_print(sh, "Log test started\n");

	return 0;
}

static int cmd_log_test_start_demo(const struct shell *sh, size_t argc,
				   char **argv)
{
	return cmd_log_test_start(sh, argc, argv, 200);
}

static int cmd_log_test_start_flood(const struct shell *sh, size_t argc,
				    char **argv)
{
	return cmd_log_test_start(sh, argc, argv, 10);
}

static int cmd_log_test_stop(const struct shell *sh, size_t argc,
			     char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_timer_stop(&log_timer);
	shell_print(sh, "Log test stopped");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_log_test_start,
	SHELL_CMD_ARG(demo, NULL,
		  "Start log timer which generates log message every 200ms.",
		  cmd_log_test_start_demo, 1, 0),
	SHELL_CMD_ARG(flood, NULL,
		  "Start log timer which generates log message every 10ms.",
		  cmd_log_test_start_flood, 1, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_STATIC_SUBCMD_SET_CREATE(sub_log_test,
	SHELL_CMD_ARG(start, &sub_log_test_start, "Start log test", NULL, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Stop log test.", cmd_log_test_stop, 1, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(log_test, &sub_log_test, "Log test", NULL);


static int cmd_radio_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Callsign %s",state.settings.callsign);
	shell_print(sh, "GPS %i",state.settings.gps_enabled);
	shell_print(sh, "rssi %f",state.rssi);
	shell_print(sh, "Brightness  rssi %i",state.settings.brightness);
	shell_print(sh, "channel %i",state.channel.tx_frequency);

	return 0;
}

static int cmd_demo_board(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, CONFIG_BOARD);

	return 0;
}

#if defined CONFIG_SHELL_GETOPT
/* Thread save usage */
static int cmd_demo_getopt_ts(const struct shell *sh, size_t argc,
			      char **argv)
{
	struct getopt_state *state;
	char *cvalue = NULL;
	int aflag = 0;
	int bflag = 0;
	int c;

	while ((c = getopt(argc, argv, "abhc:")) != -1) {
		state = getopt_state_get();
		switch (c) {
		case 'a':
			aflag = 1;
			break;
		case 'b':
			bflag = 1;
			break;
		case 'c':
			cvalue = state->optarg;
			break;
		case 'h':
			/* When getopt is active shell is not parsing
			 * command handler to print help message. It must
			 * be done explicitly.
			 */
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		case '?':
			if (state->optopt == 'c') {
				shell_print(sh,
					"Option -%c requires an argument.",
					state->optopt);
			} else if (isprint(state->optopt) != 0) {
				shell_print(sh,
					"Unknown option `-%c'.",
					state->optopt);
			} else {
				shell_print(sh,
					"Unknown option character `\\x%x'.",
					state->optopt);
			}
			return 1;
		default:
			break;
		}
	}

	shell_print(sh, "aflag = %d, bflag = %d", aflag, bflag);
	return 0;
}

static int cmd_demo_getopt(const struct shell *sh, size_t argc,
			      char **argv)
{
	char *cvalue = NULL;
	int aflag = 0;
	int bflag = 0;
	int c;

	while ((c = getopt(argc, argv, "abhc:")) != -1) {
		switch (c) {
		case 'a':
			aflag = 1;
			break;
		case 'b':
			bflag = 1;
			break;
		case 'c':
			cvalue = optarg;
			break;
		case 'h':
			/* When getopt is active shell is not parsing
			 * command handler to print help message. It must
			 * be done explicitly.
			 */
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		case '?':
			if (optopt == 'c') {
				shell_print(sh,
					"Option -%c requires an argument.",
					optopt);
			} else if (isprint(optopt) != 0) {
				shell_print(sh, "Unknown option `-%c'.",
					optopt);
			} else {
				shell_print(sh,
					"Unknown option character `\\x%x'.",
					optopt);
			}
			return 1;
		default:
			break;
		}
	}

	shell_print(sh, "aflag = %d, bflag = %d", aflag, bflag);
	return 0;
}
#endif

static int cmd_demo_params(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}

	return 0;
}
static int cmd_radio_frequency(const struct shell *sh, size_t argc, char **argv)
{
	float frequency=0;
	shell_print(sh, "Frequency argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
	sscanf(argv[1],"%f",&frequency);
shell_print(sh, "Set Frequency %f", frequency*1000);
	state.channel.tx_frequency=frequency;
	return 0;
}
static int cmd_radio_volume(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Volumeargc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}

	return 0;
}
static int cmd_radio_bright(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t brightness=0;
	shell_print(sh, "Bright argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
//	state.settings.brightness = a2i(argv[1]);
	sscanf(argv[1],"%i",&brightness);
	shell_print(sh, "  Bright %i ", brightness);
	state.settings.brightness=brightness;
	return 0;
}
static int cmd_radio_params(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
	
	return 0;
}

static int cmd_demo_hexdump(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "argv[%zd]", cnt);
		shell_hexdump(sh, argv[cnt], strlen(argv[cnt]));
	}

	return 0;
}

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Zephyr version %s", KERNEL_VERSION_STRING);

	return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(sub_radio,
	SHELL_CMD(status, NULL, "Status command.", cmd_radio_status),
	SHELL_CMD(freq, NULL, "Set Frequency.", cmd_radio_frequency),
	SHELL_CMD(volume, NULL, "Set Volume.", cmd_radio_volume),
	SHELL_CMD(bright, NULL, "Set Bright.", cmd_radio_bright),
	
	SHELL_SUBCMD_SET_END /* Array terminated. */

);

//SHELL_CMD_REGISTER(demo, &sub_demo, "Demo commands", NULL);
SHELL_CMD_REGISTER(radio, &sub_radio, "Radio commands", NULL);

SHELL_CMD_ARG_REGISTER(version, NULL, "Show kernel version", cmd_version, 1, 0);

//SHELL_CMD_ARG_REGISTER(bypass, NULL, "Bypass shell", cmd_bypass, 1, 0);

//SHELL_COND_CMD_ARG_REGISTER(CONFIG_SHELL_START_OBSCURED, login, NULL,
//			    "<password>", cmd_login, 2, 0);

//SHELL_COND_CMD_REGISTER(CONFIG_SHELL_START_OBSCURED, logout, NULL,
//			"Log out.", cmd_logout);


/* Create a set of commands. Commands to this set are added using @ref SHELL_SUBCMD_ADD
 * and @ref SHELL_SUBCMD_COND_ADD.
 */
SHELL_SUBCMD_SET_CREATE(sub_section_cmd, (section_cmd));

static int cmd1_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "cmd1 executed");

	return 0;
}

/* Create a set of subcommands for "section_cmd cm1". */
SHELL_SUBCMD_SET_CREATE(sub_section_cmd1, (section_cmd, cmd1));

/* Add command to the set. Subcommand set is identify by parent shell command. */
SHELL_SUBCMD_ADD((section_cmd), cmd1, &sub_section_cmd1, "help for cmd1", cmd1_handler, 1, 0);

SHELL_CMD_REGISTER(section_cmd, &sub_section_cmd,
		   "Demo command using section for subcommand registration", NULL);


void cmdshell_init(void)
{
	printk("Shell init\n");
}


struct fs_littlefs lfsfs;
static struct fs_mount_t __mp = {
	.type = FS_LITTLEFS,
	.fs_data = &lfsfs,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

int cmdshell_main(void)
{
	int rc=1;
/*	struct fs_mount_t *mountpoint = &__mp;
//	rc = fs_mount(mountpoint);
	if (rc < 0) {
		LOG_PRINTK("FAIL: mount id %" PRIuPTR " at %s: %d\n",(uintptr_t)mountpoint->storage_dev, mountpoint->mnt_point, rc);
		return rc;
	}
*/
printk("Shell main\n");

	return 0;
}
