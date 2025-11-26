#include "CSteamEngine.hpp"

#include "../hooks.hpp"


void CSteamEngine::setAppIdForCurrentPipe(uint32_t appId)
{
	//Last argument needs to be 0, otherwise steam crashes.
	//Might be only 1 when steam first sets it, then 0
	Hooks::CSteamEngine_SetAppIdForCurrentPipe.tramp.fn(this, appId, 0);
}

CSteamEngine* g_pSteamEngine = nullptr;
