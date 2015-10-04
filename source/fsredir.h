#ifndef FSREDIR_H
#define FSREDIR_H

#include "patch.h"

void patchRedirectFs(u8* code_data, u32 code_size, u32 fsHandle);

#endif
