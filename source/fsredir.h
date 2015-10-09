#ifndef FSREDIR_H
#define FSREDIR_H

#include "patch.h"

void patchFsOpenRom(u8* code_data, u32 code_size, u32 fsHandle, char* path);
void patchRedirectFs(u8* code_data, u32 code_size, u32 fsHandle, char* directory);

#endif
