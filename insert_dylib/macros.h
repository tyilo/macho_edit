#ifndef insert_dylib_macros_h
#define insert_dylib_macros_h

#define MIN(x, y) ((x) < (y)? (x): (y))
#define MAX(x, y) ((x) > (y)? (x): (y))
#define SIGN(x) ((x) == 0? 0: ((x) > 0)? 1: -1)

#define ELEMENTS(x) (sizeof(x) / sizeof(*(x)))

#define IS_FAT(x) ((x) == FAT_MAGIC || (x) == FAT_CIGAM)
#define IS_THIN(x) ((x) == MH_MAGIC || (x) == MH_CIGAM || (x) == MH_MAGIC_64 || (x) == MH_CIGAM_64)
#define IS_MAGIC(x) (IS_FAT(x) || IS_THIN(x))
#define IS_64_BIT(x) ((x) == MH_MAGIC_64 || (x) == MH_CIGAM_64)
#define IS_BIG_ENDIAN(x) ((x) == FAT_CIGAM || (x) == MH_CIGAM_64 || (x) == MH_CIGAM)
#define SWAP32(x, magic) (IS_BIG_ENDIAN(magic)? OSSwapInt32(x): (x))
#define SWAP64(x, magic) (IS_BIG_ENDIAN(magic)? OSSwapInt64(x): (x))
#define MH_SIZE(magic) (IS_64_BIT(magic)? sizeof(mach_header_64): sizeof(mach_header))

#define ROUND_UP(x, y) (((x) + (y) - 1) & -(y))

#define ABSDIFF(x, y) ((x) > (y)? (uintmax_t)(x) - (uintmax_t)(y): (uintmax_t)(y) - (uintmax_t)(x))

#define READ(x, f)  fread(&x, sizeof(x), 1, f)
#define WRITE(x, f) fwrite(&x, sizeof(x), 1, f)
#define PEEK(x, f)  fpeek(&x, sizeof(x), 1, f)

#endif
