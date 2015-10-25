#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctr/types.h>
#include <ctr/srv.h>
#include <ctr/svc.h>
#include <ctr/FS.h>
#include <ctr/GSP.h>
#include <ctr/GX.h>

#include <common.h>

#include "text.h"
#include "stub_bin.h"

u8* _heap_base; // should be 0x08000000
extern const u32 _heap_size;

u8* gspHeap;
u32 gspHeap_size;
u32* gxCmdBuf;
Handle gspEvent, gspSharedMemHandle;

u8* top_framebuffer;
u8* low_framebuffer;

char console_buffer[4096];

void gspGpuInit()
{
	gspInit();

	GSPGPU_AcquireRight(NULL, 0x0);

	// //set subscreen to red
	// u32 regData=0x010000FF;
	// GSPGPU_WriteHWRegs(NULL, 0x202A04, &regData, 4);

	//setup our gsp shared mem section
	u8 threadID;
	svc_createEvent(&gspEvent, 0x0);
	GSPGPU_RegisterInterruptRelayQueue(NULL, gspEvent, 0x1, &gspSharedMemHandle, &threadID);
	svc_mapMemoryBlock(gspSharedMemHandle, 0x10002000, 0x3, 0x10000000);

	//wait until we can write stuff to it
	svc_waitSynchronization1(gspEvent, 0x55bcb0);

	gxCmdBuf=(u32*)(0x10002000+0x800+threadID*0x200);

	top_framebuffer = &gspHeap[0];
	low_framebuffer = &gspHeap[0x46500];

	GSP_FramebufferInfo topfb = (GSP_FramebufferInfo){0, (u32*)top_framebuffer, (u32*)top_framebuffer, 240 * 3, (1<<8)|(1<<6)|1, 0, 0};
	GSP_FramebufferInfo lowfb = (GSP_FramebufferInfo){0, (u32*)low_framebuffer, (u32*)low_framebuffer, 240 * 3, 1, 0, 0};
	
	GSPGPU_SetBufferSwap(NULL, 0, &topfb);
	GSPGPU_SetBufferSwap(NULL, 1, &lowfb);
}

void gspGpuExit()
{
	GSPGPU_UnregisterInterruptRelayQueue(NULL);

	//unmap GSP shared mem
	svc_unmapMemoryBlock(gspSharedMemHandle, 0x10002000);
	svc_closeHandle(gspSharedMemHandle);
	svc_closeHandle(gspEvent);

	//free GSP heap
	svc_controlMemory((u32*)&gspHeap, (u32)gspHeap, 0x0, gspHeap_size, MEMOP_FREE, 0x0);

	GSPGPU_ReleaseRight(NULL);
	
	gspExit();
}

void doGspwn(u32* src, u32* dst, u32 size)
{
	size += 0x1f;
	size &= ~0x1f;
	GX_SetTextureCopy(gxCmdBuf, src, 0xFFFFFFFF, dst, 0xFFFFFFFF, size, 0x00000008);
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
	if((ret=svc_sendSyncRequest(*handle)))return ret;

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
	if((ret=svc_sendSyncRequest(*handle)))return ret;

	return cmdbuf[1];
}

void swapBuffers()
{
}

