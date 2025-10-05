#include "config.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "log.hpp"
#include "memhlp.hpp"
#include "patterns.hpp"
#include "sdk/IClientUser.hpp"
#include "vftableinfo.hpp"

#include "libmem/libmem.h"

#include "sdk/CAppOwnershipInfo.hpp"
#include "sdk/IClientApps.hpp"
#include "sdk/IClientAppManager.hpp"

#include "feats/apps.hpp"
#include "feats/dlc.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#include <vector>

template<typename T>
Hook<T>::Hook(const char* name)
{
	this->name = std::string(name);
}

template<typename T>
DetourHook<T>::DetourHook(const char* name) : Hook<T>::Hook(name)
{
	this->size = 0;
}

template<typename T>
VFTHook<T>::VFTHook(const char* name) : Hook<T>::Hook(name)
{
	this->hooked = false;
}

template<typename T>
bool DetourHook<T>::setup(const char* pattern, const MemHlp::SigFollowMode followMode, lm_byte_t* extraData, lm_size_t extraDataSize, T hookFn)
{
	//Hardcoding g_modSteamClient here is definitely bad design, but we can easily change that
	//in case we ever need to
	lm_address_t oFn = MemHlp::searchSignature(this->name.c_str(), pattern, g_modSteamClient, followMode, extraData, extraDataSize);
	if (oFn == LM_ADDRESS_BAD)
	{
		return false;
	}

	this->originalFn.address = oFn;
	this->hookFn.fn = hookFn;

	return true;
}

template<typename T>
bool DetourHook<T>::setup(const char* pattern, const MemHlp::SigFollowMode followMode, T hookFn)
{
	return setup(pattern, followMode, nullptr, 0, hookFn);
}

template<typename T>
void DetourHook<T>::place()
{
	this->size = LM_HookCode(this->originalFn.address, this->hookFn.address, &this->tramp.address);
	MemHlp::fixPICThunkCall(this->name.c_str(), this->originalFn.address, this->tramp.address);

	g_pLog->debug
	(
		"Detour hooked %s (%p) with hook at %p and tramp at %p\n",
		this->name.c_str(),
		this->originalFn.address,
		this->hookFn.address,
		this->tramp.address
	);
}

template<typename T>
void DetourHook<T>::remove()
{
	if (!this->size)
	{
		return;
	}

	LM_UnhookCode(this->originalFn.address, this->tramp.address, this->size);
	this->size = 0;

	g_pLog->debug("Unhooked %s\n", this->name.c_str());
}

template<typename T>
void VFTHook<T>::place()
{
	LM_VmtHook(this->vft.get(), this->index, this->hookFn.address);
	this->hooked = true;

	g_pLog->debug
	(
		"VFT hooked %s (%p) with hook at %p\n",
		this->name.c_str(),
		this->originalFn.address,
		this->hookFn.address
	);
}

template<typename T>
void VFTHook<T>::remove()
{
	//No clue how libmem reacts when unhooking a non existent hook
	//so we do this
	if (!this->hooked)
	{
		return;
	}

	LM_VmtUnhook(this->vft.get(), this->index);
	this->hooked = false;

	g_pLog->debug("Unhooked %s!\n", this->name.c_str());
}

template<typename T>
void VFTHook<T>::setup(std::shared_ptr<lm_vmt_t> vft, unsigned int index, T hookFn)
{
	this->vft = vft;
	this->index = index;

	this->originalFn.address = LM_VmtGetOriginal(this->vft.get(), this->index);
	this->hookFn.fn = hookFn;
}

__attribute__((hot))
static void hkLogSteamPipeCall(const char* iface, const char* fn)
{
	Hooks::LogSteamPipeCall.tramp.fn(iface, fn);

	if (g_config.extendedLogging)
	{
		g_pLog->debug("LogSteamPipeCall(%s, %s)\n", iface, fn);
	}
}

