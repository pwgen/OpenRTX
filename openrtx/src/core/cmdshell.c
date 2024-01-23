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
#include <lfs.h>
#define CONFIG_FS_LITTLEFS_LOOKAHEAD_SIZE 2048
#define CONFIG_FS_LITTLEFS_CACHE_SIZE 1024
#define  CONFIG_FS_LITTLEFS_READ_SIZE 1024
#define CONFIG_FS_LITTLEFS_PROG_SIZE  1024

/* Matches LFS_NAME_MAX */
#define MAX_PATH_LEN 255
#define TEST_FILE_SIZE 547
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

/* Matches LFS_NAME_MAX */
#define MAX_PATH_LEN 255
#define TEST_FILE_SIZE 547

static uint8_t file_test_pattern[TEST_FILE_SIZE];
static int lsdir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		LOG_ERR("Error opening dir %s [%d]\n", path, res);
		return res;
	}

	LOG_PRINTK("\nListing dir %s ...\n", path);
	for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			if (res < 0) {
				LOG_ERR("Error reading dir [%d]\n", res);
			}
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			LOG_PRINTK("[DIR ] %s\n", entry.name);
		} else {
			LOG_PRINTK("[FILE] %s (size = %zu)\n",
				   entry.name, entry.size);
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);

	return res;
}

static int littlefs_increase_infile_value(char *fname)
{
	uint8_t boot_count = 0;
	struct fs_file_t file;
	int rc, ret;

	fs_file_t_init(&file);
	rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
	if (rc < 0) {
		LOG_ERR("FAIL: open %s: %d", fname, rc);
		return rc;
	}

	rc = fs_read(&file, &boot_count, sizeof(boot_count));
	if (rc < 0) {
		LOG_ERR("FAIL: read %s: [rd:%d]", fname, rc);
		goto out;
	}
	LOG_PRINTK("%s read count:%u (bytes: %d)\n", fname, boot_count, rc);

	rc = fs_seek(&file, 0, FS_SEEK_SET);
	if (rc < 0) {
		LOG_ERR("FAIL: seek %s: %d", fname, rc);
		goto out;
	}

	boot_count += 1;
	rc = fs_write(&file, &boot_count, sizeof(boot_count));
	if (rc < 0) {
		LOG_ERR("FAIL: write %s: %d", fname, rc);
		goto out;
	}

	LOG_PRINTK("%s write new boot count %u: [wr:%d]\n", fname,
		   boot_count, rc);

 out:
	ret = fs_close(&file);
	if (ret < 0) {
		LOG_ERR("FAIL: close %s: %d", fname, ret);
		return ret;
	}

	return (rc < 0 ? rc : 0);
}

static void incr_pattern(uint8_t *p, uint16_t size, uint8_t inc)
{
	uint8_t fill = 0x55;

	if (p[0] % 2 == 0) {
		fill = 0xAA;
	}

	for (int i = 0; i < (size - 1); i++) {
		if (i % 8 == 0) {
			p[i] += inc;
		} else {
			p[i] = fill;
		}
	}

	p[size - 1] += inc;
}

static void init_pattern(uint8_t *p, uint16_t size)
{
	uint8_t v = 0x1;

	memset(p, 0x55, size);

	for (int i = 0; i < size; i += 8) {
		p[i] = v++;
	}

	p[size - 1] = 0xAA;
}

static void print_pattern(uint8_t *p, uint16_t size)
{
	int i, j = size / 16, k;

	for (k = 0, i = 0; k < j; i += 16, k++) {
		LOG_PRINTK("%02x %02x %02x %02x %02x %02x %02x %02x ",
			   p[i], p[i+1], p[i+2], p[i+3],
			   p[i+4], p[i+5], p[i+6], p[i+7]);
		LOG_PRINTK("%02x %02x %02x %02x %02x %02x %02x %02x\n",
			   p[i+8], p[i+9], p[i+10], p[i+11],
			   p[i+12], p[i+13], p[i+14], p[i+15]);

		/* Mark 512B (sector) chunks of the test file */
		if ((k + 1) % 32 == 0) {
			LOG_PRINTK("\n");
		}
	}

	for (; i < size; i++) {
		LOG_PRINTK("%02x ", p[i]);
	}

	LOG_PRINTK("\n");
}