const u8 hexTable[]=
{
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

void hex2str(char* out, u32 val)
{
	int i;
	for(i=0;i<8;i++){out[7-i]=hexTable[val&0xf];val>>=4;}
	out[8]=0x00;
}

void renderString(char* str, int x, int y)
{
	drawString(top_framebuffer,str,x,y);
	GSPGPU_FlushDataCache(NULL, top_framebuffer, 240*400*3);
}

void centerString(char* str, int y)
{
	int x=200-(strlen(str)*4);
	drawString(top_framebuffer,str,x,y);
	GSPGPU_FlushDataCache(NULL, top_framebuffer, 240*400*3);
}

void drawHex(u32 val, int x, int y)
{
	char str[9];

	hex2str(str,val);
	renderString(str,x,y);
}

void clearScreen(u8 shade)
{
	memset(top_framebuffer, shade, 240*400*3);
	GSPGPU_FlushDataCache(NULL, top_framebuffer, 240*400*3);
}

void drawTitleScreen(char* str)
{
	clearScreen(0x00);
	centerString("HANS",0);
	renderString(str, 0, 40);
}

void resetConsole(void)
{
	console_buffer[0] = 0x00;
	drawTitleScreen(console_buffer);
}

void print_str(char* str)
{
	strcpy(&console_buffer[strlen(console_buffer)], str);
	drawTitleScreen(console_buffer);
}

void append_str(char* str)
{
	strcpy(&console_buffer[strlen(console_buffer)], str);
}

void refresh_screen()
{
	drawTitleScreen(console_buffer);
}

void print_hex(u32 val)
{
	char str[9];

	hex2str(str,val);
	print_str(str);
}

Result apply_map(memorymap_t* m, u8* code_data, u32 code_skip)
{
	if(!m)return -1;

	Result ret = 0;
	int i;
	for(i=0; i<m->num; i++)
	{
		int remaining_size = m->map[i].size;
		u32 offset = 0;

		if(!i)
		{
			// assume code_skip will always be less than the first map region
			// (it probably will tbh)
			offset = code_skip;
			remaining_size -= offset;
		}

		while(remaining_size > 0)
		{
			int size = remaining_size;
			if(size > 0x00080000)size = 0x00080000;

			ret = GSPGPU_FlushDataCache(NULL, (u8*)&code_data[m->map[i].src + offset], size);
			if(ret) return ret;
			svc_sleepThread(5*1000*1000);
			doGspwn((u32*)&code_data[m->map[i].src - 0x00100000 + offset], (u32*)(FIRM_APPMEMALLOC_LINEAR - m->processLinearOffset + m->map[i].dst + offset), size);

			remaining_size -= size;
			offset += size;
		}
	}

	return ret;
}

typedef struct {
    u32 base_addr;
    u32 size;
    u32 perm;
    u32 state;
} MemInfo;

typedef struct {
    u32 flags;
} PageInfo;

Result svc_queryMemory(MemInfo* info, PageInfo* out, u32 addr);

void* stub = NULL;

void setupStub(u8* code_data)
{
	// get .text info
	MemInfo minfo;
	PageInfo pinfo;
	Result ret = svc_queryMemory(&minfo, &pinfo, 0x00100000);

	print_str("text size "); print_hex((u32)minfo.size); print_str("\n");
	print_str("text perm "); print_hex((u32)minfo.perm); print_str("\n");

	memcpy(&code_data[minfo.size - HANS_STUB_OFFSET], stub_bin, stub_bin_size);

	stub = (void*)(0x00100000 + minfo.size - HANS_STUB_OFFSET);
}

Result invalidateIcache(Handle nssHandle)
{
	Result ret = NSS_LaunchTitle(&nssHandle, 0x0004013000002A02LL, 0x1);
	if(ret)return ret;

	svc_sleepThread(100*1000*1000);

	ret = NSS_TerminateProcessTID(&nssHandle, 0x0004013000002A02LL, 100*1000*1000);
	if(ret)return ret;

	print_str("invalidated icache\n");

	return 0;
}

extern Handle gspGpuHandle;

Result runStub(Handle nssHandle, memorymap_t* m, u8* code_data)
{
	u32 tmp;

	// flush code we're about to copy
	void* src_adr = (void*)(code_data);
	void* dst_adr = (void*)(FIRM_APPMEMALLOC_LINEAR - m->processLinearOffset);
	Result ret = GSPGPU_FlushDataCache(NULL, src_adr, HANS_LOADER_SIZE);

	if(ret)return ret;

	// need to load those now because we're about to unmap bss
	Handle local_gspGpuHandle = gspGpuHandle;
	Handle local_gspSharedMemHandle = gspSharedMemHandle;
	void (*local_stub)(Handle gsp, Handle nss, Handle gspMem) = stub;

	// free GSP heap
	svc_controlMemory(&tmp, (u32)gspHeap, 0x0, gspHeap_size, MEMOP_FREE, 0x0);

	// start copying code
	doGspwn(src_adr, dst_adr, HANS_LOADER_SIZE);

	// // free heap (includes bss !)
	// svc_controlMemory(&tmp, (u32)0x08000000, 0x0, _heap_size, MEMOP_FREE, 0x0);

	local_stub(local_gspGpuHandle, nssHandle, local_gspSharedMemHandle);

	return 0;
}

void _main(paramblk_t* p)
{
	gspHeap = (u8*)p->linear_heap;
	gspHeap_size = p->linear_size;

	initSrv();
	srv_RegisterClient(NULL);

	gspGpuInit();

	resetConsole();
	print_str("hello\n");
	print_str("_firm_appmemalloc "); print_hex((u32)_firm_appmemalloc); print_str("\n");
	print_str("code address "); print_hex((u32)p->code_data); print_str("\n");
	print_str("code size    "); print_hex(p->code_size); print_str("\n");

	setupStub((u8*)p->code_data);

	Result ret = apply_map(&p->map, (u8*)p->code_data, HANS_LOADER_SIZE);
	print_str("applied map  "); print_hex(ret); print_str("\n");

	ret = invalidateIcache(p->nssHandle);

	ret = runStub(p->nssHandle, &p->map, (u8*)p->code_data);
	print_hex(ret);

	while(1);
	gspGpuExit();
}
