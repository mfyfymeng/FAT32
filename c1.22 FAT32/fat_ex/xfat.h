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

#pragma pack()

typedef struct _xfat_t {
	u32_t fat_start_sector;
	u32_t fat_tbl_nr;
	u32_t fat_tbl_sectors;
	u32_t total_sectors;
	xdisk_part_t* disk_part;
}xfat_t;

xfat_err_t xfat_open(xfat_t* xfat, xdisk_part_t* xdisk_part);

#endif // !XFAT_H
