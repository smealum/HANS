#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "fsredir.h"

#include "path_insert_stub_stub.h"
#include "findarchive_path_insert_stub_stub.h"
#include "openfiledirectly_stub_stub.h"

function_s findFsInitialize(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x08010002, NULL);
}

function_s findFsInitializeWithSdkVersion(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x08610042, NULL);
}

function_s findFsOpenArchive(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x080C00C2, NULL);
}

function_s findFsOpenFileDirectly(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x08030204, NULL);
}

function_s findFsControlArchive(u8* code_data, u32 code_size)
{
    return findPooledCommandFunction(code_data, code_size, 0x80D0144, NULL);
}

function_s findFsHighLevelInitialize(u8* code_data, u32 code_size)
{
    function_s fsInitialize = findFsInitialize(code_data, code_size);
    if(fsInitialize.start == fsInitialize.end) fsInitialize = findFsInitializeWithSdkVersion(code_data, code_size);
    if(fsInitialize.start == fsInitialize.end) return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsInitialize, NULL, NULL);
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

    return findFunctionReferenceFunction(code_data, code_size, fsOpenArchive, findFsOpenSpecialArchiveRawCallback, NULL);
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

    return findFunctionReferenceFunction(code_data, code_size, fsOpenSpecialArchiveRaw, findFsMountSaveCallback, NULL);
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

    return findFunctionReferenceFunction(code_data, code_size, fsOpenFileDirectly, findFsOpenRomCallback, NULL);
}

function_s findFsMountRom(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};
    function_s fsOpenRom = findFsOpenRom(code_data, code_size);
    if(fsOpenRom.start == fsOpenRom.end)return (function_s){0,0};

    return findFunctionReferenceFunction(code_data, code_size, fsOpenRom, NULL, NULL);
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

    code_data32[blFsOpenRom] = (code_data32[blFsOpenRom] & ~0xFFFFFF) | ((fsOpenSpecialArchiveRaw.start - blFsOpenRom - 2) & 0xFFFFFF);

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

u32 num_thunks_offset;

bool findFsFindArchiveCallersCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    u8 register_path = 0x00;
    int j;

    for(j=candidate.start; j<ref; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_MOV && d.Rd == 0))
        {
            register_path = d.Rm;
            break;
        }
    }

    if(code_data32[num_thunks_offset])
    {
        u32 thunk = num_thunks_offset - 8 * code_data32[num_thunks_offset];

        u32 push_instr = code_data32[candidate.start];
        u32 jump_address = (candidate.start + 1) * 4 + 0x00100000;


        code_data32[thunk + 1] = 0xE1A05000 | register_path; // mov r5, rA
        code_data32[thunk + 4] = push_instr;
        code_data32[thunk + 7] = jump_address;
        
        code_data32[candidate.start] = 0xEA000000 | ((thunk - candidate.start - 2) & 0x00FFFFFF); // b origin+4
        
        code_data32[num_thunks_offset]--;
    }else return true;
    
    return false;
}

function_s findSvcConnectToPort(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i;

    for(i=0; i<code_size32; i++)
    {
        if(code_data32[i] == 0xEF00002D)
        {
            return (function_s){i-1,i+4};
        }
    }

    return (function_s){0,0};
}

bool findFatalerrCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    int j;

    for(j=candidate.start; j<candidate.end; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_LDR && d.Rn == PC && code_data32[j+d.imm/4+2] == 0xD0401834))
        {
            return true;
        }
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_AND && d.imm == 0x7E00000))
        {
            return false;
        }
    }

    return true;
}

function_s findFatalerr(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    function_s svcConnectToPort = findSvcConnectToPort(code_data, code_size);

    return findFunctionReferenceFunction(code_data, code_size, svcConnectToPort, findFatalerrCallback, NULL);
}

bool findFsSetPriorityForFileCallback(u32* code_data32, u32 code_size32, function_s candidate, u32 ref)
{
    int j;

    for(j=candidate.end; j>candidate.start; j--)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_BX))
        {
            int reg = d.Rn;
            if(!darm_armv7_disasm(&d, code_data32[j-1]) && (d.instr == I_LDR && d.Rd == reg && d.imm == 0x18))
            {
                return true;
            }
        }
    }

    return false;
}

function_s findFsSetPriorityForFile(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    function_s ret = findPooledCommandFunction(code_data, code_size, 0xE0E046BC, findFsSetPriorityForFileCallback);
    if(ret.start == ret.end)return ret;

    ret.start = ret.end - 6;

    return ret;
}

