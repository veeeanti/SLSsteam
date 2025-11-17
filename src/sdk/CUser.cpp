#include "CUser.hpp"

#include "../patterns.hpp"

CUser* g_pUser = nullptr;

void CUser::updateAppOwnershipTicket(uint32_t appId, void* pTicket, uint32_t len)
{
	const static auto fn = reinterpret_cast<void(*)(void*, uint32_t, void*, uint32_t)>(Patterns::CUser::UpdateAppOwnershipTicket.address);
	fn(this, appId, pTicket, len);
}
