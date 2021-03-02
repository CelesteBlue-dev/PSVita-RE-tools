/*
 * PSVita RTC set
 * Copyright (C) 2019, CelesteBlue, zecoxao, Silica, Mathieulh
 */

#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/debug.h>
#include <psp2kern/kernel/rtc.h>
#include <psp2kern/sblacmgr.h>
#include <psp2kern/sblaimgr.h>
#include <taihen.h>

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

int checkSystemFw(void){

	int res;
	SceKernelFwInfo fw_data;
	memset(&fw_data, 0, sizeof(fw_data));
	fw_data.size = sizeof(fw_data);

	res = ksceKernelGetSystemSwVersion(&fw_data);
	if(res < 0)
		return res;

	if(((fw_data.version & ~0xFFF) - 0x3600000) != 0)
		return -1;

	return 0;
}

int printTickInfo(void){

	int res;
	uint64_t tick;

	tick = 0LL;
	res = ksceRtcGetCurrentTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcGetCurrentTick             : 0x%X 0x%llX\n", res, tick);

	tick = 0LL;
	res = ksceRtcGetCurrentNetworkTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcGetCurrentNetworkTick      : 0x%X 0x%llX\n", res, tick);

	tick = 0LL;
	res = ksceRtcGetCurrentSecureTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcGetCurrentSecureTick       : 0x%X 0x%llX\n", res, tick);

	tick = 0LL;
	res = ksceRtcGetCurrentDebugNetworkTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcGetCurrentDebugNetworkTick : 0x%X 0x%llX\n", res, tick);

	return 0;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args){

	int res;

	printTickInfo();

	SceUID inj_id_secure = -1, inj_id_network = -1;

	//   0xDCB731F0F1C000 (1970/01/01 00:00:00(UTC))
	// +    9676800000000 (diff 112 days Idk why)
	// = 0xDCBFFEFF2BC000 = 62135596800000000
	uint64_t unixtime, tick;

	// unixtime = 1355270400; // 2012/12/12 00:00:00(UTC)
	// unixtime = 1417219200; // 2014/11/29 00:00:00(UTC)
	unixtime = 1420070400; // 2015/01/01 00:00:00(UTC)

	tick = (unixtime * 1000 * 1000) + 0xDCBFFEFF2BC000;

	if(unixtime <= 1420070400){
		// TODO : Investigates offsets in other versions of SceRtc and supports anything other than 3.60.
		if(checkSystemFw() < 0)
			return SCE_KERNEL_START_NO_RESIDENT;

		SceUID SceRtc_modid = ksceKernelSearchModuleByName("SceRtc");

		uint64_t time_patch = 0xDCBFFEFF2BC000; // 1970/01/01 00:00:00(UTC)

		/*
		 * Patch to allows rollback until 1970/01/01 00:00:00(UTC)
		 */
		inj_id_secure  = taiInjectDataForKernel(0x10005, SceRtc_modid, 0, 0xA70, &time_patch, sizeof(time_patch));
		inj_id_network = taiInjectDataForKernel(0x10005, SceRtc_modid, 0, 0xBC8, &time_patch, sizeof(time_patch));
	}

	ksceDebugPrintf("tick : 0x%llX\n", tick);

	// Setting Network RTC for TestKits
	res = ksceRtcSetCurrentTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcSetCurrentTick             : 0x%X\n", res);

	res = ksceRtcSetCurrentNetworkTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcSetCurrentNetworkTick      : 0x%X\n", res);

	res = ksceRtcSetCurrentSecureTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcSetCurrentSecureTick       : 0x%X\n", res);

	res = ksceRtcSetCurrentDebugNetworkTick((SceUInt64 *)&tick);
	ksceDebugPrintf("sceRtcSetCurrentDebugNetworkTick : 0x%X\n", res);

	if(inj_id_network > 0)
		taiInjectReleaseForKernel(inj_id_network);

	if(inj_id_secure > 0)
		taiInjectReleaseForKernel(inj_id_secure);

	// Setting CP RTC for DevKits
	if(ksceSblAimgrIsTool() != 0 && ksceSblACMgrIsDevelopmentMode() != 0){
		int (* sceSblRtcMgrSetCpRtcForDriver)(SceUInt32 time);

		res = module_get_export_func(0x10005, "SceSblPostSsMgr", 0x2254E1B2, 0x3F9BDEDF, (uintptr_t *)&sceSblRtcMgrSetCpRtcForDriver);
		if(res < 0)
			return SCE_KERNEL_START_NO_RESIDENT;

		res = sceSblRtcMgrSetCpRtcForDriver(unixtime); // 01-01-2000: 0x386D4381;
		ksceDebugPrintf("sceSblRtcMgrSetCpRtc             : 0x%X\n", res);
	}

	printTickInfo();

	return SCE_KERNEL_START_NO_RESIDENT;
}