static uint32_t hkCAPIJob_RequestUserStats(void* a0)
{
	const uint32_t ret = Hooks::CAPIJob_RequestUserStats.tramp.fn(a0);
	g_pLog->once
	(
		"%s(%p) -> %u\n",

		Hooks::CAPIJob_RequestUserStats.name.c_str(),
		a0,
		ret
	);

	switch (ret)
	{
		//1 = Success
		case 1:
			return ret;

		//2 = Failed
		//3 = No Connection
		default:
			return 3;
	}
}

static void* hkClientAppManager_LaunchApp(void* pClientAppManager, uint32_t* pAppId, void* a2, void* a3, void* a4)
{
	if (pAppId)
	{
		g_pLog->once
		(
			"%s(%p, %u, %p, %p, %p)\n",

			Hooks::IClientAppManager_LaunchApp.name.c_str(),
			pClientAppManager,
			*pAppId,
			a2,
			a3,
			a4
		);
		Apps::launchApp(*pAppId);
	}

	//Do not do anything in post! Otherwise App launching will break
	return Hooks::IClientAppManager_LaunchApp.originalFn.fn(pClientAppManager, pAppId, a2, a3, a4);
}

static bool hkClientAppManager_IsAppDlcInstalled(void* pClientAppManager, uint32_t appId, uint32_t dlcId)
{
	const bool ret = Hooks::IClientAppManager_IsAppDlcInstalled.originalFn.fn(pClientAppManager, appId, dlcId);
	g_pLog->once
	(
		"%s(%p, %u, %u) -> %i\n",

		Hooks::IClientAppManager_IsAppDlcInstalled.name.c_str(),
		pClientAppManager,
		appId,
		dlcId,
		ret
	);

	if (DLC::isAppDlcInstalled(appId, dlcId))
	{
		return true;
	}

	return ret;
}

static bool hkClientAppManager_BIsDlcEnabled(void* pClientAppManager, uint32_t appId, uint32_t dlcId, void* a3)
{
	const bool ret = Hooks::IClientAppManager_BIsDlcEnabled.originalFn.fn(pClientAppManager, appId, dlcId, a3);
	g_pLog->once
	(
		"%s(%p, %u, %u, %p) -> %i\n",

		Hooks::IClientAppManager_BIsDlcEnabled.name.c_str(),
		pClientAppManager,
		appId,
		dlcId,
		a3,
		ret
	);

	
	if (DLC::isDlcEnabled(appId))
	{
		return true;
	}

	return ret;
}

static bool hkClientAppManager_GetUpdateInfo(void* pClientAppManager, uint32_t appId, uint32_t* a2)
{
	const bool success = Hooks::IClientAppManager_GetAppUpdateInfo.originalFn.fn(pClientAppManager, appId, a2);
	g_pLog->once("IClientAppManager::GetUpdateInfo(%p, %u, %p) -> %i\n", pClientAppManager, appId, a2, success);

	if (Apps::shouldDisableUpdates(appId))
	{
		g_pLog->once("Disabled updates for %u\n", appId);
		return false;
	}

	return success;
}

__attribute__((hot))
static void hkClientAppManager_PipeLoop(void* pClientAppManager, void* a1, void* a2, void* a3)
{
	g_pClientAppManager = reinterpret_cast<IClientAppManager*>(pClientAppManager);

	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientAppManager), vft.get());

	Hooks::IClientAppManager_BIsDlcEnabled.setup(vft, VFTIndexes::IClientAppManager::BIsDlcEnabled, hkClientAppManager_BIsDlcEnabled);
	Hooks::IClientAppManager_GetAppUpdateInfo.setup(vft, VFTIndexes::IClientAppManager::GetUpdateInfo, hkClientAppManager_GetUpdateInfo);
	Hooks::IClientAppManager_LaunchApp.setup(vft, VFTIndexes::IClientAppManager::LaunchApp, hkClientAppManager_LaunchApp);
	Hooks::IClientAppManager_IsAppDlcInstalled.setup(vft, VFTIndexes::IClientAppManager::IsAppDlcInstalled, hkClientAppManager_IsAppDlcInstalled);

	Hooks::IClientAppManager_BIsDlcEnabled.place();
	Hooks::IClientAppManager_GetAppUpdateInfo.place();
	Hooks::IClientAppManager_LaunchApp.place();
	Hooks::IClientAppManager_IsAppDlcInstalled.place();

	g_pLog->debug("IClientAppManager->vft at %p\n", vft->vtable);

	Hooks::IClientAppManager_PipeLoop.remove();
	Hooks::IClientAppManager_PipeLoop.originalFn.fn(pClientAppManager, a1, a2, a3);
}

