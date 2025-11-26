#pragma once

#include <cstdint>


class CSteamEngine
{
public:
	void setAppIdForCurrentPipe(uint32_t appId);
};

extern CSteamEngine* g_pSteamEngine;
