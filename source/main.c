#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "decomp.h"
#include "common.h"
#include "r5.h"
#include "loader_bin.h"

paramblk_t* paramblk = NULL;

Result gspwn(void* dst, void* src, u32 size)
{
	return GX_SetTextureCopy(NULL, src, 0xFFFFFFFF, dst, 0xFFFFFFFF, size, 0x8);
}

void buildMap(memorymap_t* mm, u32 size)
{
	if(!mm)return;

	// init
	mm->num = 0;

	// figure out the physical memory map
	u32 current_size = size;
	u32 current_offset = 0x00000000;

	int i;
	for(i=0; i<2; i++)
	{
		const u32 mask = 0x000fffff >> (4*i);
		u32 section_size = current_size & ~mask;
		if(section_size)
		{
			if(!mm->num) mm->processLinearOffset = section_size;
			current_offset += section_size;
			mm->map[mm->num++] = (memorymap_entry_t){0x00100000 + current_offset - section_size, mm->processLinearOffset - current_offset, section_size};
			current_size -= section_size;
		}
	}
}

memorymap_t processMap;

// bypass handle list
Result _srvGetServiceHandle(Handle* out, const char* name)
{
	Result rc = 0;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x50100;
	strcpy((char*) &cmdbuf[1], name);
	cmdbuf[3] = strlen(name);
	cmdbuf[4] = 0x0;
	
	if((rc = svcSendSyncRequest(*srvGetSessionHandle())))return rc;

	*out = cmdbuf[3];
	return cmdbuf[1];
}

Result getTitleInformation(u8* mediatype, u64* tid)
{
	Result ret = 0;

	if(mediatype)
	{
		Handle localFsHandle;

		ret = _srvGetServiceHandle(&localFsHandle, "fs:USER");
		if(ret)return ret;
		
		ret = FSUSER_Initialize(&localFsHandle);
		if(ret)return ret;

		ret = FSUSER_GetMediaType(&localFsHandle, mediatype);

		svcCloseHandle(localFsHandle);
	}

	if(tid)
	{
		aptOpenSession();
		ret = APT_GetProgramID(NULL, tid);
		aptCloseSession();
	}

	return ret;
}

Result loadCode()
{
	Result ret;
	Handle fileHandle;

	u8 mediatype = 0;
	u64 tid = 0;

	ret = getTitleInformation(&mediatype, &tid);

	printf("%08X : %d, %08X, %08X\n", (unsigned int)ret, mediatype, (unsigned int)tid & 0xFFFFFFFF, (unsigned int)(tid >> 32) & 0xFFFFFFFF);

	u32 archivePath[] = {tid & 0xFFFFFFFF, (tid >> 32) & 0xFFFFFFFF, mediatype, 0x00000000};
	static const u32 filePath[] = {0x00000000, 0x00000000, 0x00000002, 0x646F632E, 0x00000065};

	// ret = FSUSER_OpenFileDirectly(&localFsHandle, &fileHandle, (FS_archive){0x00000003, (FS_path){PATH_EMPTY, 1, (u8*)""}}, (FS_path){PATH_BINARY, 0x14, (u8*)filePath}, FS_OPEN_READ, FS_ATTRIBUTE_NONE);
	ret = FSUSER_OpenFileDirectly(NULL, &fileHandle, (FS_archive){0x2345678a, (FS_path){PATH_BINARY, 0x10, (u8*)archivePath}}, (FS_path){PATH_BINARY, 0x14, (u8*)filePath}, FS_OPEN_READ, FS_ATTRIBUTE_NONE);

	printf("loading code : %08X\n", (unsigned int)ret);

	u8* fileBuffer = NULL;
	u64 fileSize = 0;

	{
		u32 bytesRead;

		ret = FSFILE_GetSize(fileHandle, &fileSize);
		if(ret)return ret;

		fileBuffer = malloc(fileSize);
		if(ret)return ret;

		ret = FSFILE_Read(fileHandle, &bytesRead, 0x0, fileBuffer, fileSize);
		if(ret)return ret;

		ret = FSFILE_Close(fileHandle);
		if(ret)return ret;

		printf("loaded code : %08X\n", (unsigned int)fileSize);
	}

	u32 decompressedSize = lzss_get_decompressed_size(fileBuffer, fileSize);
	printf("decompressed code size : %08X\n", (unsigned int)decompressedSize);
	u8* decompressedBuffer = linearMemAlign(decompressedSize, 0x1000);
	if(!decompressedBuffer)return -1;

	lzss_decompress(fileBuffer, fileSize, decompressedBuffer, decompressedSize);

	buildMap(&processMap, decompressedSize);
	printf("map built : %08X\n", (unsigned int)(FIRM_APPMEMALLOC_LINEAR - processMap.processLinearOffset));

	paramblk->code_data = (u32*)decompressedBuffer;
	paramblk->code_size = decompressedSize;

	return 0;
}