static unsigned int hkClientApps_GetDLCCount(void* pClientApps, uint32_t appId)
{
	uint32_t count = Hooks::IClientApps_GetDLCCount.originalFn.fn(pClientApps, appId);
	g_pLog->once
	(
		"%s(%p, %u) -> %u\n",

		Hooks::IClientApps_GetDLCCount.name.c_str(),
		pClientApps,
		appId,
		count
	);

	const uint32_t override = DLC::getDlcCount(appId);
	if (override)
	{
		return override;
	}

	return count;
}

static bool hkClientApps_GetDLCDataByIndex(void* pClientApps, uint32_t appId, int dlcIndex, uint32_t* pDlcId, bool* pIsAvailable, char* pChDlcName, size_t dlcNameLen)
{
	//Preserve original call to populate stuff
	bool ret = DLC::getDlcDataByIndex(appId, dlcIndex, pDlcId, pIsAvailable, pChDlcName, dlcNameLen)
		|| Hooks::IClientApps_GetDLCDataByIndex.originalFn.fn(pClientApps, appId, dlcIndex, pDlcId, pIsAvailable, pChDlcName, dlcNameLen);


	g_pLog->once
	(
		"%s(%p, %u, %i, %p, %p, %s, %i) -> %i\n",

		Hooks::IClientApps_GetDLCDataByIndex.name.c_str(),
		pClientApps,
		appId,
		dlcIndex,
		pDlcId,
		pIsAvailable,
		pChDlcName,
		dlcNameLen,
		ret
	);

	return ret;
}

__attribute__((hot))
static void hkClientApps_PipeLoop(void* pClientApps, void* a1, void* a2, void* a3)
{
	g_pClientApps = reinterpret_cast<IClientApps*>(pClientApps);

	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientApps), vft.get());

	Hooks::IClientApps_GetDLCDataByIndex.setup(vft, VFTIndexes::IClientApps::GetDLCDataByIndex, hkClientApps_GetDLCDataByIndex);
	Hooks::IClientApps_GetDLCCount.setup(vft, VFTIndexes::IClientApps::GetDLCCount, hkClientApps_GetDLCCount);

	Hooks::IClientApps_GetDLCDataByIndex.place();
	Hooks::IClientApps_GetDLCCount.place();

	g_pLog->debug("IClientApps->vft at %p\n", vft->vtable);

	Hooks::IClientApps_PipeLoop.remove();
	Hooks::IClientApps_PipeLoop.originalFn.fn(pClientApps, a1, a2, a3);
}

static bool hkClientRemoteStorage_IsCloudEnabledForApp(void* pClientRemoteStorage, uint32_t appId)
{
	const bool enabled = Hooks::IClientRemoteStorage_IsCloudEnabledForApp.originalFn.fn(pClientRemoteStorage, appId);
	g_pLog->once
	(
		"%s(%p, %u) -> %i\n",

		Hooks::IClientRemoteStorage_IsCloudEnabledForApp.name.c_str(),
		pClientRemoteStorage,
		appId,
		enabled
	);

	if (Apps::shouldDisableCloud(appId))
	{
		g_pLog->once("Disabled cloud for %u\n", appId);
		return false;
	}

	return enabled;
}

static void hkClientRemoteStorage_PipeLoop(void* pClientRemoteStorage, void* a1, void* a2, void* a3)
{
	std::shared_ptr<lm_vmt_t> vft = std::make_shared<lm_vmt_t>();
	LM_VmtNew(*reinterpret_cast<lm_address_t**>(pClientRemoteStorage), vft.get());

	Hooks::IClientRemoteStorage_IsCloudEnabledForApp.setup(vft, VFTIndexes::IClientRemoteStorage::IsCloudEnabledForApp, hkClientRemoteStorage_IsCloudEnabledForApp);
	Hooks::IClientRemoteStorage_IsCloudEnabledForApp.place();

	g_pLog->debug("IClientRemoteStorage->vft at %p\n", vft->vtable);

	Hooks::IClientRemoteStorage_PipeLoop.remove();
	Hooks::IClientRemoteStorage_PipeLoop.originalFn.fn(pClientRemoteStorage, a1, a2, a3);
}

