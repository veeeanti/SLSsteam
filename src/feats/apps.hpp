#pragma once

#include "../sdk/CAppOwnershipInfo.hpp"

#include <cstddef>
#include <cstdint>
#include <map>


namespace Apps
{
	extern bool applistRequested;
	extern std::map<uint32_t, int> appIdOwnerOverride;

	bool checkAppOwnership(uint32_t appId, CAppOwnershipInfo* info);
	void getSubscribedApps(uint32_t* appList, size_t size, uint32_t& count);
	void launchApp(uint32_t appId);

	bool shouldDisableCloud(uint32_t appId);
	bool shouldDisableCDKey(uint32_t appId);
	bool shouldDisableUpdates(uint32_t appId);
};
