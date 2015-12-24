#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "nimpatch.h"
#include "fsredir.h"

#include "nim_checkupdate_stub_stub.h"
#include "nim_titleversion_stub_stub.h"

static function_s nimCheckSysupdateAvailableSOAP;

bool findNimCheckSysupdateAvailableSOAPCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    int j;

    for(j=candidate.start; j<=candidate.end+1; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_B && d.cond >= C_AL))
        {
            function_s c = findFunction(code_data32, code_size32, (d.imm / 4) + j + 2);
            int i, total = 0;
            for(i=c.start; i<c.end; i++)
            {
                if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_MOV && d.imm == 0xA0000))total |= 1;
                if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_SVC && d.imm == 0x32))total |= 2;
            }
            if(total == 3) nimCheckSysupdateAvailableSOAP = c;
            return total == 3;
        }
    }

    return false;
}

function_s findNimCheckSysupdateAvailableSOAP(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    function_s ret = findPooledCommandFunction(code_data, code_size, 0xC8A0D018, findNimCheckSysupdateAvailableSOAPCallback);
    if(ret.start == ret.end)return ret;

    return nimCheckSysupdateAvailableSOAP;
}

function_s findNimListTitles(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    return findPooledCommandFunction(code_data, code_size, 0x0016020A, NULL);
}

function_s findNimListTitlesWrapperWrapper(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    function_s listTitles = findNimListTitles(code_data, code_size);
    function_s listTitlesWrapper = findFunctionReferenceFunction(code_data, code_size, listTitles, NULL, NULL);
    return findFunctionReferenceFunction(code_data, code_size, listTitlesWrapper, NULL, NULL);
}

darm_reg_t getLowestRegister(darm_t* d)
{
    if(!d) return 0;

    int i;
    for(i = 0; i < 16; i++)
    {
        if((d->reglist >> i) & 0x1) return i;
    }

    return 0;
}

void patchNimTitleVersion(u8* code_data, u32 code_size, u32 version)
{
    if(!code_data || !code_size)return;

    function_s ltww = findNimListTitlesWrapperWrapper(code_data, code_size);
    if(ltww.start == ltww.end)return;

    u32 ref = 0;
    function_s ltwwr = findFunctionReferenceFunction(code_data, code_size, ltww, NULL, &ref);
    if(ltwwr.start == ltwwr.end)return;

    u32* code_data32 = (u32*)code_data;
    // u32 code_size32 = code_size / 4;

    u32 stub_offset = findFatalerr(code_data, code_size).start + 1 + STUB_NIM_OFFSET;
    u32 stub_size32 = nim_titleversion_stub_stub_size / 4;

    memcpy(&code_data32[stub_offset], nim_titleversion_stub_stub, stub_size32 * 4);

    code_data32[stub_offset + stub_size32 - 1] = version;

    // printf("yo %08X\n", ref * 4 + 0x00100000);
    int j;
    for(j=ref; j<=ltwwr.end+1; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_STM && d.cond >= C_AL))
        {
            // printf("%08X - %08X - %d\n", j * 4 + 0x00100000, code_data32[j], getLowestRegister(&d));

            code_data32[stub_offset + 0] = 0xE59F0000 | ((getLowestRegister(&d) & 0xf) << 12) | (code_data32[stub_offset + 0] & 0xfff); // overwritten with an ldr rX, [pc, #self]
            code_data32[stub_offset + 1] = code_data32[j];
            code_data32[j] = 0xEB000000 | ((stub_offset - j - 2) & 0x00FFFFFF); // branch with link

            return;
        }
    }
}

void patchNimCheckSysupdateAvailableSOAP(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return;

    function_s ret = findNimCheckSysupdateAvailableSOAP(code_data, code_size);
    if(ret.start == ret.end)return;

    memcpy(&code_data[ret.start * 4], nim_checkupdate_stub_stub, nim_checkupdate_stub_stub_size);
}