static bool hkClientUser_BIsSubscribedApp(void* pClientUser, uint32_t appId)
{
	const bool ret = Hooks::IClientUser_BIsSubscribedApp.tramp.fn(pClientUser, appId);
	g_pLog->once
	(
		"%s(%p, %u) -> %i\n",

		Hooks::IClientUser_BIsSubscribedApp.name.c_str(),
		pClientUser,
		appId,
		ret
	);

	if (DLC::isSubscribed(appId))
	{
		return true;
	}

	return ret;
}

__attribute__((hot))
static bool hkClientUser_CheckAppOwnership(void* pClientUser, uint32_t appId, CAppOwnershipInfo* pOwnershipInfo)
{
	const bool ret = Hooks::IClientUser_CheckAppOwnership.tramp.fn(pClientUser, appId, pOwnershipInfo);

	//Do not log pOwnershipInfo because it gets deleted very quickly, so it's pretty much useless in the logs
	g_pLog->once
	(
		"%s(%p, %u) -> %i\n",

		Hooks::IClientUser_CheckAppOwnership.name.c_str(),
		pClientUser,
		appId,
		ret
	);

	if (Apps::checkAppOwnership(appId, pOwnershipInfo))
	{
		return true;
	}

	return ret;
}

static uint8_t hkClientUser_IsUserSubscribedAppInTicket(void* pClientUser, uint32_t steamId, uint32_t a2, uint32_t a3, uint32_t appId)
{
	const uint8_t ticketState = Hooks::IClientUser_IsUserSubscribedAppInTicket.tramp.fn(pClientUser, steamId, a2, a3, appId);
	//g_pLog->once("IClientUser::IsUserSubscribedAppInTicket(%p, %u, %u, %u, %u) -> %i\n", pClientUser, steamId, a2, a3, appId, ticketState);
	//Don't log the steamId, protect users from themselves and stuff
	g_pLog->once
	(
		"%s(%p, %u, %u, %u) -> %i\n",

		Hooks::IClientUser_IsUserSubscribedAppInTicket.name.c_str(),
		pClientUser,
		a2,
		a3,
		appId,
		ticketState
	);
	
	if (DLC::userSubscribedInTicket(appId))
	{
		//Owned and subscribed hehe :)
		return 0;
	}

	return ticketState;
}

static uint32_t hkClientUser_GetSubscribedApps(void* pClientUser, uint32_t* pAppList, size_t size, bool a3)
{
	uint32_t count = Hooks::IClientUser_GetSubscribedApps.tramp.fn(pClientUser, pAppList, size, a3);
	g_pLog->once
	(
		"%s(%p, %p, %i, %i) -> %i\n",

		Hooks::IClientUser_GetSubscribedApps.name.c_str(),
		pClientUser,
		pAppList,
		size,
		a3,
		count
	);

	Apps::getSubscribedApps(pAppList, size, count);

	return count;
}

static bool hkClientUser_RequiresLegacyCDKey(void* pClientUser, uint32_t appId, uint32_t* a2)
{
	const bool requiresKey = Hooks::IClientUser_RequiresLegacyCDKey.tramp.fn(pClientUser, appId, a2);
	g_pLog->once
	(
		"%s(%p, %u, %u) -> %i\n",

		Hooks::IClientUser_RequiresLegacyCDKey.name.c_str(),
		pClientUser,
		appId,
		a2,
		requiresKey
	);

	if (Apps::shouldDisableCDKey(appId))
	{
		g_pLog->once("Disable CD Key for %u\n", appId);
		return false;
	}

	return requiresKey;
}