static int littlefs_binary_file_adj(char *fname)
{
	struct fs_dirent dirent;
	struct fs_file_t file;
	int rc, ret;

	/*
	 * Uncomment below line to force re-creation of the test pattern
	 * file on the littlefs FS.
	 */
	/* fs_unlink(fname); */
	fs_file_t_init(&file);

	rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
	if (rc < 0) {
		LOG_ERR("FAIL: open %s: %d", fname, rc);
		return rc;
	}

	rc = fs_stat(fname, &dirent);
	if (rc < 0) {
		LOG_ERR("FAIL: stat %s: %d", fname, rc);
		goto out;
	}

	/* Check if the file exists - if not just write the pattern */
	if (rc == 0 && dirent.type == FS_DIR_ENTRY_FILE && dirent.size == 0) {
		LOG_INF("Test file: %s not found, create one!",
			fname);
		init_pattern(file_test_pattern, sizeof(file_test_pattern));
	} else {
		rc = fs_read(&file, file_test_pattern,
			     sizeof(file_test_pattern));
		if (rc < 0) {
			LOG_ERR("FAIL: read %s: [rd:%d]",
				fname, rc);
			goto out;
		}
		incr_pattern(file_test_pattern, sizeof(file_test_pattern), 0x1);
	}

	LOG_PRINTK("------ FILE: %s ------\n", fname);
	print_pattern(file_test_pattern, sizeof(file_test_pattern));

	rc = fs_seek(&file, 0, FS_SEEK_SET);
	if (rc < 0) {
		LOG_ERR("FAIL: seek %s: %d", fname, rc);
		goto out;
	}

	rc = fs_write(&file, file_test_pattern, sizeof(file_test_pattern));
	if (rc < 0) {
		LOG_ERR("FAIL: write %s: %d", fname, rc);
	}

 out:
	ret = fs_close(&file);
	if (ret < 0) {
		LOG_ERR("FAIL: close %s: %d", fname, ret);
		return ret;
	}

	return (rc < 0 ? rc : 0);
}

static int littlefs_flash_erase(unsigned int id)
{
	const struct flash_area *pfa;
	int rc;

	rc = flash_area_open(id, &pfa);
	if (rc < 0) {
		LOG_ERR("FAIL: unable to find flash area %u: %d\n",
			id, rc);
		return rc;
	}

	LOG_PRINTK("Area %u at 0x%x on %s for %u bytes\n",
		   id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
		   (unsigned int)pfa->fa_size);

	/* Optional wipe flash contents */
	if (IS_ENABLED(CONFIG_APP_WIPE_STORAGE)) {
		rc = flash_area_erase(pfa, 0, pfa->fa_size);
		LOG_ERR("Erasing flash area ... %d", rc);
	}

	flash_area_close(pfa);
	return rc;
}
//FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
char storage_buffer[2048];
void * storage=storage_buffer;
static struct fs_mount_t lfs_storage_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = "/lfs",
};
struct fs_mount_t *mountpoint =&lfs_storage_mnt;

static int littlefs_mount(struct fs_mount_t *mp)
{
	int rc;

	rc = littlefs_flash_erase((uintptr_t)mp->storage_dev);
	if (rc < 0) {
		return rc;
		printf("Coud not mount %i\n",rc);
	}
	rc = fs_mount(mp);
	if (rc < 0) {
		LOG_PRINTK("FAIL: mount id %" PRIuPTR " at %s: %d\n",
		       (uintptr_t)mp->storage_dev, mp->mnt_point, rc);
		return rc;
	}
	LOG_PRINTK("%s mount: %d\n", mp->mnt_point, rc);
	return 0;
}

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
	char * buffer;
	int res;
	int loop;
	int flash_size=0x1000;
	const struct flash_area *my_area;
	const struct flash_area *my_area1;
	int err;	
	char *str="STRINGLOGer\n";
	shell_print(sh, "Checking nvram");
 LOG_INF("LOG INfo foo state %s ",state.settings.callsign);
	LOG_WRN("LOG Warn %s", str);
	err = flash_area_open(FIXED_PARTITION_ID(storage_partition), &my_area1);
	if (err != 0) {
		shell_print(sh, "failed");
		
	} else {
		shell_print(sh, "Offset:%x  size:%x",FIXED_PARTITION_OFFSET(storage_partition), FIXED_PARTITION_SIZE(storage_partition));
	//	shell_print(sh, "label %s ", FLASH_AREA_LABEL_EXISTS(storage_partition));
		shell_print(sh, "Reading  ");
		buffer=malloc(flash_size);
		printf("res read %i\n",flash_area_read(my_area1,FIXED_PARTITION_OFFSET(storage_partition),buffer,flash_size));
		for (loop=0;loop<32;loop++)
		{
			printf("%c",buffer[loop]);
		}
		printf("\n");
		for (loop=0;loop<32;loop++)
		{
			printf(":%2x",buffer[loop]);
		}
		if (argv[1] != NULL)
		{
		shell_print(sh, "write flash");
		sprintf(buffer,"%s",argv[1]);
		for (loop=0;loop<32;loop++)
		{
			printf(":%2x",buffer[loop]);
		}
		res=flash_area_erase(my_area1,FIXED_PARTITION_OFFSET(storage_partition),flash_size);
		shell_print(sh, "\nres erase %i",res);
		flash_area_write(my_area1,FIXED_PARTITION_OFFSET(storage_partition),buffer,1024);
		}
		free(buffer);
		flash_area_close(my_area1);
	}
	
	return 0;
}


