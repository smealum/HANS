#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "darm.h"
#include "r5.h"
#include "fsredir.h"

// it's time
// for
// regioooooonFIIIIIIIIIIVE

Result getTitleInformation(u8* mediatype, u64* tid);
Result gspwn(void* dst, void* src, u32 size);

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

void setClockrate(u8 setting)
{
	int j, i;
	u32* patchArea = linearAlloc(0x00100000);

	// grab waitLoop stub
	GSPGPU_FlushDataCache(NULL, (u8*)patchArea, 0x100);
	gspwn(patchArea, (u32*)(MENU_LOADEDROP_BUFADR-0x100), 0x100);
	svcSleepThread(20*1000*1000);

	// patch it
	for(i=0; i<0x100/4; i++)
	{
		if(patchArea[i] == 0x67666E63) // "cnfg"
		{
			patchArea[i+1] = (patchArea[i+1] & ~0xFF) | setting;
			break;
		}
	}

	// copy it back
	GSPGPU_FlushDataCache(NULL, (u8*)patchArea, 0x100);
	gspwn((u32*)(MENU_LOADEDROP_BUFADR-0x100), patchArea, 0x100);
	svcSleepThread(20*1000*1000);

	// ghetto dcache invalidation
	// don't judge me
	for(j=0; j<4; j++)
		for(i=0; i<0x00100000/0x4; i+=0x4)
			patchArea[i+j]^=0xDEADBABE;

	linearFree(patchArea);
}

u8 smdhGetRegionCode(u8* smdh_data)
{
    if(!smdh_data)return 0xFF;

    u8 flags = smdh_data[0x2018];
    int i;
    for(i=0; i<6; i++)if(flags & (1<<i))return i;

    return 0xFF;
}

u8* loadSmdh(u64 tid, u8 mediatype)
{
	Result ret;
	Handle fileHandle;

	u32 archivePath[] = {tid & 0xFFFFFFFF, (tid >> 32) & 0xFFFFFFFF, mediatype, 0x00000000};
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
char* clocks[] = {"268Mhz", "804Mhz"};

typedef enum
{
	CHOICE_REGION = 0,
	CHOICE_LANGUAGE = 1,
	CHOICE_CLOCK = 2,
	CHOICE_SAVE = 3,
	CHOICE_OK
}choices_t;

Result configureTitle(u8* region_code, u8* language_code, u8* clock)
{
	u8 mediatype = 0;
	u64 tid = 0;

	getTitleInformation(&mediatype, &tid);

	if(!tid)return -1;

	static char fn[256];
	sprintf(fn, "titles/%08X%08X.txt", (unsigned int)(tid >> 32), (unsigned int)(tid & 0xFFFFFFFF));

	mkdir("titles", 777);

	u8 numChoices[] = {sizeof(regions) / sizeof(regions[0]), sizeof(languages) / sizeof(languages[0]), sizeof(clocks) / sizeof(clocks[0]), sizeof(yesno) / sizeof(yesno[0]), 0};
	int num_fields = 5;
	int choice[] = {numChoices[0]-1, numChoices[1]-1, 0, 1, 0};

	hidScanInput();

	{
		FILE* f = fopen(fn, "r");
		if(f)
		{
			static char l[256];
			while(fgets(l, sizeof(l), f))
			{
				if(sscanf(l, "region : %d", &choice[CHOICE_REGION]) != 1)
				if(sscanf(l, "language : %d", &choice[CHOICE_LANGUAGE]) != 1);
				if(sscanf(l, "clock : %d", &choice[CHOICE_CLOCK]) != 1);
			}

			if(region_code)*region_code = choice[CHOICE_REGION];
			if(language_code)*language_code = choice[CHOICE_LANGUAGE];
			if(clock)*clock = choice[CHOICE_CLOCK];
			choice[CHOICE_SAVE] = 0;

			fclose(f);

			if(!(hidKeysHeld() & KEY_L))return 0;
		}else{
			u8* smdh_data = loadSmdh(tid, mediatype);
			if(smdh_data)
			{
				choice[CHOICE_REGION] = smdhGetRegionCode(smdh_data);
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
		printf(field == CHOICE_REGION ?   "  Region             : < %s > \n" : "  Region             :   %s   \n", regions[choice[CHOICE_REGION]]);
		printf(field == CHOICE_LANGUAGE ? "  Language           : < %s > \n" : "  Language           :   %s   \n", languages[choice[CHOICE_LANGUAGE]]);
		printf(field == CHOICE_CLOCK ?    "  N3DS CPU clock     : < %s > \n" : "  N3DS CPU clock     :   %s   \n", clocks[choice[CHOICE_CLOCK]]);
		printf(field == CHOICE_SAVE ?     "  Save configuration : < %s > \n" : "  Save configuration :   %s   \n", yesno[choice[CHOICE_SAVE]]);
		printf("\n");
		printf(             "  Current title      : %08X%08X\n", (unsigned int)(tid >> 32), (unsigned int)(tid & 0xFFFFFFFF));
		printf("\n");
		printf(field == CHOICE_OK ? "             > OK\n" : "               OK\n");

		gfxFlushBuffers();
		gfxSwapBuffers();

		gspWaitForVBlank();
	}

	if(choice[CHOICE_REGION] >= numChoices[CHOICE_REGION] - 1)choice[CHOICE_REGION] = -1;
	if(choice[CHOICE_LANGUAGE] >= numChoices[CHOICE_LANGUAGE] - 1)choice[CHOICE_LANGUAGE] = -1;

	if(choice[CHOICE_SAVE] == 0)
	{
		FILE* f = fopen(fn, "w");
		if(f)
		{
			fprintf(f, "region : %d\nlanguage : %d\nclock : %d\n", choice[CHOICE_REGION], choice[CHOICE_LANGUAGE], choice[CHOICE_CLOCK]);

			fclose(f);
		}
	}

	if(region_code)*region_code = choice[CHOICE_REGION];
	if(language_code)*language_code = choice[CHOICE_LANGUAGE];
	if(clock)*clock = choice[CHOICE_CLOCK];

	return 0;
}

void doRegionFive(u8* code_data, u32 code_size)
{
    u8 region_code = 2;
    u8 language_code = 2;
    u8 clock = 0;

    configureTitle(&region_code, &language_code, &clock);

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

	if(clock != 0xFF)
	{
		setClockrate(clock);
	}

	// TEMP
	Handle fsHandle;
	srvGetServiceHandle(&fsHandle, "fs:USER");
	// {
	// 	char directory[9];
	// 	u64 tid = 0;
	// 	getTitleInformation(NULL, &tid);
	// 	sprintf(directory, "%08X", (unsigned int)(tid&0xFFFFFFFF));

	// 	patchRedirectFs(code_data, code_size, fsHandle, directory);
	// }

	{
		// Handle fileHandle;
		// FS_archive sdmcArchive=(FS_archive){ARCH_SDMC, (FS_path){PATH_EMPTY, 1, (u8*)""}};
		// FS_path filePath=FS_makePath(PATH_CHAR, "/mm.romfs");

		// FSUSER_OpenFileDirectly(NULL, &fileHandle, sdmcArchive, filePath, FS_OPEN_READ, FS_ATTRIBUTE_NONE);

		// patchFsOpenRom(code_data, code_size, fileHandle);
		patchFsOpenRom(code_data, code_size, fsHandle);
	}
}