static void patchRetn(lm_address_t address)
{
	constexpr lm_byte_t retn = 0xC3;

	lm_prot_t oldProt;
	LM_ProtMemory(address, 1, LM_PROT_XRW, &oldProt); //LM_PROT_W Should be enough, but just in case something tries to execute it inbetween us setting the prot and writing to it
	LM_WriteMemory(address, &retn, 1);
	LM_ProtMemory(address, 1, oldProt, LM_NULL);
}

static lm_address_t hkGetSteamId;
static bool createAndPlaceSteamIdHook()
{
	hkGetSteamId = LM_AllocMemory(0, LM_PROT_XRW);
	if (hkGetSteamId == LM_ADDRESS_BAD)
	{
		g_pLog->debug("Failed to allocate memory for GetSteamId!\n");
		return false;
	}

	g_pLog->debug("Allocated memory for GetSteamId hook at %p\n", hkGetSteamId);

	auto insts = std::vector<lm_inst_t>();
	lm_address_t readAddr = Hooks::IClientUser_GetSteamId;
	for(;;)
	{
		lm_inst_t inst;
		if (!LM_Disassemble(readAddr, &inst))
		{
			g_pLog->debug("Failed to disassemble function at %p!\n", readAddr);
			return false;
		}

		insts.emplace_back(inst);
		readAddr = inst.address + inst.size;

		if (strcmp(inst.mnemonic, "ret") == 0)
		{
			break;
		}
	}

	const unsigned int retIdx = insts.size() - 1;

	g_pLog->debug("Ret is instruction number %u\n", retIdx);
	//TODO: Create InlineHook class for this
	size_t totalBytes = 0;
	unsigned int instsToOverwrite = 0;
	for(int i = retIdx; i >= 0; i--)
	{
		lm_inst_t inst = insts.at(i);
		totalBytes += inst.size;
		instsToOverwrite++;

		//Need only 5 bytes to place relative jmp
		if (totalBytes >= 5)
		{
			break;
		}
	}

	lm_address_t writeAddr = hkGetSteamId;
	//TODO: Dynamically resolve register which holds SteamId
	MemHlp::assembleCodeAt(writeAddr, "mov [%p], ecx", &g_currentSteamId);

	//Write the overwritten instructions after our hook code
	for (unsigned int i = 0; i < instsToOverwrite; i++)
	{
		lm_inst_t inst = insts.at(insts.size() - instsToOverwrite + i);
		memcpy(reinterpret_cast<void*>(writeAddr), inst.bytes, inst.size);

		writeAddr += inst.size;
		g_pLog->debug("Copied %s %s to tramp\n", inst.mnemonic, inst.op_str);
	}

	lm_address_t jmpAddr = insts.at(insts.size() - instsToOverwrite).address;
	g_pLog->debug("Placing jmp at %p\n", jmpAddr);

	//Might be worth to convert to LM_AssembleEx, but whatever
	lm_prot_t oldProt;
	LM_ProtMemory(jmpAddr, 5, LM_PROT_XRW, &oldProt);
	*reinterpret_cast<lm_byte_t*>(jmpAddr) = 0xE9;
	*reinterpret_cast<lm_address_t*>(jmpAddr + 1) = hkGetSteamId - jmpAddr - 5;
	LM_ProtMemory(jmpAddr, 5, oldProt, nullptr);

	return true;
}

namespace Hooks
{
	//TODO: Replace logging in hooks with Hook::name
	DetourHook<LogSteamPipeCall_t> LogSteamPipeCall("LogSteamPipeCall");

	DetourHook<IClientAppManager_PipeLoop_t> IClientAppManager_PipeLoop("IClientAppManager::PipeLoop");
	DetourHook<IClientApps_PipeLoop_t> IClientApps_PipeLoop("IClientApps::PipeLoop");
	DetourHook<IClientRemoteStorage_PipeLoop_t> IClientRemoteStorage_PipeLoop("IClientRemoteStorage::PipeLoop");

	DetourHook<CAPIJob_RequestUserStats_t> CAPIJob_RequestUserStats("CAPIJob_RequestUserStats");