static int cmd_radio_nvramchannel(const struct shell *sh, size_t argc, char **argv)
{
	char * buffer;
	int res;
	int loop;
	int flash_size=0x1000;
	const struct flash_area *my_area;
	const struct flash_area *my_area1;
	int err;	
	char *str="STRINGLOGer\n";
	shell_print(sh, "Checking nvram");
 	LOG_INF("LOG INfo foo state %s ",state.settings.callsign);
	LOG_WRN("LOG Warn %s", str);
	err = flash_area_open(FIXED_PARTITION_ID(storage_partition), &my_area1);
	if (err != 0) {
		shell_print(sh, "failed");
		
	} else {
		shell_print(sh, "Offset:%x  size:%x",FIXED_PARTITION_OFFSET(storage_partition)+0x1000, FIXED_PARTITION_SIZE(storage_partition));
	//	shell_print(sh, "label %s ", FLASH_AREA_LABEL_EXISTS(storage_partition));
		shell_print(sh, "Reading  ");
		buffer=malloc(flash_size);
		printf("res read %i\n",flash_area_read(my_area1,FIXED_PARTITION_OFFSET(storage_partition)+0x1000,buffer,flash_size));
		for (loop=0;loop<32;loop++)
		{
			printf("%c",buffer[loop]);
		}
		printf("\n");
		for (loop=0;loop<32;loop++)
		{
			printf(":%2x",buffer[loop]);
		}
		if (argv[1] != NULL)
		{
		shell_print(sh, "write flash");
		sprintf(buffer,"%s",argv[1]);
		for (loop=0;loop<32;loop++)
		{
			printf(":%2x",buffer[loop]);
		}
		res=flash_area_erase(my_area1,FIXED_PARTITION_OFFSET(storage_partition)+0x1000,flash_size);
		shell_print(sh, "\nres erase %i",res);
		flash_area_write(my_area1,FIXED_PARTITION_OFFSET(storage_partition)+0x1000,buffer,1024);
		}
		free(buffer);
		flash_area_close(my_area1);
	}
	
	return 0;
}


static int cmd_radio_wrnv(const struct shell *sh, size_t argc, char **argv)
{
	char * buffer;
	int res,rc;
	const struct flash_area *my_area1;
	int err;	
    LOG_INF("Callsign from   %s ",state.settings.callsign);
	
    LOG_INF("Bright   %i ",state.settings.brightness);
	err = flash_area_open(FIXED_PARTITION_ID(storage_partition), &my_area1);
//	shell_print(sh, "Offset:%x  size:%x",FIXED_PARTITION_OFFSET(storage_partition), FIXED_PARTITION_SIZE(storage_partition));
	//printf("res read %i\n",flash_area_read(my_area1,FIXED_PARTITION_OFFSET(storage_partition), &state.settings, sizeof(state.settings) ) );
	rc=flash_area_erase(my_area1,FIXED_PARTITION_OFFSET(storage_partition),0x1000);
	res= flash_area_write(my_area1,FIXED_PARTITION_OFFSET(storage_partition),&state.settings, sizeof(state.settings));
  	shell_print(sh,"erase stat %i ",rc);
	LOG_INF("write stat %i ",res);
	shell_print(sh," state %i ",state.settings.brightness);
   shell_print(sh,"Callsign from after  settings  %s ",state.settings.callsign);	
	flash_area_close(my_area1);

	return 0;
}
static int cmd_radio_rdnv(const struct shell *sh, size_t argc, char **argv)
{
	char * buffer;
	int res,rc;
	int loop;
	int flash_size=0x4000;
	const struct flash_area *my_area;
	const struct flash_area *my_area1;
	int err;	
	char *str="STRINGLOGer\n";
    LOG_INF("Callsign from settings:%s <",state.settings.callsign);
	
	err = flash_area_open(FIXED_PARTITION_ID(storage_partition), &my_area1);
	//shell_print(sh, "Offset:%x  size:%x",FIXED_PARTITION_OFFSET(storage_partition), FIXED_PARTITION_SIZE(storage_partition));
	printf("res read %i %i \n",flash_area_read(my_area1,FIXED_PARTITION_OFFSET(storage_partition), &state.settings, sizeof(state.settings) ), sizeof(state.settings) );
	//res=flash_area_erase(my_area1,FIXED_PARTITION_OFFSET(storage_partition),flash_size);
	//flash_area_write(my_area1,FIXED_PARTITION_OFFSET(storage_partition),buffer,1024);
    LOG_INF("Callsign from after  settings  %s ",state.settings.callsign);
	flash_area_close(my_area1);

	return 0;
}
static int cmd_radio_lfs(const struct shell *sh, size_t argc, char **argv)
{
	char * buffer;
	int res,rc;
	int loop;

	rc = littlefs_mount(mountpoint);
	if (rc < 0) {
		shell_print(sh, "mount fail %i",res);
	
	}else
	{
		shell_print(sh, "mount good %i",res);
	}
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
	SHELL_CMD(nvc, NULL, "Checking nvram", cmd_radio_nvramchannel),
	SHELL_CMD(dtmf, NULL, "Set dtmf register", cmd_radio_dsp),
	SHELL_CMD(tone, NULL, "Cset tone 1 and tone 2", cmd_radio_tone),
	SHELL_CMD(loadset, NULL, "Write test 2", cmd_radio_rdnv),
	SHELL_CMD(writeset, NULL, "Write test 2", cmd_radio_wrnv),
	
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
