#ifndef MMAP_H
#define MMAP_H

#include <3ds.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

memorymap_t* loadMemoryMap(char* path);
memorymap_t* loadMemoryMapTitle(u32 tidlow, u32 tidhigh);

#ifdef __cplusplus
}
#endif

#endif