void patchCode()
{
	printf("patchin\n");

	// u8* data = (u8*)paramblk->code_data;

	// int i;
	// for(i=0; i<paramblk->code_size; i++)
	// {
	// 	// if(!memcmp(&data[i], "pikachu", 7))memcpy(&data[i], "lucario", 7);
	// 	if(!memcmp(&data[i], "Pikachu", 7))memcpy(&data[i], "Lucario", 7);

	// 	if(!(i&0xfffff))printf("\x1b[0;0H%d\n", (100*i)/paramblk->code_size);
	// }

	// strcpy((char*)&data[0x43D288], "rom:/actor/zelda_link_boy_new.zar");

	// *(u32*)(&data[0x0013765C-0x00100000]) = 0xE12FFF1E ; // bx lr
	// *(u32*)(&data[0x00162420-0x00100000]) = 0xE3A00001 ; // mov r0, #1 ; (USA)

	// *(u32*)(&data[0x0013C7C8-0x00100000]) = 0x00000000;
	// *(u32*)(&data[0x0013C7D4-0x00100000]) = 0x00000000;

	// *(u32*)(&data[0x002163C8-0x00100000]) = 0xE3A00002; // mov r0, #2 ; (EUR)

	doRegionFive((u8*)paramblk->code_data, paramblk->code_size);

	printf("ballin\n");
}

Result NSS_LaunchTitle(Handle* handle, u64 tid, u8 flags)
{
	if(!handle)return -1;

	u32* cmdbuf=getThreadCommandBuffer();
	cmdbuf[0]=0x000200C0; //request header code
	cmdbuf[1]=tid&0xFFFFFFFF;
	cmdbuf[2]=(tid>>32)&0xFFFFFFFF;
	cmdbuf[3]=flags;

	Result ret=0;
	if((ret=svcSendSyncRequest(*handle)))return ret;

	return cmdbuf[1];
}

Result NSS_TerminateProcessTID(Handle* handle, u64 tid, u64 timeout)
{
	if(!handle)return -1;

	u32* cmdbuf=getThreadCommandBuffer();
	cmdbuf[0]=0x00110100; //request header code
	cmdbuf[1]=tid&0xFFFFFFFF;
	cmdbuf[2]=(tid>>32)&0xFFFFFFFF;
	cmdbuf[3]=timeout&0xFFFFFFFF;
	cmdbuf[4]=(timeout>>32)&0xFFFFFFFF;

	Result ret=0;
	if((ret=svcSendSyncRequest(*handle)))return ret;

	return cmdbuf[1];
}

extern void (*__system_retAddr)(void);

extern u32 __linear_heap;
extern u32 __heapBase;
extern u32 __heap_size, __linear_heap_size;
extern void (*__system_retAddr)(void);

void __destroy_handle_list(void);
void __appExit();

void __libc_fini_array(void);