	DetourHook<IClientUser_BIsSubscribedApp_t> IClientUser_BIsSubscribedApp("IClientUser::BIsSubscribedApp");
	DetourHook<IClientUser_CheckAppOwnership_t> IClientUser_CheckAppOwnership("IClientUser::CheckAppOwnership");
	DetourHook<IClientUser_IsUserSubscribedAppInTicket_t> IClientUser_IsUserSubscribedAppInTicket("IClientUser::IsUserSubscribedAppInTicket");
	DetourHook<IClientUser_GetSubscribedApps_t> IClientUser_GetSubscribedApps("IClientUser::GetSubscribedApps");
	DetourHook<IClientUser_RequiresLegacyCDKey_t> IClientUser_RequiresLegacyCDKey("IClientUser::RequiresLegacyCDKey");

	VFTHook<IClientAppManager_BIsDlcEnabled_t> IClientAppManager_BIsDlcEnabled("IClientAppManager::BIsDlcEnabled");
	VFTHook<IClientAppManager_GetAppUpdateInfo_t> IClientAppManager_GetAppUpdateInfo("IClientAppManager::GetAppUpdateInfo");
	VFTHook<IClientAppManager_LaunchApp_t> IClientAppManager_LaunchApp("IClientAppManager::LaunchApp");
	VFTHook<IClientAppManager_IsAppDlcInstalled_t> IClientAppManager_IsAppDlcInstalled("IClientAppManager::IsAppDlcInstalled");

	VFTHook<IClientApps_GetDLCDataByIndex_t> IClientApps_GetDLCDataByIndex("IClientApps::GetDLCDataByIndex");
	VFTHook<IClientApps_GetDLCCount_t> IClientApps_GetDLCCount("IClientApps::GetDLCCount");

	VFTHook<IClientRemoteStorage_IsCloudEnabledForApp_t> IClientRemoteStorage_IsCloudEnabledForApp("IClientRemoteStorage::IsCloudEnabledForApp");

	lm_address_t IClientUser_GetSteamId;
}

