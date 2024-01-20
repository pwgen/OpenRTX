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
//#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include <state.h>
#include <SA8x8.h>
#include <interfaces/radio.h>

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



static int cmd_radio_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Callsign %s",state.settings.callsign);
	shell_print(sh, "GPS %i",state.settings.gps_enabled);
	shell_print(sh, "rssi %f",state.rssi);
	shell_print(sh, "Brightness  rssi %i",state.settings.brightness);
	shell_print(sh, "channel %i",state.channel.tx_frequency);
	shell_print(sh, CONFIG_BOARD);

	return 0;
}

static int cmd_radio_frequency(const struct shell *sh, size_t argc, char **argv)
{
	
	float txfrequency=0;
	float rxfrequency=0;
	shell_print(sh, "Frequency argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
	sscanf(argv[1],"%f",&txfrequency);
	sscanf(argv[2],"%f",&rxfrequency);
    shell_print(sh, "Set Frequency TX  %f RX %f", txfrequency*1000000,rxfrequency*1000000);
	state.channel.tx_frequency=txfrequency*1000000;
	state.channel.rx_frequency=rxfrequency*1000000;
	return 0;
}
static int cmd_radio_volume(const struct shell *sh, size_t argc, char **argv)
{
	int vol=0;

	shell_print(sh, "Volumeargc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
	sscanf(argv[1],"%i",&vol);
	state.settings.vpLevel=vol;
	return 0;
}
static int cmd_radio_audioon(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "txon argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
	sa8x8_setAudio(true);

	return 0;
}
static int cmd_radio_audiooff(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "txoff argc = %zd", argc);
	for (size_t cnt = 0; cnt < argc; cnt++) {
		shell_print(sh, "  argv[%zd] = %s", cnt, argv[cnt]);
	}
//	radio_enableRx();
	sa8x8_setAudio(false);
	return 0;
}
static int cmd_radio_bright(const struct shell *sh, size_t argc, char **argv)
{
	int brightness=0;
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

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Zephyr version %s", KERNEL_VERSION_STRING);

	return 0;
}
static int cmd_radio_poke(const struct shell *sh, size_t argc, char **argv)
{
	int reg;
	int addr;
	sscanf(argv[1],"%2x",&addr);
	sscanf(argv[2],"%4x",&reg);
	shell_print(sh, " %2x %4x %i %i ",addr,reg,addr,reg);
	sa8x8_writeAT1846Sreg((uint8_t) addr ,(uint16_t) reg);
	shell_print(sh, " %2x %4x",addr,reg);
	

	return 0;
}
static int cmd_radio_voicesource(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Zephyr version %s", KERNEL_VERSION_STRING);

	return 0;
}
static int cmd_radio_peek(const struct shell *sh, size_t argc, char **argv)
{
	int  addr;
	uint16_t reg;
	sscanf(argv[1],"%2x",&addr);
	//shell_print(sh, " peek  %s  %4x ",argv[1], addr);
//	sscanf(argv[1],"%4x",&reg);
	reg=sa8x8_readAT1846Sreg((uint16_t) addr);
	shell_print(sh, " peek  %2x  %4x ",addr, reg);
	return 0;
}

static int cmd_radio_txpower(const struct shell *sh, size_t argc, char **argv)
{
	float txpower;
	int pgagain;
	sscanf(argv[1],"%f",&txpower);
	sscanf(argv[2],"%i",&pgagain);
	//setPgaGain(pgagain);
	sa8x8_writeAT1846Sreg(0x0a,pgagain);
	shell_print(sh, "TX power %f",txpower);
	sa8x8_setTxPower(txpower);
	return 0;
}
static int cmd_radio_tx(const struct shell *sh, size_t argc, char **argv)
{
//	shell_print(sh, "txoff argc = %zd", argc);
	radio_enableTx();
//	sa8x8_setAudio(false);
	return 0;
}
static int cmd_radio_rx(const struct shell *sh, size_t argc, char **argv)
{
//	shell_print(sh, "txoff argc = %zd", argc);
	radio_enableRx();
//	sa8x8_setAudio(false);
	return 0;
}
static int cmd_radio_mic(const struct shell *sh, size_t argc, char **argv)
{
	sa8x8_writeAT1846Sreg(0x3a,0x4000);
	return 0;
}
static int cmd_radio_dsp(const struct shell *sh, size_t argc, char **argv)
{
	sa8x8_writeAT1846Sreg(0x79,0xd932);
	sa8x8_writeAT1846Sreg(0x3a,0x3000);
	return 0;
}
static int cmd_radio_nvram(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Checking nvram");
	return 0;
}
static int cmd_radio_dmtf(const struct shell *sh, size_t argc, char **argv)
{
	int mode,time1 ,time2;
	uint16_t reg;
	sscanf(argv[1],"%1x",&mode);
	sscanf(argv[2],"%1x",&time1);
	sscanf(argv[3],"%1x",&time2);
	reg=mode<<8;
	reg=reg + (time1<<4)  + time2; 
	sa8x8_writeAT1846Sreg(0x63,reg);
	shell_print(sh, "Checking nvram");
	return 0;
}
static int cmd_radio_tone(const struct shell *sh, size_t argc, char **argv)
{
	int tone1,tone2;
	uint16_t reg;
	sscanf(argv[1],"%4x",&tone1);
	sscanf(argv[2],"%4x",&tone2);
	
	sa8x8_writeAT1846Sreg(0x35,tone1);
	sa8x8_writeAT1846Sreg(0x36,tone1);
	return 0;
}
	SHELL_STATIC_SUBCMD_SET_CREATE(sub_radio,
	SHELL_CMD(status, NULL, "Status command.", cmd_radio_status),
	SHELL_CMD(freq, NULL, "Set Frequency  tx in Mhz rx in Mhz  radio freq 144.5 145.0 ", cmd_radio_frequency),
	SHELL_CMD(volume, NULL, "Set Volume.", cmd_radio_volume),
	SHELL_CMD(bright, NULL, "Set Bright.", cmd_radio_bright),
	SHELL_CMD(aoff, NULL, "Set Audio off.", cmd_radio_audiooff),
	SHELL_CMD(aon, NULL, "Set Audio on.", cmd_radio_audioon),
	SHELL_CMD(peek, NULL, "Peek on sa8x8.", cmd_radio_peek),
	SHELL_CMD(poke, NULL, "poke on sa868.", cmd_radio_poke),
	SHELL_CMD(source, NULL, "Select voice source .", cmd_radio_voicesource),
	SHELL_CMD(txpower, NULL, "Set txpoweramp radio txpower 1.0| 0.0 [0 1 2 4 8 16 32 63].", cmd_radio_txpower),
	SHELL_CMD(tx, NULL, "Transmit", cmd_radio_tx),
	SHELL_CMD(rx, NULL, "Receive", cmd_radio_rx),
	SHELL_CMD(mic, NULL, "Set audio source to mic", cmd_radio_mic),
	SHELL_CMD(dsp, NULL, "Set audio source DSP", cmd_radio_dsp),
	SHELL_CMD(nv, NULL, "Checking nvram", cmd_radio_nvram),
	SHELL_CMD(dtmf, NULL, "Set dtmf register", cmd_radio_dsp),
	SHELL_CMD(tone, NULL, "Cset tone 1 and tone 2", cmd_radio_tone),
	
	SHELL_SUBCMD_SET_END /* Array terminated. */

);

SHELL_CMD_REGISTER(radio, &sub_radio, "Radio commands", NULL);

SHELL_CMD_ARG_REGISTER(version, NULL, "Show kernel version", cmd_version, 1, 0);

SHELL_SUBCMD_SET_CREATE(sub_section_cmd, (section_cmd));

static int cmd1_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "cmd1 executed");

	return 0;
}

SHELL_SUBCMD_SET_CREATE(sub_section_cmd1, (section_cmd, cmd1));
SHELL_SUBCMD_ADD((section_cmd), cmd1, &sub_section_cmd1, "help for cmd1", cmd1_handler, 1, 0);
SHELL_CMD_REGISTER(section_cmd, &sub_section_cmd, "Demo command using section for subcommand registration", NULL);


void cmdshell_init(void)
{
	printk("Shell init\n");
}


//struct fs_littlefs lfsfs;
//static struct fs_mount_t __mp = {
//	.type = FS_LITTLEFS,
//	.fs_data = &lfsfs,
//	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
//};

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
