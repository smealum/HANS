#ifndef FSREDIR_H
#define FSREDIR_H

#include "patch.h"

#include "path_insert_stub_stub.h"
#include "findarchive_path_insert_stub_stub.h"
#include "openfiledirectly_stub_stub.h"
#include "openfile_stub_stub.h"

#define STUB_OFD_OFFSET (0)
#define STUB_OF_OFFSET (STUB_OFD_OFFSET + openfiledirectly_stub_stub_size / 4)
#define STUB_NIM_OFFSET (STUB_OF_OFFSET + openfile_stub_stub_size / 4)

function_s findFatalerr(u8* code_data, u32 code_size);

void patchFsOpenRom(u8* code_data, u32 code_size, u32 fileHandle);
void patchRedirectFs(u8* code_data, u32 code_size, u32 fsHandle, char* directory);
void patchFsSavegame(u8* code_data, u32 code_size, u32 fsHandle, u64 archiveHandle);

#endif
