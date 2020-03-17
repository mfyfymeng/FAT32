#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xdisk.h"
#include "xfat.h"

extern xdisk_driver_t vdisk_driver;

const char* disk_path_test = "disk_test.img";
const char* disk_path = "disk.img";
static u32_t write_buffer[160 * 1024];
static u32_t read_buffer[160 * 1024];

xdisk_t disk;
xdisk_part_t disk_part;
xfat_t xfat;

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

	for (u32_t i = 0; i < count; i++) {
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

void show_dir_info(diritem_t* dir_item) {
	char file_name[12];
	u8_t attr = dir_item->DIR_Attr;

	memset(file_name, 0, sizeof(file_name));
	memcpy(file_name, dir_item->DIR_Name, 11);
	if (file_name[0] == 0x05) {
		file_name[0] = 0xE5;
	}
	printf("\n name: %s, ", file_name);

	printf("\n\t");
	if (attr & DIRITEM_ATTR_READ_ONLY)
		printf("readonly, ");

	if (attr & DIRITEM_ATTR_HIDDEN)
		printf("hidden, ");

	if (attr & DIRITEM_ATTR_SYSTEM)
		printf("system, ");

	if (attr & DIRITEM_ATTR_DIRECTORY)
		printf("directory, ");

	if (attr & DIRITEM_ATTR_ARCHIVE)
		printf("archive, ");

	printf("\n\tcreate: %d-%d-%d, ", 
		dir_item->DIR_CrtDate.year_from_1980 + 1980, dir_item->DIR_CrtDate.month, dir_item->DIR_CrtDate.day);
	printf("\n\tcreate time: %d-%d-%d, ",
		dir_item->DIR_CrtTime.hour, dir_item->DIR_CrtTime.minute, dir_item->DIR_CrtTime.second_2 * 2 + dir_item->DIR_CrtTimeTeenth / 100);
	
	printf("\n\tlast write: %d-%d-%d, ",
		dir_item->DIR_WrtDate.year_from_1980 + 1980, dir_item->DIR_WrtDate.month, dir_item->DIR_WrtDate.day);
	printf("\n\tlast write time: %d-%d-%d, ",
		dir_item->DIR_WrtTime.hour, dir_item->DIR_WrtTime.minute, dir_item->DIR_WrtTime.second_2 * 2);

	printf("\n\tlast acc: %d-%d-%d, ",
		dir_item->DIR_LastAccDate.year_from_1980 + 1980, dir_item->DIR_LastAccDate.month, dir_item->DIR_LastAccDate.day);

	printf("\n\t size %d KB, ", dir_item->DIR_FileSize / 1024);
	printf("\n\t cluster %d, ", (dir_item->DIR_FstClusHI << 16) | dir_item->DIR_FstClusL0);

	printf("\n");
}

int fat_dir_test(void) {
	u8_t* cluster_buffer;
	u32_t curr_cluster;
	int index = 0;

	cluster_buffer = (u8_t*)malloc(xfat.cluster_byte_size);
	curr_cluster = xfat.root_cluster;

	while (is_cluster_valid(curr_cluster)) {
		int err = read_cluster(&xfat, cluster_buffer, curr_cluster, 1);
		if (err) {
			printf("read cluster %d failed!\n", curr_cluster);
			return -1;
		}

		diritem_t* dir_item = (diritem_t*)cluster_buffer;
		for (u32_t i = 0; i < xfat.cluster_byte_size / sizeof(diritem_t); i++) {
			u8_t* name = (u8_t*)(dir_item[i].DIR_Name);
			if (name[0] == DIRITEM_NAME_FREE)
				continue;
			else if (name[0] == DIRITEM_ATTR_LONG_NAME)
				continue;
			else if (name[0] == DIRITEM_NAME_END)
				break;

			++index;
			printf("no: %d, ", index);
			show_dir_info(&dir_item[i]);
		}

		err = get_next_cluster(&xfat, curr_cluster, &curr_cluster);
		if (err) {
			printf("get next cluster failed! current cluster: %d\n", curr_cluster);
			return -1;
		}
	}
	
	return 0;
}

int fat_file_test(void) {
	u8_t* cluster_buffer;
	u32_t curr_cluster;
	int size = 0;

	cluster_buffer = (u8_t*)malloc(xfat.cluster_byte_size);
	curr_cluster = 4565;

	while (is_cluster_valid(curr_cluster)) {
		int err = read_cluster(&xfat, cluster_buffer, curr_cluster, 1);
		if (err) {
			printf("read cluster %d failed!\n", curr_cluster);
			return -1;
		}

		cluster_buffer[xfat.cluster_byte_size + 1] = '\0';

		printf("%s", (char*)cluster_buffer);

		size += xfat.cluster_byte_size;

		err = get_next_cluster(&xfat, curr_cluster, &curr_cluster);
		if (err) {
			printf("get next cluster failed! current cluster: %d\n", curr_cluster);
			return -1;
		}
	}

	printf("\nfile size: %d\n", size);
	return 0;
}

void show_file_info(xfileinfo_t* info) {
	printf("\n\n name: %s, ", info->file_name);

	switch (info->type)
	{
	case FAT_FILE:
		printf("file, ");
		break;
	case FAT_DIR:
		printf("dir, ");
		break;
	case FAT_VOL:
		printf("vol, ");
		break;
	default:
		printf("unknown, ");
		break;
	}

	printf("\n\tcreate: %d-%d-%d, ", info->create_time.year, info->create_time.month, info->create_time.day);
	printf("\n\ttime: %d-%d-%d, ", info->create_time.hour, info->create_time.minute, info->create_time.second);

	printf("\n\tlast write: %d-%d-%d, ", info->modify_time.year, info->modify_time.month, info->modify_time.day);
	printf("\n\ttime: %d-%d-%d, ", info->modify_time.hour, info->modify_time.minute, info->modify_time.second);

	printf("\n\tlast acc: %d-%d-%d, ", info->last_acctime.year, info->last_acctime.month, info->last_acctime.day);

	printf("\n\tsize: %d kB, ", info->size / 1024);

	printf("\n");
}

int list_sub_file(xfile_t* file, int curr_depth) {
	xfat_err_t err;
	xfileinfo_t fileinfo;

	err = xdir_first_file(file, &fileinfo);
	if (err) return err;

	do {
		xfile_t sub_file;
		
		if (fileinfo.type == FAT_DIR) {
			for (int i = 0; i < curr_depth; i++) {
				printf("-");
			}
			printf("%s\n", fileinfo.file_name);

			err = xfile_open_sub(file, fileinfo.file_name, &sub_file);
			if (err < 0) return err;

			err = list_sub_file(&sub_file, curr_depth + 1);
			if (err < 0) return err;

			xfile_close(&sub_file);
		}
		else {
			for (int i = 0; i < curr_depth; i++) {
				printf("-");
			}
			printf("%s\n", fileinfo.file_name);
		}


	} while ((err = xdir_next_file(file, &fileinfo)) == 0);

	return err;
}

int dir_trans_test(void) {
	xfat_err_t err;
	xfile_t top_dir;
	xfileinfo_t fileinfo;

	printf("\ntrans dir test!\n");

	err = xfile_open(&xfat, &top_dir, "/read/..");
	if (err < 0) {
		printf("open directory failed!\n");
		return err;
	}

	err = xdir_first_file(&top_dir, &fileinfo);
	if (err < 0) {
		printf("get file info failed!\n");
		return err;
	}

	show_file_info(&fileinfo);

	while ((err = xdir_next_file(&top_dir, &fileinfo)) == 0) {
		show_file_info(&fileinfo);
	}
	if (err < 0) {
		printf("get file info failed!\n");
		return err;
	}

	printf("\ntry to list all sub files\n");

	err = list_sub_file(&top_dir, 0);
	if (err < 0) {
		printf("list file failed!\n");
		return err;
	}

	err = xfile_close(&top_dir);
	if (err < 0) {
		printf("close file failed!\n");
		return err;
	}

	printf("file trans test ok!\n");
	return FS_ERR_OK;
}

int fs_open_test(void) {
	xfat_err_t err;
	xfile_t file;
	const char* not_exist_path = "/file_not_exist.txt";
	const char* exist_path = "/12345678ABC";
	const char* file1 = "open/file.txt";
	const char* file2 = "open/a0/a1/a2/a3/a4/a5/a6/a7/a8/a9/a10/a11/a12/a13/a14/a15/a16/a17/a18/a19/file.txt";

	printf("fs open test...\n");

	err = xfile_open(&xfat, &file, "/");
	if (err) {
		printf("open file failed %s\n", "/");
		return -1;
	}
	xfile_close(&file);

	err = xfile_open(&xfat, &file, not_exist_path);
	if (err == 0) {
		printf("open file failed %s\n", not_exist_path);
		return -1;
	}
	xfile_close(&file);

	err = xfile_open(&xfat, &file, exist_path);
	if (err) {
		printf("open file failed %s\n", exist_path);
		return -1;
	}
	xfile_close(&file);

	err = xfile_open(&xfat, &file, file1);
	if (err) {
		printf("open file failed %s\n", file1);
		return err;
	}
	xfile_close(&file);

	err = xfile_open(&xfat, &file, file2);
	if (err) {
		printf("open file failed %s\n", file2);
		return -1;
	}
	xfile_close(&file);

	printf("open file test OK!\n");

	return 0;
}

int file_read_and_check(const char* path, xfile_size_t elem_size, xfile_size_t e_count) {
	xfile_t file;
	xfile_size_t read_count;

	xfat_err_t err = xfile_open(&xfat, &file, path);
	if (err != FS_ERR_OK) {
		printf("open file failed! %s\n", path);
		return -1;
	}
	
	if ((read_count = xfile_read(read_buffer, elem_size, e_count, &file)) > 0) {
		u32_t num_start = 0;
		xfile_size_t bytes_count = read_count * elem_size;
		
		for (u32_t i = 0; i < bytes_count; i += 4) {
			if (read_buffer[i / 4] != num_start++) {
				printf("read file failed!\n");
				return -1;
			}
		}
	}
	
	if (xfile_error(&file) < 0) {
		printf("read failed!\n");
		return -1;
	}

	xfile_close(&file);

	return FS_ERR_OK;
}

int fs_read_test(void) {
	const char* file_0b_path = "/read/0b.bin";
	const char* file_1MB_path = "/read/1MB.bin";
	xfat_err_t err;

	printf("\nfile read test\n");
	memset(read_buffer, 0, sizeof(read_buffer));

	err = file_read_and_check(file_0b_path, 32, 1);
	if (err < 0) {
		printf("read failed!\n");
		return err;
	}

	err = file_read_and_check(file_1MB_path, disk.sector_size - 32, 1);
	if (err < 0) {
		printf("read failed!\n");
		return err;
	}

	err = file_read_and_check(file_1MB_path, disk.sector_size, 1);
	if (err < 0) {
		printf("read failed!\n");
		return err;
	}

	err = file_read_and_check(file_1MB_path, disk.sector_size + 14, 1);
	if (err < 0) {
		printf("read failed!\n");
		return err;
	}

	err = file_read_and_check(file_1MB_path, xfat.cluster_byte_size + 32, 1);
	if (err < 0) {
		printf("read failed!\n");
		return err;
	}

	err = file_read_and_check(file_1MB_path, 2 * xfat.cluster_byte_size + 32, 1);
	if (err < 0) {
		printf("read failed!\n");
		return err;
	}

	printf("\nfile read test ok!\n");
	return FS_ERR_OK;
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

	err = xdisk_get_part(&disk, &disk_part, 1);
	if (err < 0) {
		printf("read partition failed!\n");
		return -1;
	}

	err = xfat_open(&xfat, &disk_part);
	if (err < 0) return err;

	//err = fat_dir_test();
	//if (err) return err;

	//err = fat_file_test();
	//if (err) return err;

	//err = fs_open_test();
	//if (err) return err;

	//err = dir_trans_test();
	//if (err) return err;

	err = fs_read_test();
	if (err < 0) {
		printf("read test failed!\n");
		return -1;
	}

	err = xdisk_close(&disk);
	if (err) {
		printf("close disk failed!\n");
		return -1;
	}

	printf("Test End!\n");
	return 0;
}
