#pragma once

#include "libmem/libmem.h"
#include <cstdint>
#include <string>


namespace Ticket
{
	class CTicketData
	{
	public:
		uint32_t steamId; //This one is redundant, but I'd rather track it myself then rely on Steam
		uint32_t appId;

		char ticket[0x400];
		uint32_t size;
		char extraData[4 * sizeof(uint32_t)];
	};

	extern uint32_t steamIdSpoof;

	//TODO: Fill with error checks
	std::string getTicketPath(uint32_t appId);
	CTicketData getCachedTicket(uint32_t appId);
	bool saveTicketToCache(uint32_t appId, uint32_t steamId, void* ticketData, uint32_t ticketSize, uint32_t* a4);

	uint32_t getTicketOwnershipExtendedData(uint32_t appId, void* ticket, uint32_t ticketSize, uint32_t* a4);//, uint32_t* a5, uint32_t* a6, uint32_t* a7);
}
