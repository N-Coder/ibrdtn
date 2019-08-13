#ifndef UnixSockDatagramService_H_
#define UnixSockDatagramService_H_

#include "net/DatagramConvergenceLayer.h"
#include "net/DatagramService.h"
#include <ibrcommon/net/vsocket.h>
#include <ibrcommon/net/vinterface.h>
#include <ibrcommon/net/socketstream.h>

namespace dtn
{
	namespace net
	{
		class UnixSockDatagramService : public dtn::net::DatagramService
		{
		public:
			UnixSockDatagramService(const std::string &path, size_t mtu = 1280);
			virtual ~UnixSockDatagramService();

			/**
			 * Bind to the local socket.
			 * @throw If the bind fails, an DatagramException is thrown.
			 */
			virtual void bind() throw (DatagramException);

			/**
			 * Shutdown the socket. Unblock all calls on the socket (recv, send, etc.)
			 */
			virtual void shutdown();

			/**
			 * Send the payload as datagram to a defined destination
			 * @param address The destination address encoded as string.
			 * @param buf The buffer to send.
			 * @param length The number of available bytes in the buffer.
			 */
			virtual void send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno, const std::string &address, const char *buf, size_t length) throw (DatagramException);

			/**
			 * Send the payload as datagram to all neighbors (broadcast)
			 * @param buf The buffer to send.
			 * @param length The number of available bytes in the buffer.
			 */
			virtual void send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno, const char *buf, size_t length) throw (DatagramException);

			/**
			 * Receive an incoming datagram.
			 * @param buf A buffer to catch the incoming data.
			 * @param length The length of the buffer.
			 * @param address A buffer for the address of the sender.
			 * @throw If the receive call failed for any reason, an DatagramException is thrown.
			 * @return The number of received bytes.
			 */
			virtual size_t recvfrom(char *buf, size_t length, DatagramService::FRAME_TYPE &type, DatagramService::FLAG_BITS &flags, unsigned int &seqno, std::string &address) throw (DatagramException);

			/**
			 * Get the service description for this convergence layer. This
			 * data is used to contact this node.
			 * @return The service description as string.
			 */
			virtual const std::string getServiceDescription() const;

			/**
			 * The used interface as vinterface object.
			 * @return A vinterface object.
			 */
			virtual const ibrcommon::vinterface& getInterface() const;

			/**
			 * The protocol identifier for this type of service.
			 * @return
			 */
			virtual dtn::core::Node::Protocol getProtocol() const;

			/**
			 * Returns the parameter for the connection.
			 * @return
			 */
			virtual const DatagramService::Parameter& getParameter() const;

		private:
			DatagramService::Parameter _params;
            std::string _bindpath;
            ibrcommon::vsocket _vsocket;
            bool _remove_on_exit;
        };
	} /* namespace net */
} /* namespace dtn */
#endif /* UnixSockDatagramService_H_ */
