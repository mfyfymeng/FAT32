#ifndef XTYPES_H
#define XTYPES_H

#include <stdint.h>

typedef uint8_t     u8_t;
typedef uint16_t    u16_t;
typedef uint32_t    u32_t;
typedef uint64_t    u64_t;
typedef uint32_t    xfile_size_t;
typedef int64_t     xfile_ssize_t;

typedef enum _xfat_err_t {
	FS_ERR_EOF = 1,
	FS_ERR_OK = 0,
	FS_ERR_IO = -1,
	FS_ERR_PARAM = -2,
	FS_ERR_NONE = -3,
	FS_ERR_FSTYPE = -4,
	FS_ERR_READONLY = -5,
}xfat_err_t;

#endif // !XTYPES_H
