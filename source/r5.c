#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "r5.h"

// it's time
// for
// regioooooonFIIIIIIIIIIVE

typedef struct
{
    u32 start, end;
}function_s;

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

function_s findCfgSecureInfoGetRegion(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i, j, k;

    static function_s candidates[32];
    int num_candidates = 0;

    for(i=0; i<code_size32; i++)
    {
        // search for "mov r0, #0x20000"
        if(code_data32[i] == 0xE3A00802)
        {
            function_s c = findFunction(code_data32, code_size32, i);

            // start at i because svc 0x32 should always be after the mov
            for(j=i; j<=c.end; j++)
            {
                if(code_data32[j] == 0xEF000032)
                {
                    candidates[num_candidates++] = c;
                    break;
                }
            }
        }
    }

    // look for error code which is known to be stored near cfg:u handle
    // this way we can find the right candidate
    // (handle should also be stored right after end of candidate function)
    for(i=0; i<code_size32; i++)
    {
        if(code_data32[i] == 0xD8A103F9)
        {
            for(j=i-4; j<i+4; j++)
            {
                for(k=0; k<num_candidates; k++)
                {
                    if(code_data32[j] == code_data32[candidates[k].end + 1])
                    {
                        return candidates[k];
                    }
                }
            }
        }
    }

    return (function_s){0,0};
}

function_s findCfgCtrGetLanguage(u8* code_data, u32 code_size)
{
    if(!code_data || !code_size)return (function_s){0,0};

    u32* code_data32 = (u32*)code_data;
    u32 code_size32 = code_size / 4;
    int i, j;

    for(i=0; i<code_size32; i++)
    {
        if(code_data32[i] == 0x000A0002)
        {
            function_s c = findFunction(code_data32, code_size32, i-4);

            for(j=c.start; j<=c.end; j++)
            {
                darm_t d;
                if(!darm_armv7_disasm(&d, code_data32[j]) && (d.instr == I_LDR && d.Rn == PC && (i-j-2)*4 == d.imm))
                {
                    return c;
                }
            }
        }
    }

    return (function_s){0,0};
}

void patchCfgSecureInfoGetRegion(u8* code_data, u32 code_size, function_s c, u8 region_code)
{
    if(!code_data || !code_size || c.start == c.end || c.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    int i;

    for(i = c.start; i<c.end; i++)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && d.instr == I_LDRB)
        {
            printf("%08X %d\n", i * 4 + 0x00100000, d.Rt);
            code_data32[i] = 0xE3A00000 | (d.Rt << 12) | region_code;
            break;
        }
    }
}

void patchCfgCtrGetLanguage(u8* code_data, u32 code_size, function_s c, u8 language_code)
{
    if(!code_data || !code_size || c.start == c.end || c.end == 0)return;

    u32* code_data32 = (u32*)code_data;
    int i;

    for(i = c.end; i>c.start; i--)
    {
        darm_t d;
        if(!darm_armv7_disasm(&d, code_data32[i]) && (d.instr == I_LDRB && d.Rt == 0))
        {
            printf("%08X\n", i * 4 + 0x00100000);
            code_data32[i] = 0xE3A00000 | (d.Rt << 12) | language_code;
            break;
        }
    }
}

u8 smdhGetRegionCode(u8* smdh_data)
{
    if(!smdh_data)return 0xFF;

    u8 flags = smdh_data[0x2018];
    int i;
    for(i=0; i<6; i++)if(flags & (1<<i))return i;

    return 0xFF;
}

u8* loadSmdh()
{
	Result ret;
	Handle fileHandle;

	static const u32 archivePath[] = {0x00000000, 0x00000000, 0x00000002, 0x00000000};
	static const u32 filePath[] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000}; // icon

	ret = FSUSER_OpenFileDirectly(NULL, &fileHandle, (FS_archive){0x2345678a, (FS_path){PATH_BINARY, 0x10, (u8*)archivePath}}, (FS_path){PATH_BINARY, 0x14, (u8*)filePath}, FS_OPEN_READ, FS_ATTRIBUTE_NONE);

	printf("loading smdh : %08X\n", (unsigned int)ret);

	u8* fileBuffer = NULL;
	u64 fileSize = 0;

	{
		u32 bytesRead;

		ret = FSFILE_GetSize(fileHandle, &fileSize);
		if(ret)return NULL;

		fileBuffer = malloc(fileSize);
		if(ret)return NULL;

		ret = FSFILE_Read(fileHandle, &bytesRead, 0x0, fileBuffer, fileSize);
		if(ret)return NULL;

		ret = FSFILE_Close(fileHandle);
		if(ret)return NULL;

		printf("loaded code : %08X\n", (unsigned int)fileSize);
	}

	return fileBuffer;
}

char* regions[] = {"JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN", "---"};
char* languages[] = {"JP", "EN", "FR", "DE", "IT", "ES", "ZH", "KO", "NL", "PT", "RU", "TW", "--"};
char* yesno[] = {"YES", "NO"};

