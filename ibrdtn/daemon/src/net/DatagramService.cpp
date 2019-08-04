/*
 * DatagramService.cpp
 *
 *  Created on: 09.11.2012
 *      Author: morgenro
 */

#include "net/DatagramService.h"
#include "net/DiscoveryService.h"

namespace dtn
{
	namespace net
	{
        size_t DatagramService::FRAME_SEQNO_SHORT_MASK = 0b1111;
        size_t DatagramService::FRAME_SEQNO_SHORT_BITS = 4;
        size_t DatagramService::FRAME_SEQNO_SHORT_MAX = FRAME_SEQNO_SHORT_MASK;

        size_t DatagramService::FRAME_SEQNO_LONG_MASK = 0xFF;
        size_t DatagramService::FRAME_SEQNO_LONG_BITS = 8;
        size_t DatagramService::FRAME_SEQNO_LONG_MAX = FRAME_SEQNO_LONG_MASK;

        size_t DatagramService::FRAME_HEADER_SHORT_LENGTH = 2;
        size_t DatagramService::FRAME_HEADER_LONG_LENGTH = 2;

		DatagramService::~DatagramService()
		{
		}

		const std::string DatagramService::getServiceTag() const
		{
			return dtn::net::DiscoveryService::asTag(getProtocol());
		}
	}
}
