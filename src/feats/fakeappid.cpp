#include "fakeappid.hpp"

#include "../config.hpp"

#include "../sdk/CUser.hpp"


void FakeAppIds::setAppIdForCurrentPipe(uint32_t& appId)
{
	//Do not change Steam Client itself (AppId 0)
	if (!appId)
	{
		return;
	}

	if (g_config.fakeAppIds.contains(appId))
	{
		g_pLog->once("Changing AppId of %u\n", appId);
		appId = g_config.fakeAppIds[appId];
	}
	else if (g_config.fakeAppIds.contains(0) && !g_pUser->checkAppOwnership(appId))
	{
		g_pLog->once("Changing AppId of %u to default override\n", appId);
		appId = g_config.fakeAppIds[0];
	}
}
