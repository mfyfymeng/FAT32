#include "xdisk.h"

u8_t temp_buffer[512];

xfat_err_t xdisk_open(xdisk_t* disk, const char* name, xdisk_driver_t* driver, void* init_data) {
	xfat_err_t err;

	disk->driver = driver;
	
	err = disk->driver->open(disk, init_data);
	if (err < 0) return err;

	disk->name = name;
	return FS_ERR_OK;
}

xfat_err_t xdisk_close(xdisk_t* disk) {
	xfat_err_t err;

	err = disk->driver->close(disk);
	if (err < 0) return err;

	return FS_ERR_OK;
}

xfat_err_t xdisk_read_sector(xdisk_t* disk, u8_t* buffer, u32_t start_sector, u32_t count) {
	xfat_err_t err;

	if (start_sector + count >= disk->total_sector) return FS_ERR_PARAM;

	err = disk->driver->read_sector(disk, buffer, start_sector, count);
	return FS_ERR_OK;
}

xfat_err_t xdisk_write_sector(xdisk_t* disk, u8_t* buffer, u32_t start_sector, u32_t count) {
	xfat_err_t err;

	if (start_sector + count >= disk->total_sector) return FS_ERR_PARAM;

	err = disk->driver->write_sector(disk, buffer, start_sector, count);
	return FS_ERR_OK;
}

static xfat_err_t disk_get_extend_part_count(xdisk_t* disk, u32_t start_sector, u32_t* count) {
	int r_count = 0;
	u8_t* disk_buffer = temp_buffer;
	u32_t ext_start_sector = start_sector;

	do {
		mbr_part_t* part;
		int err = xdisk_read_sector(disk, disk_buffer, start_sector, 1);
		if (err) return err;

		part = ((mbr_t*)disk_buffer)->part_info;
		if (part->system_id == FS_NOT_VALID)
			break;

		r_count++;
		if ((++part)->system_id != FS_EXTEND)
			break;

		start_sector = ext_start_sector + part->relative_sectors;
	} while (1);

	*count = r_count;
	return FS_ERR_OK;
}

xfat_err_t xdisk_get_part_count(xdisk_t* disk, u32_t* count) {
	u32_t r_count = 0, ext_count = 0;
	mbr_part_t* part;
	u8_t* disk_buffer = temp_buffer;
	u8_t extend_part_flag = 0;
	u32_t start_sector[4];

	int err = xdisk_read_sector(disk, disk_buffer, 0, 1);
	if (err) return err;

	part = ((mbr_t*)disk_buffer)->part_info;
	for (int i = 0; i < MBR_PRIMARY_PART_NR; i++, part++) {
		if (part->system_id == FS_NOT_VALID)
			continue;
		else if (part->system_id == FS_EXTEND) {
			start_sector[i] = part->relative_sectors;
			extend_part_flag |= (1 << i);
		}
		else
			r_count++;
	}

	if (extend_part_flag) {
		for (int i = 0; i < MBR_PRIMARY_PART_NR; i++) {
			if (extend_part_flag & (1 << i)) {
				err = disk_get_extend_part_count(disk, start_sector[i], &ext_count);
				if (err) return err;
				r_count += ext_count;
			}
			
		}
	}

	*count = r_count;
	return FS_ERR_OK;
}

