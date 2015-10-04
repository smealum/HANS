#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "fsredir.h"

function_s findFsInitialize(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x08010002);
}

function_s findFsInitializeWithSdkVersion(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x08610042);
}

function_s findFsOpenArchive(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x080C00C2);
}

function_s findFsOpenFileDirectly(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x08030204);
}

function_s findFsControlArchive(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x80D0144);
}

function_s findFsHighLevelInitialize(u8* code_data, u32 code_size)
{
    function_s fsInitialize = findFsInitialize(code_data, code_size);
    if(fsInitialize.start == fsInitialize.end) fsInitialize = findFsInitializeWithSdkVersion(code_data, code_size);
    if(fsInitialize.start == fsInitialize.end) return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsInitialize, NULL);
}

bool findFsOpenSpecialArchiveRawCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    int j;

    for(j=candidate.start; j<ref; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_MOV && d.Rd == 2 && d.Rm == 1))
        {
            return true;
        }
    }
    return false;
}

function_s findFsOpenSpecialArchiveRaw(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};
    function_s fsOpenArchive = findFsOpenArchive(code_data, code_size);
    if(fsOpenArchive.start == fsOpenArchive.end)return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsOpenArchive, findFsOpenSpecialArchiveRawCallback);
}

bool findFsMountSaveCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    int j;
    
    for(j=ref-4; j<ref; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_MOV && d.Rd == 1 && d.imm == 0x4))
        {
            return true;
        }
    }
    return false;
}

function_s findFsMountSave(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};
    function_s fsOpenSpecialArchiveRaw = findFsOpenSpecialArchiveRaw(code_data, code_size);
    if(fsOpenSpecialArchiveRaw.start == fsOpenSpecialArchiveRaw.end)return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsOpenSpecialArchiveRaw, findFsMountSaveCallback);
}

bool findFsOpenRomCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    int j;

    bool ret = false;
    
    for(j=ref; j>candidate.start; j--)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_MOV && d.Rd == 3))
        {
            ret = d.imm == 0x3;
            if(ret)break;
        }
    }

    if(ret)
    {
        ret = false;
        for(j=candidate.end; j<candidate.end+50; j++)
        {
            darm_t d;
            if(code_data32[j] == 0xD8604659)
            {
                ret = true;
                break;
            }

            if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_PUSH && d.Rn == SP))
            {
                ret = false;
                break;
            }
        }
    }

    return ret;
}

function_s findFsOpenRom(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};
    function_s fsOpenFileDirectly = findFsOpenFileDirectly(code_data, code_size);
    if(fsOpenFileDirectly.start == fsOpenFileDirectly.end)return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsOpenFileDirectly, findFsOpenRomCallback);
}

function_s findFsMountRom(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};
    function_s fsOpenRom = findFsOpenRom(code_data, code_size);
    if(fsOpenRom.start == fsOpenRom.end)return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsOpenRom, NULL);
}

void patchFsHighLevelInitialize(u8* code_data, u32 code_size, function_s c, u32 handle)
{
    if(!code_data || !code_size || c.start == c.end || c.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i;

    u32 push_location = 0;
    u32 str_location = 0;

    // find str r0, [rA, #...]
    // after str r0, [rA, #...], place mov r0, #0, bx lr, handle litteral
    // find PUSH, replace it with ldr r0, [PC, #offset] (offset to handle litteral)
    for(i = c.start; i<c.end && (!push_location || !str_location); i++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && d.instr == I_PUSH) push_location = i;
        else if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_STR && d.Rn != SP)) str_location = i;
    }

    code_data32[str_location + 1] = 0xE3A00000; // mov r0, #0
    code_data32[str_location + 2] = 0xE12FFF1E; // bx lr
    code_data32[str_location + 3] = handle; // handle litteral

    u32 offset = (str_location + 3 - push_location - 2) * 4;

    code_data32[push_location] = 0xE59F0000 | offset; // ldr r0, [pc, #offset]
}

void patchFsMountSave(u8* code_data, u32 code_size, function_s c)
{
    if(!code_data || !code_size || c.start == c.end || c.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i;

    // find mov r1, #4
    // replace with mov r1, #9
    for(i = c.start; i<c.end; i++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_MOV && d.Rd == r1 && d.imm == 0x4))
        {
            code_data32[i] = (code_data32[i] & ~0xFF) | 0x09;
        }
    }

    // printFunction(code_data, code_size, c);
}

