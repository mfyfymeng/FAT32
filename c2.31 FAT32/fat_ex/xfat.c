#include <stdlib.h>
#include "xfat.h"

extern u8_t temp_buffer[512];

#define is_path_sep(ch)                     (((ch) == '\\') || ((ch) == '/'))
#define xfat_get_disk(xfat)                 ((xfat)->disk_part->disk)
#define to_sector(disk, offset)             ((offset) / (disk)->sector_size)
#define to_sector_offset(disk, offset)      ((offset) % (disk)->sector_size)


static xfat_err_t parse_fat_header(xfat_t* xfat, dbr_t* dbr) {
	xdisk_part_t* xdisk_part = xfat->disk_part;

	xfat->root_cluster = dbr->fat32.BPB_RootClus;
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

	xfat->sec_per_cluster = dbr->bpb.BPB_SecPerClus;
	xfat->total_sectors = dbr->bpb.BPB_TotSec32;
	xfat->cluster_byte_size = xfat->sec_per_cluster * dbr->bpb.BPB_BytsPerSec;

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

	xfat->fat_buffer = (u8_t*)malloc(xfat->fat_tbl_sectors * xdisk->sector_size);
	err = xdisk_read_sector(xdisk, (u8_t*)xfat->fat_buffer, xfat->fat_start_sector, xfat->fat_tbl_sectors);
	if (err < 0) return err;

	return FS_ERR_OK;
}

u32_t cluster_first_sector(xfat_t* xfat, u32_t cluster_no) {
	u32_t data_start_sector = xfat->fat_start_sector + xfat->fat_tbl_sectors * xfat->fat_tbl_nr;
	return data_start_sector + (cluster_no - 2) * xfat->sec_per_cluster;
}

xfat_err_t read_cluster(xfat_t* xfat, u8_t* buffer, u32_t cluster, u32_t count) {
	xfat_err_t err = 0;
	u8_t* curr_buffer = buffer;
	u32_t curr_sector = cluster_first_sector(xfat, cluster);

	for (u32_t i = 0; i < count; i++) {
		err = xdisk_read_sector(xfat_get_disk(xfat), curr_buffer, curr_sector, xfat->sec_per_cluster);
		if (err < 0) return err;

		curr_buffer += xfat->cluster_byte_size;
		curr_sector += xfat->sec_per_cluster;
	}

	return FS_ERR_OK;
}

static u8_t is_filename_match(const char* name_in_dir, const char* to_find_name) {
	return memcmp(to_find_name, name_in_dir, SFN_LEN) == 0;
}

static const char* skip_first_path_sep(const char* path) {
	const char* c = path;
	while (is_path_sep(*c)) {
		c++;
	}
	return c;
}

static const char* get_child_path(const char* dir_path) {
	const char* c = skip_first_path_sep(dir_path);
	while ((*c != '\0') && (!is_cluster_valid(*c))) {
		c++;
	}
	return (*c == '\0') ? ((const char*)0) : (c + 1);
}

static xfile_type_t get_file_type(const diritem_t* dir_item) {
	xfile_type_t type;

	if (dir_item->DIR_Attr & DIRITEM_ATTR_VOLUME_ID)
		type = FAT_VOL;
	else if (dir_item->DIR_Attr & DIRITEM_ATTR_DIRECTORY)
		type = FAT_DIR;
	else
		type = FAT_FILE;

	return type;
}

static u32_t get_diritem_cluster(diritem_t* item) {
	return (item->DIR_FstClusHI << 16) | item->DIR_FstClusL0;
}

int is_cluster_valid(u32_t cluster) {
	cluster &= 0x0FFFFFFF;
	return (cluster < 0x0FFFFFF0) && (cluster >= 2);
}

xfat_err_t get_next_cluster(xfat_t* xfat, u32_t curr_cluster_no, u32_t* next_cluster) {
	if (is_cluster_valid(curr_cluster_no)) {
		cluster32_t* cluster32_buf = (cluster32_t*)xfat->fat_buffer;
		*next_cluster = cluster32_buf[curr_cluster_no].s.next;
	}
	else
		*next_cluster = CLUSTER_INVALID;

	return FS_ERR_OK;
}

