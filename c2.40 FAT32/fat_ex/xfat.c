#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "xfat.h"

extern u8_t temp_buffer[512];

#define DOT_FILE        ".          "
#define DOT_DOT_FILE    "..         "

#define is_path_sep(ch)                     (((ch) == '\\') || ((ch) == '/'))
#define file_get_disk(file)                 ((file)->xfat->disk_part->disk)
#define xfat_get_disk(xfat)                 ((xfat)->disk_part->disk)
#define to_sector(disk, offset)             ((offset) / (disk)->sector_size)
#define to_sector_offset(disk, offset)      ((offset) % (disk)->sector_size)
#define to_cluster_offset(xfat, pos)        ((pos) % (xfat)->cluster_byte_size)

u32_t to_phy_sector(xfat_t* xfat, u32_t cluster, u32_t cluster_offset) {
	xdisk_t* disk = xfat_get_disk(xfat);
	return cluster_first_sector(xfat, cluster) + to_sector(disk, cluster_offset);
}

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

static xfat_err_t to_sfn(char* dest_name, const char* my_name) {
	int name_len;
	const char* ext_dot;
	const char* p;
	char* dest = dest_name;
	int ext_existed;

	memset(dest_name, ' ', SFN_LEN);

	while (is_path_sep(*my_name)) {
		my_name++;
	}

	ext_dot = my_name;
	p = my_name;
	name_len = 0;
	ext_existed = 0;
	while ((*p != '\0') && (!is_path_sep(*p))) {
		if (*p == '.')
			ext_dot = p;
	
		p++;
		name_len++;
	}

	ext_existed = (ext_dot > my_name) && (ext_dot < (my_name + name_len - 1));
	
	p = my_name;
	for (int i = 0; ((i < SFN_LEN) && (*p != '\0') && !is_path_sep(*p)); i++) {
		if (ext_existed) {
			if (p == ext_dot) {
				dest = dest_name + 8;
				p++;
				i--;
				continue;
			}
			else if (p < ext_dot)
				*dest++ = toupper(*p++);
			else
				*dest++ = toupper(*p++);
		}
		else
			*dest++ = toupper(*p++);
	}

	return FS_ERR_OK;
}

static u8_t get_sfn_case_cfg(const char* sfn_new) {
	u8_t case_cfg = 0;
	int name_len;
	const char* ext_dot;
	const char* p;
	const char* src_name = sfn_new;
	int ext_existed;
	
	while (is_path_sep(*src_name)) {
		src_name++;
	}

	ext_dot = src_name;
	p = src_name;
	name_len = 0;
	ext_existed = 0;
	while ((*p != '\0') && (!is_path_sep(*p))) {
		if (*p == '.')
			ext_dot = p;

		p++;
		name_len++;
	}

	ext_existed = (ext_dot > src_name) && (ext_dot < (src_name + name_len - 1));
	for (p = src_name; p < src_name + name_len; p++) {
		if (ext_existed) {
			if (p < ext_dot)
				case_cfg |= islower(*p) ? DIRITEM_NTRES_BODY_LOWER : 0;
			else if (p > ext_dot)
				case_cfg |= islower(*p) ? DIRITEM_NTRES_EXT_LOWER : 0;
		}
		else
			case_cfg |= islower(*p) ? DIRITEM_NTRES_BODY_LOWER : 0;
	}
	
	return case_cfg;
}