void patchFsMountRom(u8* code_data, u32 code_size, function_s fsOpenSpecialArchiveRaw, function_s fsOpenRom, function_s c)
{
    if(!code_data || !code_size || c.start == c.end || c.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i;

    int blFsOpenRom = 0;

    // find bl fsOpenRom
    // replace bl fsOpenRom with bl fsOpenSpecialArchiveRaw
    // walk back up from bl fsOpenSpecialArchiveRaw and :
    //     - if mov r1, ..., replace with mov r1, #9 and break
    //     - if str ..., [sp] or strd ..., [sp] or stmea sp, {...}, replace with mov r1, #9 and break
    for(i = c.start; i<c.end; i++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_BL && (fsOpenRom.start-i-2)*4 == d.imm))
        {
            blFsOpenRom = i;
            break;
        }
    }

    code_data32[blFsOpenRom] = (code_data32[blFsOpenRom] & ~0xFFFF) | (fsOpenSpecialArchiveRaw.start - blFsOpenRom - 2);

    for(i = blFsOpenRom; i>c.start; i--)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && (d.Rn == SP && (d.instr == I_STM || d.instr == I_STRD || d.instr == I_STR) && d.imm == 0))
        {
            code_data32[i] = 0xE3A01009; // mov r1, #9
            // printf("%X %X\n", i * 4 + 0x00100000, blFsOpenRom * 4 + 0x00100000);
            break;
        }
    }
}

void patchFsControlArchive(u8* code_data, u32 code_size, function_s c)
{
    if(!code_data || !code_size || c.start == c.end || c.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;

    // just nop out the function by replacing it with mov r0, #0; bx lr
    code_data32[c.start + 0] = 0xE3A00000; // mov r0, #0
    code_data32[c.start + 1] = 0xE12FFF1E; // bx lr
}

void patchRedirectFs(u8* code_data, u32 code_size, u32 fsHandle)
{
    function_s fsHighLevelInitialize = findFsHighLevelInitialize(code_data, code_size);
    printf("fsHighLevelInitialize : %08X - %08X\n", (unsigned int)(fsHighLevelInitialize.start * 4 + 0x00100000), (unsigned int)(fsHighLevelInitialize.end * 4 + 0x00100000));

    function_s fsOpenSpecialArchiveRaw = findFsOpenSpecialArchiveRaw(code_data, code_size);
    printf("fsOpenSpecialArchiveRaw : %08X - %08X\n", (unsigned int)(fsOpenSpecialArchiveRaw.start * 4 + 0x00100000), (unsigned int)(fsOpenSpecialArchiveRaw.end * 4 + 0x00100000));
    
    function_s fsMountSave = findFsMountSave(code_data, code_size);
    printf("fsMountSave : %08X - %08X\n", (unsigned int)(fsMountSave.start * 4 + 0x00100000), (unsigned int)(fsMountSave.end * 4 + 0x00100000));
    
    function_s fsOpenRom = findFsOpenRom(code_data, code_size);
    printf("fsOpenRom : %08X - %08X\n", (unsigned int)(fsOpenRom.start * 4 + 0x00100000), (unsigned int)(fsOpenRom.end * 4 + 0x00100000));
    
    function_s fsMountRom = findFsMountRom(code_data, code_size);
    printf("fsMountRom : %08X - %08X\n", (unsigned int)(fsMountRom.start * 4 + 0x00100000), (unsigned int)(fsMountRom.end * 4 + 0x00100000));
    
    function_s fsControlArchive = findFsControlArchive(code_data, code_size);
    printf("fsControlArchive : %08X - %08X\n", (unsigned int)(fsControlArchive.start * 4 + 0x00100000), (unsigned int)(fsControlArchive.end * 4 + 0x00100000));

    patchFsHighLevelInitialize(code_data, code_size, fsHighLevelInitialize, fsHandle);
    patchFsMountSave(code_data, code_size, fsMountSave);
    patchFsMountRom(code_data, code_size, fsOpenSpecialArchiveRaw, fsOpenRom, fsMountRom);
    patchFsControlArchive(code_data, code_size, fsControlArchive);
}
