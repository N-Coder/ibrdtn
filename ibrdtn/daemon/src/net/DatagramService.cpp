/*
 * DatagramService.cpp
 *
 *  Created on: 09.11.2012
 *      Author: morgenro
 */

#include <ibrcommon/Logger.h>
#include "net/DatagramService.h"
#include "net/DiscoveryService.h"

namespace dtn {
    namespace net {
        size_t DatagramService::FRAME_SEQNO_SHORT_MASK = 0b1111;
        size_t DatagramService::FRAME_SEQNO_SHORT_BITS = 4;
        size_t DatagramService::FRAME_SEQNO_SHORT_MAX = FRAME_SEQNO_SHORT_MASK;

        size_t DatagramService::FRAME_SEQNO_LONG_MASK = 0xFF;
        size_t DatagramService::FRAME_SEQNO_LONG_BITS = 8;
        size_t DatagramService::FRAME_SEQNO_LONG_MAX = FRAME_SEQNO_LONG_MASK;

        size_t DatagramService::FRAME_HEADER_SHORT_LENGTH = 2;
        size_t DatagramService::FRAME_HEADER_LONG_LENGTH = 2;

        DatagramService::~DatagramService() {}

        std::string DatagramService::getServiceTag() const {
            return dtn::net::DiscoveryService::asTag(getProtocol());
        }

        void DatagramService::read_header_short(const char *buf, DatagramService::FRAME_TYPE &type,
                                                DatagramService::FLAG_BITS &flags, unsigned int &seqno) {
            // SSSS FF TT
            char tmp = buf[0];
            type = (DatagramService::FRAME_TYPE) (tmp & DatagramService::FRAME_TYPE_MASK);
            tmp >>= DatagramService::FRAME_TYPE_BITS;
            flags = (DatagramService::FLAG_BITS) (tmp & DatagramService::FRAME_FLAGS_MASK);
            tmp >>= DatagramService::FRAME_FLAGS_BITS;
            seqno = tmp & DatagramService::FRAME_SEQNO_SHORT_MASK;
        }

        void DatagramService::write_header_short(char *buf, const DatagramService::FRAME_TYPE &type,
                                                 const DatagramService::FLAG_BITS &flags, const unsigned int &seqno) {
            // SSSS FF TT
            buf[0] = seqno & DatagramService::FRAME_SEQNO_SHORT_MASK;
            buf[0] <<= DatagramService::FRAME_FLAGS_BITS;
            buf[0] |= flags.get() & DatagramService::FRAME_FLAGS_MASK;
            buf[0] <<= DatagramService::FRAME_TYPE_BITS;
            buf[0] |= type & DatagramService::FRAME_TYPE_MASK;
        }

        void DatagramService::read_header_long(const char *buf, DatagramService::FRAME_TYPE &type,
                                               DatagramService::FLAG_BITS &flags, unsigned int &seqno) {
            // first byte is the flags (upper) and type (lower): 0000 FF TT
            char tmp = buf[0];
            type = (DatagramService::FRAME_TYPE) (tmp & DatagramService::FRAME_TYPE_MASK);
            tmp >>= DatagramService::FRAME_TYPE_BITS;
            flags = (DatagramService::FLAG_BITS) (tmp & DatagramService::FRAME_FLAGS_MASK);

            // second byte is seqno: SSSS SSSS
            seqno = buf[1] & DatagramService::FRAME_SEQNO_LONG_MASK;
        }

        void DatagramService::write_header_long(char *buf, const DatagramService::FRAME_TYPE &type,
                                                const DatagramService::FLAG_BITS &flags, const unsigned int &seqno) {
            // first byte is the flags (upper) and type (lower): 0000 FF TT
            buf[0] = flags.get() & DatagramService::FRAME_FLAGS_MASK;
            buf[0] <<= DatagramService::FRAME_TYPE_BITS;
            buf[0] |= type & DatagramService::FRAME_TYPE_MASK;

            // second byte is seqno: SSSS SSSS
            buf[1] = seqno & DatagramService::FRAME_SEQNO_LONG_MASK;
        }

        std::string DatagramService::packet_to_string(DatagramService::FRAME_TYPE type,
                                                      const DatagramService::FLAG_BITS &flags, unsigned int seqno,
                                                      const char *buf, size_t buf_len, const std::string &address) {
            std::stringstream ss;
            ss << "[type: " << SS_HEX(type);
            ss << "; flags: " << SS_HEX(flags.get());
            ss << "; seqno: " << std::dec << seqno;
            ss << "; length: " << std::dec << buf_len;
            ss << "; address: " << address;
            ss << "]\n";
            for (size_t i = 0; i < buf_len; i++) {
                if (i == 8 && buf_len > 16) {
                    ss << " .....";
                    i = buf_len - 8;
                }
                ss << " " << SS_HEX(buf[i]);
            }
            return ss.str();
        }
    }
}