static u8_t is_filename_match(const char* name_in_dir, const char* to_find_name) {
	char temp_name[SFN_LEN];

	to_sfn(temp_name, to_find_name);
	
	return memcmp(temp_name, name_in_dir, SFN_LEN) == 0;
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
	while ((*c != '\0') && (!is_path_sep(*c))) {
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

static void copy_date_time(xfile_time_t* dest, const diritem_date_t* date, const diritem_time_t* time, const u8_t mil_sec) {
	if (date) {
		dest->year = date->year_from_1980 + 1980;
		dest->month = date->month;
		dest->day = date->day;
	}
	else {
		dest->year = 0;
		dest->month = 0;
		dest->day = 0;
	}

	if (time) {
		dest->hour = time->hour;
		dest->minute = time->minute;
		dest->second = time->second_2 * 2 + mil_sec / 100;
	}
	else {
		dest->hour = 0;
		dest->minute = 0;
		dest->second = 0;
	}
}

static void sfn_to_myname(char* dest_name, const diritem_t* diritem) {
	char* dest = dest_name;
	char* raw_name = (char*)diritem->DIR_Name;
	u8_t ext_name = raw_name[8] != 0x20;
	u8_t scan_len = ext_name ? (SFN_LEN + 1) : SFN_LEN;

	memset(dest_name, 0, X_FILEINFO_NAME_SIZE);
	for (u8_t i = 0; i < scan_len; i++) {
		if (*raw_name == ' ')
			raw_name++;
		else if ((i == 8) && ext_name)
			*dest++ = '.';
		else {
			u8_t lower = 0;
			if (((i < 8) && (diritem->DIR_NTRes & DIRITEM_NTRES_BODY_LOWER)) || ((i > 8) && (diritem->DIR_NTRes & DIRITEM_NTRES_EXT_LOWER))) {
				lower = 1;
			}
			*dest++ = lower ? tolower(*raw_name++) : toupper(*raw_name++);
		}
	}
	*dest = '\0';
}

static u32_t get_diritem_cluster(diritem_t* item) {
	return ((item->DIR_FstClusHI << 16) | item->DIR_FstClusL0);
}

xfat_err_t move_cluster_pos(xfat_t* xfat, u32_t curr_cluster, u32_t curr_offset, u32_t move_bytes,
	u32_t* next_cluster, u32_t* next_offset) {
	if ((curr_offset + move_bytes) >= xfat->cluster_byte_size) {
		xfat_err_t err = get_next_cluster(xfat, curr_cluster, next_cluster);
		if (err < 0) return err;

		*next_cluster = 0;
	}
	else {
		*next_cluster = curr_cluster;
		*next_offset = curr_offset + move_bytes;
	}
	
	return FS_ERR_OK;
}

xfat_err_t get_next_diritem(xfat_t* xfat, u8_t type, u32_t start_cluster, u32_t start_offset, u32_t* found_cluster,
	u32_t* found_offset, u32_t* next_cluster, u32_t* next_offset, u8_t * temp_buffer, diritem_t** diritem) {
	xfat_err_t err;
	diritem_t* r_diritem;
	
	while (is_cluster_valid(start_cluster)) {
		u32_t sector_offset;

		err = move_cluster_pos(xfat, start_cluster, start_offset, sizeof(diritem_t), next_cluster, next_offset);
		if (err < 0) return err;

		sector_offset = to_sector_offset(xfat_get_disk(xfat), start_offset);
		if (sector_offset == 0) {
			u32_t curr_sector = to_phy_sector(xfat, start_cluster, start_offset);
			err = xdisk_read_sector(xfat_get_disk(xfat), temp_buffer, curr_sector, 1);
			if (err < 0) return err;
		}

		r_diritem = (diritem_t*)(temp_buffer + sector_offset);
		switch (r_diritem->DIR_Name[0]) {
		case DIRITEM_NAME_END:
			if (type & DIRITEM_GET_END) {
				*diritem = r_diritem;
				*found_cluster = start_cluster;
				*found_offset = start_offset;
				return FS_ERR_OK;
			}
			break;
		case DIRITEM_NAME_FREE:
			if (type & DIRITEM_GET_FREE) {
				*diritem = r_diritem;
				*found_cluster = start_cluster;
				*found_offset = start_offset;
				return FS_ERR_OK;
			}
			break;
		default:
			if (type & DIRITEM_GET_USED) {
				*diritem = r_diritem;
				*found_cluster = start_cluster;
				*found_offset = start_offset;
				return FS_ERR_OK;
			}
			break;
		}

		start_cluster = *next_cluster;
		start_offset = *next_offset;
	}

	*diritem = (diritem_t*)0;
	return FS_ERR_EOF;
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

static void copy_file_info(xfileinfo_t* info, const diritem_t* dir_item) {
	sfn_to_myname(info->file_name, dir_item);
	info->size = dir_item->DIR_FileSize;
	info->attr = dir_item->DIR_Attr;
	info->type = get_file_type(dir_item);

	copy_date_time(&info->create_time, &dir_item->DIR_CrtDate, &dir_item->DIR_CrtTime, dir_item->DIR_CrtTimeTeenth);
	copy_date_time(&info->last_acctime, &dir_item->DIR_LastAccDate, 0, 0);
	copy_date_time(&info->modify_time, &dir_item->DIR_WrtDate, &dir_item->DIR_WrtTime, 0);
}

static u8_t is_locate_type_match (diritem_t * dir_item, u8_t locate_type) {
	u8_t match = 1;

	if ((dir_item->DIR_Attr & DIRITEM_ATTR_SYSTEM) && !(locate_type & XFILE_LOCATE_SYSTEM))
		match = 0;
	else if ((dir_item->DIR_Attr & DIRITEM_ATTR_HIDDEN) && !(locate_type & XFILE_LOCATE_HIDDEN))
		match = 0;
	else if ((dir_item->DIR_Attr & DIRITEM_ATTR_VOLUME_ID) && !(locate_type & XFILE_LOCATE_VOL))
		match = 0;
	else if ((memcmp(DOT_FILE, dir_item->DIR_Name, SFN_LEN) == 0) || (memcmp(DOT_DOT_FILE, dir_item->DIR_Name, SFN_LEN) == 0)) {
		if (!(locate_type & XFILE_LOCATE_DOT))
			match = 0;
	}
	else if (!(locate_type & XFILE_LOCATE_NORMAL))
		match = 0;

	return match;
}

static xfat_err_t locate_file_dir_item(xfat_t* xfat, u32_t locate_type, u32_t* dir_cluster, u32_t* cluster_offset, const char* path, u32_t* moved_bytes, diritem_t** r_diritem) {
	xfat_err_t err;
	u32_t curr_cluster = *dir_cluster;
	xdisk_t* xdisk = xfat_get_disk(xfat);
	u32_t initial_sector = to_sector(xdisk, *cluster_offset);
	u32_t initial_offset = to_sector_offset(xdisk, *cluster_offset);
	u32_t r_moved_bytes = 0;

	do {
		u32_t start_sector = cluster_first_sector(xfat, curr_cluster);

		for (u32_t i = initial_sector; i < xfat->sec_per_cluster; i++) {
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
				else if (!is_locate_type_match(dir_item, locate_type)) {
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

				r_moved_bytes += sizeof(diritem_t);
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
			xfat_err_t err;
			u32_t moved_bytes = 0;
			dir_item = (diritem_t*)0;

			err = locate_file_dir_item(xfat, (XFILE_LOCATE_DOT | XFILE_LOCATE_NORMAL), &parent_cluster, &parent_cluster_offset, curr_path, &moved_bytes, &dir_item);
			if (err < 0) return err;
			if (dir_item == (diritem_t*)0) return FS_ERR_NONE;
			
			curr_path = get_child_path(curr_path);
			if (curr_path != (const char*)0) {
				parent_cluster = get_diritem_cluster(dir_item);
				parent_cluster_offset = 0;
			}
			else {
				file_start_cluster = get_diritem_cluster(dir_item);
				if ((memcmp(dir_item->DIR_Name, DOT_DOT_FILE, SFN_LEN) == 0) && (file_start_cluster == 0))
					file_start_cluster = xfat->root_cluster;
			}
				
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
	path = skip_first_path_sep(path);
	if (memcmp(path, "..", 2) == 0)
		return FS_ERR_PARAM;
	else if (memcmp(path, ".", 1) == 0)
		path++;
	
	return open_sub_file(xfat, xfat->root_cluster, file, path);
}

xfat_err_t xfile_open_sub(xfile_t* dir, const char* sub_path, xfile_t* sub_file) {
	sub_path = skip_first_path_sep(sub_path);
	if (memcmp(sub_path, ".", 1) == 0)
		return FS_ERR_PARAM;

	return open_sub_file(dir->xfat, dir->start_cluster, sub_file, sub_path);
}

xfat_err_t xdir_first_file(xfile_t* file, xfileinfo_t* info) {
	xfat_err_t err;
	u32_t cluster_offset = 0;
	u32_t moved_bytes = 0;
	diritem_t* dir_item = (diritem_t*)0;
	
	if (file->type != FAT_DIR)
		return FS_ERR_PARAM;

	file->curr_cluster = file->start_cluster;
	file->pos = 0;
	
	err = locate_file_dir_item(file->xfat, XFILE_LOCATE_NORMAL, &file->curr_cluster, &cluster_offset, "", &moved_bytes, &dir_item);
	if (err < 0) return err;

	if (dir_item == (diritem_t*)0)
		return FS_ERR_EOF;

	file->pos += moved_bytes;

	copy_file_info(info, dir_item);
	return err;
}

xfat_err_t xdir_next_file(xfile_t* file, xfileinfo_t* info) {
	xfat_err_t err;
	u32_t cluster_offset = 0;
	u32_t moved_bytes = 0;
	diritem_t* dir_item = (diritem_t*)0;

	if (file->type != FAT_DIR)
		return FS_ERR_PARAM;
	
	cluster_offset = to_cluster_offset(file->xfat, file->pos);
	err = locate_file_dir_item(file->xfat, XFILE_LOCATE_NORMAL, &file->curr_cluster, &cluster_offset, "", &moved_bytes, &dir_item);
	if (err < 0) return err;

	if (dir_item == (diritem_t*)0)
		return FS_ERR_EOF;

	file->pos += moved_bytes;

	if ((cluster_offset + sizeof(diritem_t)) >= file->xfat->cluster_byte_size) {
		err = get_next_cluster(file->xfat, file->curr_cluster, &file->curr_cluster);
		if (err < 0) return err;
	}

	copy_file_info(info, dir_item);
	return err;
}

xfat_err_t xfile_error(xfile_t* file) {
	return file->err;
}

void xfile_clear_err(xfile_t* file) {
	file->err = FS_ERR_OK;
}

xfile_size_t xfile_read(void* buffer, xfile_size_t elem_size, xfile_size_t count, xfile_t* file) {
	u32_t cluster_sector;
	u32_t sector_offset;
	xfile_size_t r_count_read = 0;
	xdisk_t* disk = file_get_disk(file);
	xfile_size_t bytes_to_read = count * elem_size;
	u8_t* read_buffer = (u8_t*)buffer;

	if (file->type != FAT_FILE) {
		file->err = FS_ERR_FSTYPE;
		return 0;
	}

	if (file->pos >= file->size) {
		file->err = FS_ERR_EOF;
		return 0;
	}

	if (file->pos + bytes_to_read > file->size)
		bytes_to_read = file->size - file->pos;

	cluster_sector = to_sector(disk, to_cluster_offset(file->xfat, file->pos));
	sector_offset = to_sector_offset(disk, file->pos);

	while ((bytes_to_read > 0) && is_cluster_valid(file->curr_cluster)) {
		xfat_err_t err;
		xfile_size_t curr_read_bytes = 0;
		u32_t sector_count = 0;
		u32_t start_sector = cluster_first_sector(file->xfat, file->curr_cluster) + cluster_sector;
		if ((sector_offset != 0) || (!sector_offset && (bytes_to_read < disk->sector_size))) {
			sector_count = 1;
			curr_read_bytes = bytes_to_read;

			if (sector_offset != 0) {
				if (sector_offset + bytes_to_read > disk->sector_size) {
					curr_read_bytes = disk->sector_size - sector_offset;
				}
			}

			err = xdisk_read_sector(disk, temp_buffer, start_sector, 1);
			if (err < 0) {
				file->err = err;
				return 0;
			}

			memcpy(read_buffer, temp_buffer + sector_offset, curr_read_bytes);
			read_buffer += curr_read_bytes;
			bytes_to_read -= curr_read_bytes;
		}
		else {
			sector_count = to_sector(disk, bytes_to_read);
			if ((cluster_sector + sector_count) > file->xfat->sec_per_cluster)
				sector_count = file->xfat->sec_per_cluster - cluster_sector;

			err = xdisk_read_sector(disk, read_buffer, start_sector, sector_count);
			if (err != FS_ERR_OK) {
				file->err = err;
				return 0;
			}

			curr_read_bytes = sector_count * disk->sector_size;
			read_buffer += curr_read_bytes;
			bytes_to_read -= curr_read_bytes;
		}

		r_count_read += curr_read_bytes;

		sector_offset += curr_read_bytes;
		if (sector_offset >= disk->sector_size) {
			sector_offset = 0;
			cluster_sector += sector_count;
			if (cluster_sector >= file->xfat->sec_per_cluster) {
				cluster_sector = 0;
				err = get_next_cluster(file->xfat, file->curr_cluster, &file->curr_cluster);
				if (err != FS_ERR_OK) {
					file->err = err;
					return 0;
				}
			}
		}
		
		file->pos += curr_read_bytes;
	}

	file->err = is_cluster_valid(file->curr_cluster) ? FS_ERR_OK : FS_ERR_EOF;
	return r_count_read / elem_size;
}

xfat_err_t xfile_eof(xfile_t* file) {
	return (file->pos >= file->size) ? FS_ERR_EOF : FS_ERR_OK;
}

xfile_size_t xfile_tell(xfile_t* file) {
	return file->pos;
}

xfat_err_t xfile_seek(xfile_t* file, xfile_ssize_t offset, xfile_origin_t origin) {
	xfat_err_t err;
	xfile_ssize_t final_pos;
	xfile_size_t offset_to_move;
	u32_t curr_cluster;
	u32_t curr_pos;
	
	switch (origin) {
	case XFAT_SEEK_SET:
		final_pos = offset;
		break;
	case XFAT_SEEK_CUR:
		final_pos = file->pos + offset;
		break;
	case XFAT_SEEK_END:
		final_pos = file->size + offset;
		break;
	default:
		final_pos = -1;
		break;
	}
	
	if ((final_pos < 0) || (final_pos >= file->size))
		return FS_ERR_PARAM;

	offset = final_pos - file->pos;
	if (offset > 0) {
		curr_cluster = file->curr_cluster;
		curr_pos = file->pos;
		offset_to_move = (xfile_size_t)offset;
	}
	else {
		curr_cluster = file->start_cluster;
		curr_pos = 0;
		offset_to_move = (xfile_size_t)final_pos;
	}

	while (offset_to_move > 0) {
		u32_t cluster_offset = to_cluster_offset(file->xfat, curr_pos);

		xfile_size_t curr_move = offset_to_move;
		if (cluster_offset + curr_move < file->xfat->cluster_byte_size) {
			curr_pos += curr_move;
			break;
		}
		else {
			curr_move = file->xfat->cluster_byte_size - cluster_offset;
			curr_pos += curr_move;
			offset_to_move -= curr_move;

			err = get_next_cluster(file->xfat, curr_cluster, &curr_cluster);
			if (err < 0) {
				file->err = err;
				return err;
			}
		}
	}

	file->pos = curr_pos;
	file->curr_cluster = curr_cluster;
	return FS_ERR_OK;
}

xfat_err_t xfile_rename(xfat_t* xfat, const char* path, const char* new_name) {
	diritem_t* dir_item = (diritem_t*)0;
	u32_t curr_cluster, curr_offset;
	u32_t next_cluster, next_offset;
	u32_t found_cluster, found_offset;
	const char* curr_path;

	curr_cluster = xfat->root_cluster;
	curr_offset = 0;
	for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
		do {
			xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset, &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &dir_item);
			if (err < 0) return err;

			if (dir_item == (diritem_t*)0) return FS_ERR_NONE;

			if (is_filename_match((char*)dir_item->DIR_Name, curr_path)) {
				if (get_file_type(dir_item) == FAT_DIR) {
					curr_cluster = get_diritem_cluster(dir_item);
					curr_offset = 0;
				}
				break;
			}

			curr_cluster = next_cluster;
			curr_offset = next_offset;
		} while (1);
	}

	if (dir_item && !curr_path) {
		u32_t dir_sector = to_phy_sector(xfat, found_cluster, found_offset);
		to_sfn((char*)dir_item->DIR_Name, new_name);
		dir_item->DIR_NTRes &= ~DIRITEM_NTRES_CASE_MASK;
		dir_item->DIR_NTRes |= get_sfn_case_cfg(new_name);
		return xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
	}

	return FS_ERR_OK;
}

static xfat_err_t set_file_time(xfat_t* xfat, const char* path, stime_type_t type, xfile_time_t * time) {
	diritem_t* dir_item = (diritem_t*)0;
	u32_t curr_cluster, curr_offset;
	u32_t next_cluster, next_offset;
	u32_t found_cluster, found_offset;
	const char* curr_path;

	curr_cluster = xfat->root_cluster;
	curr_offset = 0;
	for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
		do {
			xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset, &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &dir_item);
			if (err < 0) return err;

			if (dir_item == (diritem_t*)0) return FS_ERR_NONE;

			if (is_filename_match((char*)dir_item->DIR_Name, curr_path)) {
				if (get_file_type(dir_item) == FAT_DIR) {
					curr_cluster = get_diritem_cluster(dir_item);
					curr_offset = 0;
				}
				break;
			}

			curr_cluster = next_cluster;
			curr_offset = next_offset;
		} while (1);
	}

	if (dir_item && !curr_path) {
		u32_t dir_sector = to_phy_sector(xfat, found_cluster, found_offset);
		
		switch (type)
		{
		case XFAT_TIME_CTIME:
			dir_item->DIR_CrtDate.year_from_1980 = time->year - 1980;
			dir_item->DIR_CrtDate.month = time->month;
			dir_item->DIR_CrtDate.day = time->day;
			dir_item->DIR_CrtTime.hour = time->hour;
			dir_item->DIR_CrtTime.minute = time->minute;
			dir_item->DIR_CrtTime.second_2 = time->second / 2;
			dir_item->DIR_CrtTimeTeenth = time->second % 2 * 1000 / 10;
			break;
		case XFAT_TIME_ATIME:
			dir_item->DIR_LastAccDate.year_from_1980 = time->year - 1980;
			dir_item->DIR_LastAccDate.month = time->month;
			dir_item->DIR_LastAccDate.day = time->day;
			break;
		case XFAT_TIME_MTIME:
			dir_item->DIR_WrtDate.year_from_1980 = time->year - 1980;
			dir_item->DIR_WrtDate.month = time->month;
			dir_item->DIR_WrtDate.day = time->day;
			dir_item->DIR_WrtTime.hour = time->hour;
			dir_item->DIR_WrtTime.minute = time->minute;
			dir_item->DIR_WrtTime.second_2 = time->second / 2;
			break;
		}

		return xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
	}

	return FS_ERR_OK;
}

xfat_err_t xfile_set_atime(xfat_t* xfat, const char* path, xfile_time_t* time) {
	xfat_err_t err = set_file_time(xfat, path, XFAT_TIME_ATIME, time);
	return err;
}

xfat_err_t xfile_set_mtime(xfat_t* xfat, const char* path, xfile_time_t* time) {
	xfat_err_t err = set_file_time(xfat, path, XFAT_TIME_MTIME, time);
	return err;
}

xfat_err_t xfile_set_ctime(xfat_t* xfat, const char* path, xfile_time_t* time) {
	xfat_err_t err = set_file_time(xfat, path, XFAT_TIME_CTIME, time);
	return err;
}

xfat_err_t xfile_close(xfile_t* file) {
	return FS_ERR_OK;
}

