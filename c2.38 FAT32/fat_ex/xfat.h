#ifndef XFAT_H
#define XFAT_H

#include "xtypes.h"
#include "xdisk.h"

#pragma pack(1)

typedef struct _bpb_t {
	u8_t  BS_jmpBoot[3];
	u8_t  BS_OEMName[8];
	u16_t BPB_BytsPerSec;
	u8_t  BPB_SecPerClus;
	u16_t BPB_RsvdSecCnt;
	u8_t  BPB_NumFATs;
	u16_t BPB_RootEntCnt;
	u16_t BPB_TotSec16;
	u8_t  BPB_Media;
	u16_t BPB_FATSz16;
	u16_t BPB_SecPerTrk;
	u16_t BPB_NumHeads;
	u32_t BPB_HiddSec;
	u32_t BPB_TotSec32;
}bpb_t;

typedef struct _fat32_hdr_t {
	u32_t BPB_FATSz32;
	u16_t BPB_ExtFlags;
	u16_t BPB_FSVer;
	u32_t BPB_RootClus;
	u16_t BPB_FsInfo;
	u16_t BPB_BkBootSec;
	u8_t  BPB_Reserved[12];
	u8_t  BS_DrvNum;
	u8_t  BS_Resvered1;
	u8_t  BS_BootSig;
	u32_t BS_VolID;
	u8_t  BS_VolLab[11];
	u8_t  BS_FileSysType[8];
}fat32_hdr_t;

typedef struct _dbr_t {
	bpb_t bpb;
	fat32_hdr_t fat32;
}dbr_t;

#define CLUSTER_INVALID                 0xFFFFFFF

#define DIRITEM_NAME_FREE               0xE5
#define DIRITEM_NAME_END                0x00

#define DIRITEM_NTRES_BODY_LOWER        (1 << 3)
#define DIRITEM_NTRES_EXT_LOWER         (1 << 4)

#define DIRITEM_ATTR_READ_ONLY          0x01
#define DIRITEM_ATTR_HIDDEN             0x02
#define DIRITEM_ATTR_SYSTEM             0x04
#define DIRITEM_ATTR_VOLUME_ID          0x08
#define DIRITEM_ATTR_DIRECTORY          0x10
#define DIRITEM_ATTR_ARCHIVE            0x20
#define DIRITEM_ATTR_LONG_NAME          0x0F

typedef struct _diritem_date_t {
	u16_t day : 5;
	u16_t month : 4;
	u16_t year_from_1980 : 7;
}diritem_date_t;

typedef struct _diritem_time_t {
	u16_t second_2 : 5;
	u16_t minute : 6;
	u16_t hour : 5;
}diritem_time_t;

typedef struct _diritem_t {
	u8_t DIR_Name[8];
	u8_t DIR_ExtName[3];
	u8_t DIR_Attr;
	u8_t DIR_NTRes;
	u8_t DIR_CrtTimeTeenth;
	diritem_time_t DIR_CrtTime;
	diritem_date_t DIR_CrtDate;
	diritem_date_t DIR_LastAccDate;
	u16_t DIR_FstClusHI;
	diritem_time_t DIR_WrtTime;
	diritem_date_t DIR_WrtDate;
	u16_t DIR_FstClusL0;
	u32_t DIR_FileSize;
}diritem_t;

typedef union _cluster32_t {
	struct {
		u32_t next : 28;
		u32_t reserved : 4;
	}s;
	u32_t v;
}cluster32_t;

#pragma pack()

typedef struct _xfat_t {
	u32_t fat_start_sector;
	u32_t fat_tbl_nr;
	u32_t fat_tbl_sectors;
	u32_t sec_per_cluster;
	u32_t root_cluster;
	u32_t cluster_byte_size;
	u32_t total_sectors;
	u8_t* fat_buffer;
	xdisk_part_t* disk_part;
}xfat_t;

typedef enum _xfile_type_t {
	FAT_DIR,
	FAT_FILE,
	FAT_VOL,
}xfile_type_t;

#define SFN_LEN                 11
#define X_FILEINFO_NAME_SIZE    32

#define XFILE_LOCATE_NORMAL     (1 << 0)
#define XFILE_LOCATE_DOT        (1 << 1)
#define XFILE_LOCATE_VOL        (1 << 2)
#define XFILE_LOCATE_SYSTEM     (1 << 3)
#define XFILE_LOCATE_HIDDEN     (1 << 4)
#define XFILE_LOCATE_ALL        0xFF

typedef struct _xfile_time_t {
	u16_t year;
	u8_t month;
	u8_t day;
	u8_t hour;
	u8_t minute;
	u8_t second;
}xfile_time_t;

typedef struct _xfileinfo_t {
	char file_name[X_FILEINFO_NAME_SIZE];
	u32_t size;
	u16_t attr;
	xfile_type_t type;
	xfile_time_t create_time;
	xfile_time_t last_acctime;
	xfile_time_t modify_time;
}xfileinfo_t;

typedef struct _xfile_t {
	xfat_t* xfat;
	u32_t size;
	u16_t attr;
	xfile_type_t type;
	u32_t pos;
	xfat_err_t err;
	u32_t start_cluster;
	u32_t curr_cluster;
}xfile_t;

typedef enum _xfile_origin_t {
	XFAT_SEEK_SET,
	XFAT_SEEK_CUR,
	XFAT_SEEK_END,
}xfile_origin_t;

int is_cluster_valid(u32_t cluster);
xfat_err_t get_next_cluster(xfat_t* xfat, u32_t curr_cluster_no, u32_t* next_cluster);
xfat_err_t xfat_open(xfat_t* xfat, xdisk_part_t* xdisk_part);
xfat_err_t read_cluster(xfat_t* xfat, u8_t* buffer, u32_t cluster, u32_t count);

xfat_err_t xfile_open(xfat_t* xfat, xfile_t* file, const char* path);
xfat_err_t xfile_open_sub(xfile_t* dir, const char* sub_path, xfile_t* sub_file);
xfat_err_t xfile_close(xfile_t* file);

xfat_err_t xdir_first_file(xfile_t* file, xfileinfo_t* info);
xfat_err_t xdir_next_file(xfile_t* file, xfileinfo_t* info);

xfat_err_t xfile_error(xfile_t* file);
void xfile_clear_err(xfile_t* file);

xfile_size_t xfile_read(void* buffer, xfile_size_t elem_size, xfile_size_t count, xfile_t* file);

xfat_err_t xfile_eof(xfile_t* file);
xfile_size_t xfile_tell(xfile_t* file);
xfat_err_t xfile_seek(xfile_t* file, xfile_ssize_t offset, xfile_origin_t origin);

#endif // !XFAT_H