static xfat_err_t locate_file_dir_item(xfat_t* xfat, u32_t* dir_cluster, u32_t* cluster_offset, const char* path, u32_t* moved_bytes, diritem_t** r_diritem) {
	xfat_err_t err;
	u32_t curr_cluster = *dir_cluster;
	xdisk_t* xdisk = xfat_get_disk(xfat);
	u32_t initial_sector = to_sector(xdisk, *cluster_offset);
	u32_t initial_offset = to_sector_offset(xdisk, *cluster_offset);
	u32_t r_moved_bytes = 0;

	do {
		u32_t start_sector = cluster_first_sector(xfat, curr_cluster);

		for (u32_t i = 0; i < xfat->sec_per_cluster; i++) {
			err = xdisk_read_sector(xdisk, temp_buffer, start_sector + i, 1);
			if (err < 0) return err;

			for (u32_t j = initial_offset / sizeof(diritem_t); j < xdisk->sector_size / sizeof(diritem_t); j++) {
				diritem_t* dir_item = ((diritem_t*)temp_buffer) + j;
				if (dir_item->DIR_Name[0] == DIRITEM_NAME_END)
					return FS_ERR_EOF;
				else if (dir_item->DIR_Name[0] == DIRITEM_NAME_FREE) {
					r_moved_bytes += sizeof(diritem_t);
					continue;
				}
					
				if ((path == (const char*)0) || (*path == 0) || is_filename_match((const char *)dir_item->DIR_Name, path)) {
					u32_t total_offset = i * xdisk->sector_size + j * sizeof(diritem_t);
					
					*dir_cluster = curr_cluster;
					*moved_bytes = r_moved_bytes + sizeof(diritem_t);
					*cluster_offset = total_offset;
					if (r_diritem) 
						*r_diritem = dir_item;
					
					return FS_ERR_OK;
				}
			}
		}

		err = get_next_cluster(xfat, curr_cluster, &curr_cluster);
		if (err < 0) return err;

		initial_sector = 0;
		initial_offset = 0;
	} while (is_cluster_valid(curr_cluster));

	return FS_ERR_EOF;
}

static xfat_err_t open_sub_file(xfat_t* xfat, u32_t dir_cluster, xfile_t* file, const char* path) {
	u32_t parent_cluster = dir_cluster;
	u32_t parent_cluster_offset = 0;
	diritem_t* dir_item = (diritem_t*)0;
	u32_t file_start_cluster = 0;

	path = skip_first_path_sep(path);

	if ((path != '\0') && (*path != '\0')) {
		const char* curr_path = path;

		while (curr_path != (const char*)0) {
			u32_t moved_bytes = 0;
			xfat_err_t err = locate_file_dir_item(xfat, &parent_cluster, &parent_cluster_offset, curr_path, &moved_bytes, &dir_item);
			if (err < 0) return err;
			if (dir_item == (diritem_t*)0) return FS_ERR_NONE;
			
			curr_path = get_child_path(curr_path);
			if (curr_path != (const char*)0) {
				parent_cluster = get_diritem_cluster(dir_item);
				parent_cluster_offset = 0;
			}
			else
				file_start_cluster = get_diritem_cluster(dir_item);
		}

		file->size = dir_item->DIR_FileSize;
		file->type = get_file_type(dir_item);
		file->start_cluster = file_start_cluster;
		file->curr_cluster = file_start_cluster;
	}
	else {
		file->size = 0;
		file->type = FAT_DIR;
		file->start_cluster = dir_cluster;
		file->curr_cluster = dir_cluster;
	}

	file->xfat = xfat;
	file->pos = 0;
	file->err = FS_ERR_OK;
	file->attr = 0;
	
	return FS_ERR_OK;
}

xfat_err_t xfile_open(xfat_t* xfat, xfile_t* file, const char* path) {
	return open_sub_file(xfat, xfat->root_cluster, file, path);
}

xfat_err_t xfile_close(xfile_t* file) {
	return FS_ERR_OK;
}
