#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "decomp.h"
#include "common.h"
#include "r5.h"
#include "loader_bin.h"
#include "app_code_bin.h"

paramblk_t* paramblk = NULL;

Result gspwn(void* dst, void* src, u32 size)
{
	return GX_SetTextureCopy(NULL, src, 0xFFFFFFFF, dst, 0xFFFFFFFF, size, 0x8);
}

u8 data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x01, 0x08, 0xA0, 0xE3, 0x00, 0x10, 0xA0, 0xE3, 0x0A, 0x00, 0x00, 0xEF, 0xFB, 0xFF, 0xFF, 0xEA};

void crashHomeMenu()
{
	u8* buffer = linearMemAlign(0x00100000, 0x100);
	int i, j;

	if(!buffer)return;

	memset(buffer, 0xFF, 0x100);
	memcpy(buffer, data, sizeof(data));

	printf("\x1b[16;19Hattempting to crash home menu\n");
	printf("%08X\n", (unsigned int)buffer);

	// grab bkp
	GSPGPU_FlushDataCache(NULL, (u8*)buffer, 0x8000);
	gspwn((u32*)buffer, (u32*)(MENU_LOADEDROP_BKP_BUFADR), 0x8000);
	svcSleepThread(20*1000*1000);

	// patch it
	u32* patchArea = (u32*)buffer;
	for(i=0; i<0x8000/4; i++)
	{
		// if(patchArea[i] == 0xBABEBAD0)
		// {
		// 	patchArea[i-4] = 0xdead0000;
		// 	break;
		// }
		if(patchArea[i] == 0xBABE0004)
		{
			patchArea[i+7] = 0xdead0000;
			break;
		}
		// if(patchArea[i] == 0x0BEBC200)
		// {
		// 	patchArea[i-1] = 0xdead0000;
		// 	break;
		// }
	}
	// // overwrite hook code
	// for(i=0; i<0x8000/4; i++)
	// {
	// 	if(patchArea[i] == 0xE1A00000) // NOP
	// 	{
	// 		memcpy(&patchArea[i], data, sizeof(data));
	// 		break;
	// 	}
	// }
	// // overwrite app_code
	// for(i=0; i<0x8000/4; i++)
	// {
	// 	if(patchArea[i] == 0xEA000001)
	// 	{
	// 		memcpy(&patchArea[i], app_code_bin, app_code_bin_size);
	// 		break;
	// 	}
	// }
	// // overwrite bootloader
	// for(i=0; i<0x8000/4; i++)
	// {
	// 	if(patchArea[i] == 0xEA00001F)
	// 	{
	// 		memcpy(&patchArea[i], data, sizeof(data));
	// 		break;
	// 	}
	// }

	// copy it back
	GSPGPU_FlushDataCache(NULL, (u8*)buffer, 0x8000);
	gspwn((u32*)(MENU_LOADEDROP_BKP_BUFADR), (u32*)buffer, 0x8000);
	svcSleepThread(20*1000*1000);

	// // corrupt menu rop wait stub
	// Result ret = GSPGPU_FlushDataCache(NULL, buffer, 0x100);
	// printf("%08X\n", (unsigned int)ret);
	// // ret = gspwn((void*)(MENU_LOADEDROP_BUFADR-0x100), buffer, 0x100);
	// ret = gspwn((void*)(MENU_LOADEDROP_BKP_BUFADR+0x400), buffer, 0x100);
	// // ret = gspwn((void*)(MENU_LOADEDROP_BKP_BUFADR+0x31e0), buffer, 0x100);
	// printf("%08X\n", (unsigned int)ret);
	// svcSleepThread(20*1000*1000);

	// ghetto dcache invalidation
	// don't judge me
	for(j=0; j<0x4; j++)
		for(i=0; i<0x00100000/0x4; i+=0x4)
			((u32*)buffer)[i+j]^=0xDEADBABE;

	linearFree(buffer);
}

typedef struct {
	u32 num;

	struct {
		char name[8];
		Handle handle;
	} services[];
} service_list_t;

extern service_list_t* __service_ptr;

void freeHomeMenuResources()
{
	if(!__service_ptr)return;

	int i;
	for(i=0; i<__service_ptr->num; i++)
	{
		svcCloseHandle(__service_ptr->services[i].handle);
	}

	sdmcExit();
	fsExit();
	irrstExit();

	printf("\x1b[16;19Hfreed all the menu resources (?)\n");
}

void crashApp()
{
	*(u32*)NULL = 0xdead;
}

extern Handle g_srv_handle;

// bypass handle list
Result _srvGetServiceHandle(Handle* out, const char* name)
{
	Result rc = 0;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x50100;
	strcpy((char*) &cmdbuf[1], name);
	cmdbuf[3] = strlen(name);
	cmdbuf[4] = 0x0;
	
	if((rc = svcSendSyncRequest(g_srv_handle)))return rc;

	*out = cmdbuf[3];
	return cmdbuf[1];
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

Result loadCode()
{
	Result ret;
	// Handle localFsHandle;
	Handle fileHandle;
	
	// ret = _srvGetServiceHandle(&localFsHandle, "fs:USER");
	// if(ret)return ret;
	
	// ret = FSUSER_Initialize(&localFsHandle);
	// if(ret)return ret;

	static const u32 archivePath[] = {0x00000000, 0x00000000, 0x00000002, 0x00000000};
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

void __attribute__((weak)) __attribute__((noreturn)) __libctru_exit(int rc)
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

void __appInit()
{
	// Initialize services
	srvInit();
	aptInit();
	hidInit(NULL);

	fsInit();
	sdmcInit();
}

void __appExit()
{
	// Exit services
	sdmcExit();
	fsExit();

	hidExit();
	aptExit();
	srvExit();
}

void retMenu()
{
	u32 tmp0 = 1, tmp1 = 0;

	printf("returning to menu");

	aptOpenSession();
	APT_AppletUtility(NULL, NULL, 0x6, 0x4, (u8*)&tmp0, 0x1, (u8*)&tmp1);
	aptCloseSession();

	aptOpenSession();
	APT_PrepareToJumpToHomeMenu(NULL); //prepare for return to menu
	aptCloseSession();

	GSPGPU_ReleaseRight(NULL); //disable GSP module access

	aptOpenSession();
	APT_JumpToHomeMenu(NULL, 0x0, 0x0, 0x0); //jump !
	aptCloseSession();

	aptOpenSession();
	APT_NotifyToWait(NULL, 0x300);
	aptCloseSession();

	tmp0 = 0;
	aptOpenSession();
	APT_AppletUtility(NULL, NULL, 0x4, 0x1, (u8*)&tmp0, 0x1, (u8*)&tmp1);
	aptCloseSession();
}

int main(int argc, char **argv)
{
	gfxInitDefault();

	consoleInit(GFX_TOP, NULL);

	printf("\x1b[15;19Hwhat is up\n");

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

	// 	if (kDown & KEY_X) freeHomeMenuResources();
	// 	if (kDown & KEY_A) crashHomeMenu();
	// 	if (kDown & KEY_B) crashApp();
	// 	if (kDown & KEY_Y) loadCode();
	// 	if (kDown & KEY_L) patchCode();
	// 	if (kDown & KEY_SELECT) runLoader();
	// 	if (kDown & KEY_DOWN) retMenu();
	// 	if (kDown & KEY_START) break;

	// 	gfxFlushBuffers();
	// 	gfxSwapBuffers();

	// 	gspWaitForVBlank();
	// }

	gfxExit();
	return 0;
}
