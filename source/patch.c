#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "patch.h"


function_s findFunction(u32* code_data32, u32 code_size32, u32 start)
{
    if(!code_data32 || !code_size32)return (function_s){0,0};

    // super crappy but should work most of the time
    // (we're only searching for small uncomplicated functions)

    function_s c = {start, start};
    int j;

    for(j=start; j<code_size32; j++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && ((d.instr == I_POP && d.Rn == SP) || (d.instr == I_BX && d.Rm == LR)))
        {
            c.end = j;
            break;
        }
    }

    for(j=start; j>0; j--)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_PUSH && d.Rn == SP))
        {
            c.start = j;
            break;
        }
    }

    return c;
}

function_s findPooledCommandFunction(u8* code_data, u32 code_size, u32 pooled_value, validationCallback_t callback)
{
    if(!code_data || !code_size)return (function_s){0,0};

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i, j;

    for(i=0; i<code_size32; i++)
    {
        if(code_data32[i] == pooled_value)
        {
            function_s c = findFunction(code_data32, code_size32, i-4);

            for(j=c.start; j<=c.end; j++)
            {
                darm_t d;
                if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_LDR && d.Rn == PC && (i-j-2)*4 == d.imm))
                {
                    if(callback)
                    {
                        if(callback(code_data32, code_size32, c, i))return c;
                    }else return c;
                }
            }
        }
    }

    return (function_s){0,0};
}

function_s findFunctionReferenceFunction(u8* code_data, u32 code_size, function_s ref, validationCallback_t callback, u32* reference)
{
    if(!code_data || !code_size)return (function_s){0,0};
    if(ref.start == ref.end)return (function_s){0,0};

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;

    int i;
    for(i=0; i<code_size32; i++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_BL && (ref.start-i-2)*4 == d.imm))
        {
            function_s c = findFunction(code_data32, code_size32, i);

            if(reference) *reference = i;

            if(callback)
            {
                if(callback(code_data32, code_size32, c, i))return c;
            }else return c;
        }
    }

    if(reference) *reference = 0;
    return (function_s){0,0};
}
