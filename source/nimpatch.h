#ifndef NIMPATCH_H
#define NIMPATCH_H

#include "patch.h"

void patchNimCheckSysupdateAvailableSOAP(u8* code_data, u32 code_size);
void patchNimTitleVersion(u8* code_data, u32 code_size, u32 version);

#endif