u64 getGamecardTid()
{
	amInit();

	u64 tid = 0;
	AM_GetTitleIdList(2, 1, &tid);

	amExit();

	return tid;
}

Result configureTitle(u8* region_code, u8* language_code)
{
	u64 tid = getGamecardTid();

	if(!tid)return -1;

	static char fn[256];
	sprintf(fn, "titles/%08X%08X.txt", (unsigned int)(tid >> 32), (unsigned int)(tid & 0xFFFFFFFF));

	mkdir("titles", 777);

	u8 numChoices[] = {sizeof(regions) / sizeof(regions[0]), sizeof(languages) / sizeof(languages[0]), sizeof(yesno) / sizeof(yesno[0]), 0};
	int num_fields = 4;
	int choice[] = {0xFF, 0xFF, 1, 0};

	hidScanInput();

	{
		FILE* f = fopen(fn, "r");
		if(f)
		{
			static char l[256];
			while(fgets(l, sizeof(l), f))
			{
				if(sscanf(l, "region : %d", &choice[0]) != 1)
				if(sscanf(l, "language : %d", &choice[1]) != 1);
			}

			if(region_code)*region_code = choice[0];
			if(language_code)*language_code = choice[1];
			choice[2] = 0;

			fclose(f);

			if(!(hidKeysHeld() & KEY_L))return 0;
		}else{
			u8* smdh_data = loadSmdh();
			if(smdh_data)
			{
				choice[0] = smdhGetRegionCode(smdh_data);
				free(smdh_data);
			}
		}
	}

	consoleClear();

	int field = 0;
	while(1)
	{
		hidScanInput();

		u32 kDown = hidKeysDown();

		if((kDown & KEY_START) || ((kDown & KEY_A) && field == num_fields - 1))break;

		if(kDown & KEY_UP)field--;
		if(kDown & KEY_DOWN)field++;

		if(field < 0)field = 0;
		if(field >= num_fields)field = num_fields - 1;

		if(kDown & KEY_LEFT)choice[field]--;
		if(kDown & KEY_RIGHT)choice[field]++;

		if(choice[field] < 0) choice[field] = numChoices[field] - 1;
		if(choice[field] >= numChoices[field]) choice[field] = 0;

		printf("\x1b[0;0H\n");
		printf(             "               r5             \n");
		printf("\n");
		printf(field == 0 ? "  Region             : < %s > \n" : "  Region             :   %s   \n", regions[choice[0]]);
		printf(field == 1 ? "  Language           : < %s > \n" : "  Language           :   %s   \n", languages[choice[1]]);
		printf(field == 2 ? "  Save configuration : < %s > \n" : "  Save configuration :   %s   \n", yesno[choice[2]]);
		printf("\n");
		printf(             "  Current title      : %08X%08X\n", (unsigned int)(tid >> 32), (unsigned int)(tid & 0xFFFFFFFF));
		printf("\n");
		printf(field == 3 ? "             > OK\n" : "               OK\n");

		gfxFlushBuffers();
		gfxSwapBuffers();

		gspWaitForVBlank();
	}

	if(choice[0] >= numChoices[0] - 1)choice[0] = -1;
	if(choice[1] >= numChoices[1] - 1)choice[1] = -1;

	if(choice[2] == 0)
	{
		FILE* f = fopen(fn, "w");
		if(f)
		{
			fprintf(f, "region : %d\nlanguage : %d\n", choice[0], choice[1]);

			fclose(f);
		}
	}

	if(region_code)*region_code = choice[0];
	if(language_code)*language_code = choice[1];

	return 0;
}

void doRegionFive(u8* code_data, u32 code_size)
{
    u8 region_code = 2;
    u8 language_code = 2;

    configureTitle(&region_code, &language_code);

    printf("region %X\n", region_code);
    printf("language %X\n", language_code);

    if(region_code != 0xFF)
    {
	    function_s cfgSecureInfoGetRegion = findCfgSecureInfoGetRegion(code_data, code_size);

	    printf("cfgSecureInfoGetRegion : %08X - %08X\n", (unsigned int)(cfgSecureInfoGetRegion.start * 4 + 0x00100000), (unsigned int)(cfgSecureInfoGetRegion.end * 4 + 0x00100000));

	    patchCfgSecureInfoGetRegion(code_data, code_size, cfgSecureInfoGetRegion, region_code);
    }

    if(language_code != 0xFF)
	{
	    function_s cfgCtrGetLanguage = findCfgCtrGetLanguage(code_data, code_size);
	    
	    printf("cfgCtrGetLanguage : %08X - %08X\n", (unsigned int)(cfgCtrGetLanguage.start * 4 + 0x00100000), (unsigned int)(cfgCtrGetLanguage.end * 4 + 0x00100000));

	    patchCfgCtrGetLanguage(code_data, code_size, cfgCtrGetLanguage, language_code);
	}
}
