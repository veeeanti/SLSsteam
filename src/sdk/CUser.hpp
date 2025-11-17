#pragma once

#include <cstdint>

class CUser
{
public:
	void updateAppOwnershipTicket(uint32_t appId, void* pTicket, uint32_t len);
};

extern CUser* g_pUser;