bool Hooks::setup()
{
	g_pLog->debug("Hooks::setup()\n");

	IClientUser_GetSteamId = MemHlp::searchSignature("IClientUser::GetSteamId", Patterns::GetSteamId, g_modSteamClient, MemHlp::SigFollowMode::Relative);

	lm_address_t runningApp = MemHlp::searchSignature("RunningApp", Patterns::FamilyGroupRunningApp, g_modSteamClient, MemHlp::SigFollowMode::Relative);

	auto prologue = std::vector<lm_byte_t>({
		0x56, 0x57, 0xe5, 0x89, 0x55
	});
	lm_address_t stopPlayingBorrowedApp = MemHlp::searchSignature
	(
		"StopPlayingBorrowedApp",
		Patterns::StopPlayingBorrowedApp,
		g_modSteamClient,
		MemHlp::SigFollowMode::PrologueUpwards,
		&prologue[0],
		prologue.size()
	);

	//TODO: Automate these
	bool clientApps_PipeLoop = IClientApps_PipeLoop.setup
	(
		Patterns::IClientApps_PipeLoop,
		MemHlp::SigFollowMode::PrologueUpwards,
		&prologue[0],
		prologue.size(),
		&hkClientApps_PipeLoop
	);

	bool clientAppManager_PipeLoop = IClientAppManager_PipeLoop.setup
	(
		Patterns::IClientAppManager_PipeLoop,
		MemHlp::SigFollowMode::PrologueUpwards,
		&prologue[0],
		prologue.size(),
		&hkClientAppManager_PipeLoop
	);

	bool clientRemoteStorage_PipeLoop = IClientRemoteStorage_PipeLoop.setup
	(
		Patterns::IClientRemoteStorage_PipeLoop,
		MemHlp::SigFollowMode::PrologueUpwards,
		&prologue[0],
		prologue.size(),
		&hkClientRemoteStorage_PipeLoop
	);

	//TODO: Make this shit less verbose in case I fail my reversing & refactor for all this crap
	prologue = std::vector<lm_byte_t>({
		0x53, 0x56, 0x57, 0x55
	});
	bool requiresLegacyCDKey = IClientUser_RequiresLegacyCDKey.setup
	(
		Patterns::RequiresLegacyCDKey,
		MemHlp::SigFollowMode::PrologueUpwards,
		&prologue[0],
		prologue.size(),
		&hkClientUser_RequiresLegacyCDKey
	);

	bool succeeded =
		LogSteamPipeCall.setup(Patterns::LogSteamPipeCall, MemHlp::SigFollowMode::Relative, &hkLogSteamPipeCall)
		&& CAPIJob_RequestUserStats.setup(Patterns::CAPIJob_RequestUserStats, MemHlp::SigFollowMode::Relative, &hkCAPIJob_RequestUserStats)
		&& IClientUser_BIsSubscribedApp.setup(Patterns::IsSubscribedApp, MemHlp::SigFollowMode::Relative, &hkClientUser_BIsSubscribedApp)
		&& IClientUser_CheckAppOwnership.setup(Patterns::CheckAppOwnership, MemHlp::SigFollowMode::Relative, &hkClientUser_CheckAppOwnership)
		&& IClientUser_IsUserSubscribedAppInTicket.setup(Patterns::IsUserSubscribedAppInTicket, MemHlp::SigFollowMode::Relative, &hkClientUser_IsUserSubscribedAppInTicket)
		&& IClientUser_GetSubscribedApps.setup(Patterns::GetSubscribedApps, MemHlp::SigFollowMode::Relative, &hkClientUser_GetSubscribedApps)

		&& runningApp != LM_ADDRESS_BAD
		&& stopPlayingBorrowedApp != LM_ADDRESS_BAD
		&& IClientUser_GetSteamId != LM_ADDRESS_BAD

		&& clientApps_PipeLoop
		&& clientAppManager_PipeLoop
		&& clientRemoteStorage_PipeLoop
		&& requiresLegacyCDKey;

	if (!succeeded)
	{
		g_pLog->warn("Failed to find all patterns! Aborting...");
		return false;
	}

	//TODO: Elegantly move into Hooks::place()
	if (g_config.disableFamilyLock)
	{
		patchRetn(runningApp);
		patchRetn(stopPlayingBorrowedApp);
	}

	//Might move this into main()
	Hooks::place();
	return true;
}

void Hooks::place()
{
	//Detours
	LogSteamPipeCall.place();
	CAPIJob_RequestUserStats.place();

	IClientApps_PipeLoop.place();
	IClientAppManager_PipeLoop.place();
	IClientRemoteStorage_PipeLoop.place();

	IClientUser_BIsSubscribedApp.place();
	IClientUser_CheckAppOwnership.place();
	IClientUser_IsUserSubscribedAppInTicket.place();
	IClientUser_GetSubscribedApps.place();
	IClientUser_RequiresLegacyCDKey.place();

	createAndPlaceSteamIdHook();
}

void Hooks::remove()
{
	//Detours
	LogSteamPipeCall.remove();
	CAPIJob_RequestUserStats.remove();

	IClientApps_PipeLoop.remove();
	IClientAppManager_PipeLoop.remove();
	IClientRemoteStorage_PipeLoop.remove();

	IClientUser_BIsSubscribedApp.remove();
	IClientUser_CheckAppOwnership.remove();
	IClientUser_IsUserSubscribedAppInTicket.remove();
	IClientUser_GetSubscribedApps.remove();
	IClientUser_RequiresLegacyCDKey.remove();

	//VFT Hooks
	IClientAppManager_BIsDlcEnabled.remove();
	IClientAppManager_GetAppUpdateInfo.remove();
	IClientAppManager_LaunchApp.remove();
	IClientAppManager_IsAppDlcInstalled.remove();

	IClientApps_GetDLCDataByIndex.remove();
	IClientApps_GetDLCCount.remove();

	IClientRemoteStorage_IsCloudEnabledForApp.remove();
	
	//TODO: Remove jmp
	if (hkGetSteamId != LM_ADDRESS_BAD)
	{
		LM_FreeMemory(hkGetSteamId, 0);
	}
}
