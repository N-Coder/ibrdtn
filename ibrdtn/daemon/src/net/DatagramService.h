/*
 * DatagramService.h
 *
 *  Created on: 09.11.2012
 *      Author: morgenro
 */

#ifndef DATAGRAMSERVICE_H_
#define DATAGRAMSERVICE_H_

#include "core/Node.h"
#include <ibrcommon/net/vinterface.h>
#include <string>

namespace dtn
{
	namespace net
	{
		class DatagramException : public ibrcommon::Exception
		{
		public:
		    DatagramException(const std::string &what) : ibrcommon::Exception(what) {};

            ~DatagramException() throw() override = default;
		};

		class DatagramService
		{
		public:
		    enum FRAME_TYPE {
                FRAME_BROADCAST = 0,
                FRAME_SEGMENT = 1,
                FRAME_ACK = 2,
                FRAME_NACK = 3,

                FRAME_TYPE_MASK = 0b11,
                FRAME_TYPE_BITS = 2
            };
            enum FRAME_FLAGS {
                SEGMENT_FIRST = 1,
                SEGMENT_LAST = 2,

                FRAME_FLAGS_MASK = 0b11,
                FRAME_FLAGS_BITS = 2
            };
            typedef dtn::data::Bitset<FRAME_FLAGS> FLAG_BITS;

            static size_t FRAME_SEQNO_SHORT_MASK;
            static size_t FRAME_SEQNO_SHORT_BITS;
            static size_t FRAME_SEQNO_SHORT_MAX;

            static size_t FRAME_SEQNO_LONG_MASK;
            static size_t FRAME_SEQNO_LONG_BITS;
            static size_t FRAME_SEQNO_LONG_MAX;

            static size_t FRAME_HEADER_SHORT_LENGTH;
            static size_t FRAME_HEADER_LONG_LENGTH;

            static void read_header_short(const char *buf, DatagramService::FRAME_TYPE &type,
                                          DatagramService::FLAG_BITS &flags, unsigned int &seqno) {
                // SSSS FF TT
                char tmp = buf[0];
                type = (DatagramService::FRAME_TYPE) (tmp & DatagramService::FRAME_TYPE_MASK);
                tmp >>= DatagramService::FRAME_TYPE_BITS;
                flags = (DatagramService::FLAG_BITS) (tmp & DatagramService::FRAME_FLAGS_MASK);
                tmp >>= DatagramService::FRAME_FLAGS_BITS;
                seqno = tmp & DatagramService::FRAME_SEQNO_SHORT_MASK;
            }

            static void write_header_short(char *buf, const DatagramService::FRAME_TYPE &type,
                                           const DatagramService::FLAG_BITS &flags, const unsigned int &seqno) {
                // SSSS FF TT
                buf[0] = seqno & DatagramService::FRAME_SEQNO_SHORT_MASK;
                buf[0] <<= DatagramService::FRAME_FLAGS_BITS;
                buf[0] |= flags.get() & DatagramService::FRAME_FLAGS_MASK;
                buf[0] <<= DatagramService::FRAME_TYPE_BITS;
                buf[0] |= type & DatagramService::FRAME_TYPE_MASK;
            }

            static void read_header_long(const char *buf, DatagramService::FRAME_TYPE &type,
                                         DatagramService::FLAG_BITS &flags, unsigned int &seqno) {
                // first byte is the flags (upper) and type (lower): 0000 FF TT
                char tmp = buf[0];
                type = (DatagramService::FRAME_TYPE) (tmp & DatagramService::FRAME_TYPE_MASK);
                tmp >>= DatagramService::FRAME_TYPE_BITS;
                flags = (DatagramService::FLAG_BITS) (tmp & DatagramService::FRAME_FLAGS_MASK);

                // second byte is seqno: SSSS SSSS
                seqno = buf[1] & DatagramService::FRAME_SEQNO_LONG_MASK;
            }

            static void write_header_long(char *buf, const DatagramService::FRAME_TYPE &type,
                                          const DatagramService::FLAG_BITS &flags, const unsigned int &seqno) {
                // first byte is the flags (upper) and type (lower): 0000 FF TT
                buf[0] = flags.get() & DatagramService::FRAME_FLAGS_MASK;
                buf[0] <<= DatagramService::FRAME_TYPE_BITS;
                buf[0] |= type & DatagramService::FRAME_TYPE_MASK;

                // second byte is seqno: SSSS SSSS
                buf[1] = seqno & DatagramService::FRAME_SEQNO_LONG_MASK;
            }
            
			class Parameter
			{
			public:
				// default constructor
				Parameter()
				: max_seq_numbers(2), max_msg_length(1024),
                  send_window_size(max_seq_numbers / 2), recv_window_size(1),
				  initial_timeout(200), retry_limit(5) { }

				// destructor
				virtual ~Parameter() { }

				unsigned int max_seq_numbers;
				unsigned int send_window_size;
				unsigned int recv_window_size;
				size_t max_msg_length;
				size_t initial_timeout;
				size_t retry_limit;
			};

			virtual ~DatagramService() = 0;

			/**
			 * Bind to the local socket.
			 * @throw If the bind fails, an DatagramException is thrown.
			 */
			virtual void bind() throw (DatagramException) = 0;

			/**
			 * Shutdown the socket. Unblock all calls on the socket (recv, send, etc.)
			 */
			virtual void shutdown() = 0;

			/**
			 * Send the payload as datagram to a defined destination
			 * @param address The destination address encoded as string.
			 * @param buf The buffer to send.
			 * @param length The number of available bytes in the buffer.
			 * @throw If the transmission wasn't successful this method will throw an exception.
			 */
			virtual void send(const FRAME_TYPE &type, const FLAG_BITS &flags, const unsigned int &seqno, const std::string &address, const char *buf, size_t length) throw (DatagramException) = 0;

			/**
			 * Send the payload as datagram to all neighbors (broadcast)
			 * @param buf The buffer to send.
			 * @param length The number of available bytes in the buffer.
			 * @throw If the transmission wasn't successful this method will throw an exception.
			 */
			virtual void send(const FRAME_TYPE &type, const FLAG_BITS &flags, const unsigned int &seqno, const char *buf, size_t length) throw (DatagramException) = 0;

			/**
			 * Receive an incoming datagram.
			 * @param buf A buffer to catch the incoming data.
			 * @param length The length of the buffer.
			 * @param address A buffer for the address of the sender.
			 * @throw If the receive call failed for any reason, an DatagramException is thrown.
			 * @return The number of received bytes.
			 */
			virtual size_t recvfrom(char *buf, size_t length, FRAME_TYPE &type, FLAG_BITS &flags, unsigned int &seqno, std::string &address) throw (DatagramException) = 0;

			/**
			 * Get the tag for this service used in discovery messages.
			 * @return The tag as string.
			 */
			virtual const std::string getServiceTag() const;

			/**
			 * Get the service description for this convergence layer. This
			 * data is used to contact this node.
			 * @return The service description as string.
			 */
			virtual const std::string getServiceDescription() const = 0;

			/**
			 * The used interface as vinterface object.
			 * @return A vinterface object.
			 */
			virtual const ibrcommon::vinterface& getInterface() const = 0;

			/**
			 * The protocol identifier for this type of service.
			 * @return
			 */
			virtual dtn::core::Node::Protocol getProtocol() const = 0;

			/**
			 * Returns the parameter for the connection.
			 * @return
			 */
			virtual const Parameter& getParameter() const = 0;
		};
	}
}

#endif /* DATAGRAMSERVICE_H_ */
