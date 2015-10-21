#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "nimpatch.h"

#include "nim_checkupdate_stub_stub.h"

static function_s nimCheckSysupdateAvailableSOAP = (function_s){0,0};

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

void patchNimCheckSysupdateAvailableSOAP(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return;

    function_s ret = findNimCheckSysupdateAvailableSOAP(code_data, code_size);
    if(ret.start == ret.end)return;

    memcpy(&code_data[ret.start * 4], nim_checkupdate_stub_stub, nim_checkupdate_stub_stub_size);
}
