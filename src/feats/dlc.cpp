#include "dlc.hpp"

#include "../sdk/IClientAppManager.hpp"

#include "../config.hpp"


bool DLC::isDlcEnabled(uint32_t appId)
{
	//TODO: Add check for legit ownership to allow toggle on/off
	return !g_config.shouldExcludeAppId(appId);
}

bool DLC::isSubscribed(uint32_t appId)
{
	return !g_config.shouldExcludeAppId(appId);
}

bool DLC::isAppDlcInstalled(uint32_t appId, uint32_t dlcId)
{
	//Do not pretend things are installed while downloading Apps, otherwise downloads will break for some of them
	auto state = g_pClientAppManager->getAppInstallState(appId);
	if (state & APPSTATE_DOWNLOADING || state & APPSTATE_INSTALLING)
	{
		g_pLog->once("Skipping DlcId %u because AppId %u has AppState %i\n", dlcId, appId, state);
		return false;
	}

	if (g_config.shouldExcludeAppId(dlcId))
	{
		return false;
	}

	return true;
}

bool DLC::userSubscribedInTicket(uint32_t appId)
{
	//Might want to compare the steamId param to the g_currentSteamId in the future
	//Although not doing that might also work for Dedicated servers?
	return !g_config.shouldExcludeAppId(appId);
}

uint32_t DLC::getDlcCount(uint32_t appId)
{
	if (g_config.dlcData.contains(appId))
	{
		return g_config.dlcData[appId].dlcIds.size();
	}

	return 0;
}

bool DLC::getDlcDataByIndex(uint32_t appId, int index, uint32_t* dlcId, bool* available, char* dlcName, size_t& dlcNameLen)
{
	if (!dlcId || !available || !dlcName)
	{
		return false;
	}

	if (!g_config.shouldExcludeAppId(*dlcId))
	{
		*available = true;
	}

	if (g_config.dlcData.contains(appId))
	{
		auto& data = g_config.dlcData[appId];
		auto dlc = std::next(data.dlcIds.begin(), index);

		*dlcId = dlc->first;

		//No clue if we have to check for errors during printf since the devs hopefully didn't fuck
		//up the dlcNameLen. Who knows though
		snprintf(dlcName, dlcNameLen, "%s", dlc->second.c_str());

		return true;
	}

	return false;
}
