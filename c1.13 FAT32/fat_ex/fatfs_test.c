#include <stdio.h>
#include <string.h>
#include "xdisk.h"
#include "xfat.h"

extern xdisk_driver_t vdisk_driver;

const char* disk_path_test = "disk_test.img";
const char* disk_path = "disk.img";
static u32_t write_buffer[160 * 1024];
static u32_t read_buffer[160 * 1024];

xdisk_t disk;

int disk_io_test(void) {
	int err;
	xdisk_t disk_test;

	memset(read_buffer, 0, sizeof(read_buffer));

	err = xdisk_open(&disk_test, "vdisk_test", &vdisk_driver, (void*)disk_path_test);
	if (err) {
		printf("open disk failed!\n");
		return -1;
	}

	err = xdisk_write_sector(&disk_test, (u8_t*)write_buffer, 0, 2);
	if (err) {
		printf("disk write failed!\n");
		return -1;
	}

	err = xdisk_read_sector(&disk_test, (u8_t*)read_buffer, 0, 2);
	if (err) {
		printf("disk read failed!\n");
		return -1;
	}

	err = memcmp((u8_t*)read_buffer, (u8_t*)write_buffer, disk_test.sector_size * 2);
	if (err != 0) {
		printf("data not equal!\n");
		return -1;
	}

	err = xdisk_close(&disk_test);
	if (err) {
		printf("close disk failed!\n");
		return -1;
	}

	printf("disk io test is OK!\n");
	return FS_ERR_OK;
}

int disk_part_test(void) {
	u32_t count = 0;
	xfat_err_t err;

	printf("partition read test...\n");

	err = xdisk_get_part_count(&disk, &count);
	if (err) {
		printf("partition count detect failed!\n");
		return err;
	}

	for (int i = 0; i < count; i++) {
		xdisk_part_t part;
		
		int err = xdisk_get_part(&disk, &part, i);
		if (err < 0) {
			printf("read partition failed!\n");
			return -1;
		}

		printf("no: %d, start: %d, count: %d, capacity: %.0f M\n", 
			i, part.start_sector, part.total_sector, part.total_sector * disk.sector_size / 1024 / 1024.0);
	}

	printf("partition count: %d\n", count);
	return 0;
}

int main(void) {
	xfat_err_t err;

	for (int i = 0; i < sizeof(write_buffer) / sizeof(u32_t); i++) {
		write_buffer[i] = i;
	}

	//err = disk_io_test();
	//if (err) return err;

	err = xdisk_open(&disk, "vdisk", &vdisk_driver, (void*)disk_path);
	if (err) {
		printf("open disk failed!\n");
		return -1;
	}

	err = disk_part_test();
	if (err) return err;

	err = xdisk_close(&disk);
	if (err) {
		printf("close disk failed!\n");
		return -1;
	}

	printf("Test End!\n");
	return 0;
}