void runLoaderThunk()
{
	void (*loader)(paramblk_t* p) = (void*)(0x00100000 + 4);

	paramblk->linear_heap = __linear_heap;
	paramblk->linear_size = __linear_heap_size;
	paramblk->map = processMap;

	loader(paramblk);
}

void runLoader()
{
	u8* buffer = linearMemAlign(loader_bin_size, 0x100);

	if(!buffer)return;

	memcpy(buffer, loader_bin, loader_bin_size);

	Result ret = GSPGPU_FlushDataCache(NULL, buffer, HANS_LOADER_SIZE);
	ret = gspwn((void*)(FIRM_APPMEMALLOC_LINEAR - processMap.processLinearOffset), buffer, HANS_LOADER_SIZE);
	svcSleepThread(20*1000*1000);

	linearFree(buffer);

	// use ns:s to launch/kill process and invalidate icache in the process
	{
		ret = NSS_LaunchTitle(&paramblk->nssHandle, 0x0004013000002A02LL, 0x1);
		if(ret)return;

		svcSleepThread(100*1000*1000);
		ret = NSS_TerminateProcessTID(&paramblk->nssHandle, 0x0004013000002A02LL, 100*1000*1000);
		if(ret)return;
	}

	__system_retAddr = runLoaderThunk;

	printf("prepared to run loader");
}

void __attribute__((noreturn)) __libctru_exit(int rc)
{
	u32 tmp=0;

	// Unmap the application heap
	svcControlMemory(&tmp, __heapBase, 0x0, __heap_size, MEMOP_FREE, 0x0);

	// Close some handles
	__destroy_handle_list();

	// Jump to the loader if it provided a callback
	if (__system_retAddr)
		__system_retAddr();

	// Since above did not jump, end this process
	svcExitProcess();
}

void __appInit() {
	// Initialize services
	srvInit();
	// aptInit();
	hidInit(NULL);

	fsInit();
	sdmcInit();
}

void __appExit() {
	// Exit services
	sdmcExit();
	fsExit();

	hidExit();
	// aptExit();
	srvExit();
}

int main(int argc, char **argv)
{
	gfxInitDefault();

	consoleInit(GFX_TOP, NULL);

	printf("\x1b[15;19Hwhat is up\n");


	// Result ret;
	// u8 currentType;
	// u32 requestedAppid, menuAppid, currentAppid;

	// int i;
	// for(i=0;i<5;i++)
	// {
	// 	aptOpenSession();
	// 	ret=APT_GetAppletManInfo(NULL, i, &currentType, &requestedAppid, &menuAppid, &currentAppid);
	// 	aptCloseSession();
	// 	printf("%d (%x) : %x %x %x %x\n", i, ret, currentType, requestedAppid, menuAppid, currentAppid);
	// }


	paramblk = linearAlloc(sizeof(paramblk_t));

	srvGetServiceHandle(&paramblk->nssHandle, "ns:s");

	loadCode();
	runLoader();
	patchCode();

	hidScanInput();
	if(hidKeysHeld() & KEY_Y)
	{
		while(true)
		{
			hidScanInput();
			if(hidKeysDown() & KEY_START)break;
		}
	}


	// // while (aptMainLoop())
	// while(true)
	// {
	// 	hidScanInput();

	// 	u32 kDown = hidKeysDown();

	// 	// if (kDown & KEY_X) freeHomeMenuResources();
	// 	// if (kDown & KEY_A) crashHomeMenu();
	// 	// if (kDown & KEY_B) crashApp();
	// 	// if (kDown & KEY_Y) loadCode();
	// 	// if (kDown & KEY_L) patchCode();
	// 	// if (kDown & KEY_SELECT) runLoader();
	// 	// if (kDown & KEY_DOWN) retMenu();
	// 	if (kDown & KEY_START) break;

	// 	gfxFlushBuffers();
	// 	gfxSwapBuffers();

	// 	gspWaitForVBlank();
	// }

	gfxExit();
	return 0;
}