void patchPathDirectoryInsert(u8* code_data, u32 code_size, function_s fsFindArchive, function_s fsOpenRom, char* directory)
{
    if(!code_data || !code_size || !directory || fsFindArchive.start == fsFindArchive.end || fsFindArchive.end == 0)return;
    if(fsOpenRom.start == fsOpenRom.end || fsOpenRom.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;

    memcpy(&code_data32[fsOpenRom.start], path_insert_stub_stub, path_insert_stub_stub_size);

    // look for unicode "deadbabe"
    // it's 0x4-aligned
    int i, j;
    for(i=fsOpenRom.start; i<fsOpenRom.end; i++)
    {
        if(code_data32[i] == 0x00650064) // "de"
        {
            int l = strlen(directory);
            for(j=0; j<l; j++)
            {
                code_data[i*4 + j*2] = directory[j];
            }
            if(j < 8)
            {
                code_data[i*4 + j*2] = '/';
                code_data[i*4 + (j+1)*2] = 0x00;
            }
            break;
        }
    }

    u32 stub_size32 = path_insert_stub_stub_size >> 2;
    num_thunks_offset = fsOpenRom.start + stub_size32 - 1;

    findFunctionReferenceFunction(code_data, code_size, fsFindArchive, findFsFindArchiveCallersCallback, NULL);
}

void patchPathDirectoryInsertFindArchive(u8* code_data, u32 code_size, function_s fsFindArchive, function_s fsOpenRom, char* directory)
{
    if(!code_data || !code_size || !directory || fsFindArchive.start == fsFindArchive.end || fsFindArchive.end == 0)return;
    if(fsOpenRom.start == fsOpenRom.end || fsOpenRom.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;

    memcpy(&code_data32[fsOpenRom.start], findarchive_path_insert_stub_stub, findarchive_path_insert_stub_stub_size);

    // look for unicode "deadbabe"
    // it's 0x4-aligned
    int i, j;
    for(i=fsOpenRom.start; i<fsOpenRom.end; i++)
    {
        // if(code_data32[i] == 0x00650064) // "de"
        if(code_data32[i] == 0x00610065) // "ea"
        {
            int l = strlen(directory);
            for(j=0; j<l; j++)
            {
                // code_data[i*4 + j*2] = directory[j];
                code_data[i*4 + (j-1)*2] = directory[j];
            }
            if(j < 8)
            {
                // code_data[i*4 + j*2] = '/';
                // code_data[i*4 + (j+1)*2] = 0x00;
                code_data[i*4 + (j-1)*2] = '/';
                code_data[i*4 + ((j-1)+1)*2] = 0x00;
            }
            break;
        }
    }

    u32 stub_size32 = findarchive_path_insert_stub_stub_size >> 2;
    u32 thunk = fsOpenRom.start + stub_size32 - 7;

    u32 push_instr = code_data32[fsFindArchive.start];
    u32 jump_address = (fsFindArchive.start + 1) * 4 + 0x00100000;

    code_data32[thunk + 3] = push_instr;
    code_data32[thunk + 6] = jump_address;
    
    code_data32[fsFindArchive.start] = 0xEA000000 | ((thunk - fsFindArchive.start - 2) & 0x00FFFFFF); // b origin+4
}

void patchFsOpenRom(u8* code_data, u32 code_size, u32 fsHandle, char* path)
{
    if(!code_data || !code_size)return;
    function_s fsOpenFileDirectly = findFsOpenFileDirectly(code_data, code_size);
    if(fsOpenFileDirectly.start == fsOpenFileDirectly.end)return;

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;

    function_s fatalErr = findFatalerr(code_data, code_size);

    u32 stub_offset = fatalErr.start + 1;
    u32 stub_size32 = openfiledirectly_stub_stub_size / 4;

    memcpy(&code_data32[stub_offset], openfiledirectly_stub_stub, openfiledirectly_stub_stub_size);
    code_data32[stub_offset + 3] = code_data32[fsOpenFileDirectly.start];
    code_data32[stub_offset + stub_size32 - 2] = fsHandle;
    code_data32[stub_offset + stub_size32 - 1] = 0x00100000 + (fsOpenFileDirectly.start + 1) * 4;

    int i;
    for(i=fatalErr.start+1; i<fatalErr.end; i++)
    {
        if(code_data32[i] == 0x6E61682f) // "/han"
        {
            strncpy((char*)&code_data32[i], path, 23);
        }else if(code_data32[i] == 0xdeaddad0)
        {
            code_data32[i] = strlen(path) + 1;
            break;
        }
    }

    code_data32[fatalErr.start] = 0xffffffff;
    code_data32[fsOpenFileDirectly.start] = 0xEA000000 | ((fatalErr.start + 1 - fsOpenFileDirectly.start - 2) & 0x00FFFFFF); // branch

    function_s fsSetPriorityForFile = findFsSetPriorityForFile(code_data, code_size);

    if(fsSetPriorityForFile.start != fsSetPriorityForFile.end)
    {
        code_data32[fsSetPriorityForFile.start + 0] = 0xE3A00000; // mov r0, #0
        code_data32[fsSetPriorityForFile.start + 1] = 0xE12FFF1E; // bx lr
    }
}

void patchRedirectFs(u8* code_data, u32 code_size, u32 fsHandle, char* directory)
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
    
    // function_s fsFindArchive = (function_s){0xA7FF, 0xA830}; // TEMP (MM)
    function_s fsFindArchive = (function_s){0x11183, 0x111B4}; // TEMP (POKEY)
    // function_s fsFindArchive = (function_s){0x26B7, 0x26E7}; // TEMP (HOR)
    // function_s fsFindArchive = (function_s){0x141F3C, 0x141F6E}; // TEMP (STARFOX)
    // function_s fsFindArchive = (function_s){0x15D85E, 0x15D88F}; // TEMP (ZALBW)

    patchFsHighLevelInitialize(code_data, code_size, fsHighLevelInitialize, fsHandle);
    patchFsMountSave(code_data, code_size, fsMountSave);
    patchFsMountRom(code_data, code_size, fsOpenSpecialArchiveRaw, fsOpenRom, fsMountRom);
    patchFsControlArchive(code_data, code_size, fsControlArchive);
    // patchPathDirectoryInsert(code_data, code_size, fsFindArchive, fsOpenRom, directory);
    patchPathDirectoryInsertFindArchive(code_data, code_size, fsFindArchive, fsOpenRom, directory);
}
