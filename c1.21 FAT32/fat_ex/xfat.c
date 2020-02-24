#include "xfat.h"

extern u8_t temp_buffer[512];

static xfat_err_t parse_fat_header(xfat_t* xfat, dbr_t* dbr) {
	xdisk_part_t* xdisk_part = xfat->disk_part;

	xfat->fat_tbl_sectors = dbr->fat32.BPB_FATSz32;

	if (dbr->fat32.BPB_ExtFlags & (1 << 7)) {
		u32_t table = dbr->fat32.BPB_ExtFlags & 0xF;
		xfat->fat_start_sector = dbr->bpb.BPB_RsvdSecCnt + xdisk_part->start_sector + table * xfat->fat_tbl_sectors;
		xfat->fat_tbl_nr = 1;
	}
	else {
		xfat->fat_start_sector = dbr->bpb.BPB_RsvdSecCnt + xdisk_part->start_sector;
		xfat->fat_tbl_nr = dbr->bpb.BPB_NumFATs;
	}

	xfat->total_sectors = dbr->bpb.BPB_TotSec32;
	return FS_ERR_OK;
}

xfat_err_t xfat_open(xfat_t* xfat, xdisk_part_t* xdisk_part) {
	dbr_t* dbr = (dbr_t*)temp_buffer;
	xdisk_t* xdisk = xdisk_part->disk;
	xfat->disk_part = xdisk_part;

	int err = xdisk_read_sector(xdisk, (u8_t*)dbr, xdisk_part->start_sector, 1);
	if (err) return err;

	err = parse_fat_header(xfat, dbr);
	if (err) return err;

	return FS_ERR_OK;
}
