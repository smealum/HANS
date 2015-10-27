#ifndef COMMON_H
#define COMMON_H

extern u32 _firm_appmemalloc;

// #define FIRM_APPMEMALLOC 0x07C00000 // N3ds-only (obviously TEMP)
// #define FIRM_APPMEMALLOC 0x04000000 // o3ds-only (obviously TEMP)
#define FIRM_APPMEMALLOC _firm_appmemalloc
#define FIRM_APPMEMALLOC_LINEAR (0x30000000 + FIRM_APPMEMALLOC)
#define MENU_LOADEDROP_BUFADR (FIRM_APPMEMALLOC_LINEAR + 0x01040000)
#define MENU_LOADEDROP_BKP_BUFADR ((MENU_LOADEDROP_BUFADR + 0x8000))

#define HANS_LOADER_SIZE (0x8000)
#define HANS_STUB_OFFSET (0x100)

typedef struct
{
	u32 src, dst, size;
}memorymap_entry_t;

typedef struct
{
	int num;
	u32 processLinearOffset;
	memorymap_entry_t map[8];
}memorymap_t;

typedef struct
{
	u32 linear_heap;
	u32 linear_size;
	memorymap_t map;
	u32* code_data;
	u32 code_size;
	Handle nssHandle;
}paramblk_t;

#endif
