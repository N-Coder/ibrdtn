#include "net/BiCEDatagramService.h"
#include <ibrdtn/utils/Utils.h>
#include <core/NodeEvent.h>
#include <core/Node.h>
#include <ibrcommon/Logger.h>
#include <ibrcommon/net/socket.h>
#include <vector>
#include <string.h>

#include <net/ConnectionManager.h>
#include <core/BundleCore.h>
#include <ibrcommon/net/socketstream.h>

namespace dtn {
    namespace net {
        BiCEDatagramService::BiCEDatagramService(const std::string &path, size_t mtu)
                : _bindpath(path), _remove_on_exit(false) {
            // set connection parameters
            _params.max_msg_length = mtu - 2;    // minus 2 bytes because we encode seqno and flags into 2 bytes
            _params.max_seq_numbers = 16;        // seqno 0..15
            _params.flowcontrol = DatagramService::FLOW_SLIDING_WINDOW;
            _params.initial_timeout = 50;        // 50ms
            _params.retry_limit = 5;
        }

        BiCEDatagramService::~BiCEDatagramService() {
            _vsocket.destroy();
        }

        /**
         * Bind to the local socket.
         * @throw If the bind fails, an DatagramException is thrown.
         */
        void BiCEDatagramService::bind() throw(DatagramException) {
            // https://stackoverflow.com/a/10260885/805569
            // need to bind the socket(AF_UNIX, SOCK_DGRAM, 0) to a file here for recvfrom to work
            try {
                _vsocket.destroy();

                // ibrcommon::File file(_bindpath);
                ibrcommon::vaddress addr(_bindpath, "", ibrcommon::vaddress::SCOPE_LOCAL, AF_UNIX);
                ibrcommon::vinterface inter(ibrcommon::vinterface::LOOPBACK);
                ibrcommon::udpsocket *udpsocket = new ibrcommon::udpsocket(inter, addr);
                _vsocket.add(udpsocket);

                _vsocket.up();

                if (udpsocket->ready()) {
                    _remove_on_exit = true;
                } else {
                    throw DatagramException("udpsocket is not ready");
                }
            } catch (const ibrcommon::Exception &e) {
                std::stringstream ss;
                ss << "bind failed: " << e.what();
                std::throw_with_nested(DatagramException(ss.str()));
            }
        }

        /**
         * Shutdown the socket. Unblock all calls on the socket (recv, send, etc.)
         */
        void BiCEDatagramService::shutdown() {
            if (_remove_on_exit) {
                remove(_bindpath.c_str());
            }
            _vsocket.down();
        }

        /**
         * Send the payload as datagram to a defined destination
         * @param address The destination address encoded as string.
         * @param buf The buffer to send.
         * @param length The number of available bytes in the buffer.
         */
        void BiCEDatagramService::send(const char &type, const char &flags, const unsigned int &seqno,
                                       const std::string &identifier, const char *buf,
                                       size_t length) throw(DatagramException) {
            ibrcommon::vaddress destination(identifier, "", ibrcommon::vaddress::SCOPE_LOCAL, AF_UNIX);

            try {
                std::vector<char> tmp(length + 2);

                // add a 2-byte header - type of frame first
                tmp[0] = type;

                // flags (4-bit) + seqno (4-bit)
                tmp[1] = static_cast<char>((0xf0 & (flags << 4)) | (0x0f & seqno));

                // copy payload to the new buffer
                ::memcpy(&tmp[2], buf, length);

                IBRCOMMON_LOGGER_DEBUG_TAG("BiCEDatagramService", 20)
                    << "send() type: " << std::hex << (int) type << "; flags: " << std::hex << (int) flags
                    << "; seqno: " << std::dec << seqno << "; length: " << std::dec << length
                    << "; address: " << destination.toString() << IBRCOMMON_LOGGER_ENDL;

                // create vaddress
                ibrcommon::socketset sockset = _vsocket.getAll();
                for (ibrcommon::socketset::iterator iter = sockset.begin(); iter != sockset.end(); ++iter) {
                    try {
                        ibrcommon::udpsocket &sock = dynamic_cast<ibrcommon::udpsocket &>(**iter);
                        sock.sendto(&tmp[0], length + 2, 0, destination);
                        return;
                    // } catch (const ibrcommon::Exception &) {
                    } catch (const std::bad_cast &) {}
                }
            } catch (const ibrcommon::Exception &e) {
                std::stringstream ss;
                ss << "send failed: " << e.what();
                throw DatagramException(ss.str());
            }
            // throw exception if all sends failed
            throw DatagramException("send failed, no usable socket available");
        }

        /**
         * Send the payload as datagram to all neighbors (broadcast)
         * @param buf The buffer to send.
         * @param length The number of available bytes in the buffer.
         */
        void BiCEDatagramService::send(const char &type, const char &flags, const unsigned int &seqno, const char *buf,
                                       size_t length) throw(DatagramException) {
            return;
        }

        /**
         * Receive an incoming datagram.
         * @param buf A buffer to catch the incoming data.
         * @param length The length of the buffer.
         * @param address A buffer for the address of the sender.
         * @throw If the receive call failed for any reason, an DatagramException is thrown.
         * @return The number of received bytes.
         */
        size_t BiCEDatagramService::recvfrom(char *buf, size_t length, char &type, char &flags, unsigned int &seqno,
                                             std::string &address) throw(DatagramException) {

            try {
                ibrcommon::socketset readfds;
                _vsocket.select(&readfds, NULL, NULL, NULL);

                for (ibrcommon::socketset::iterator iter = readfds.begin(); iter != readfds.end(); ++iter) {
                    try {
                        ibrcommon::udpsocket &sock = dynamic_cast<ibrcommon::udpsocket &>(**iter);

                        std::vector<char> tmp(length + 2);
                        ibrcommon::vaddress peeraddr;
                        size_t ret = sock.recvfrom(&tmp[0], length + 2, 0, peeraddr);

                        // first byte is the type
                        type = tmp[0];

                        // second byte is flags (4-bit) + seqno (4-bit)
                        flags = 0x0f & (tmp[1] >> 4);
                        seqno = 0x0f & tmp[1];

                        // return the encoded format
                        address = peeraddr.address();

                        // copy payload to the destination buffer
                        ::memcpy(buf, &tmp[2], ret - 2);

                        IBRCOMMON_LOGGER_DEBUG_TAG("BiCEDatagramService", 20)
                            << "recvfrom() type: " << std::hex << (int) type << "; flags: " << std::hex << (int) flags
                            << "; seqno: " << std::dec << seqno << "; length: " << std::dec << length
                            << "; address: " << peeraddr.toString() << IBRCOMMON_LOGGER_ENDL;

                        return ret - 2;
                    } catch (const std::bad_cast &) {}
                }
            } catch (const ibrcommon::Exception &e) {
                std::stringstream ss;
                ss << "receive failed: " << e.what();
                throw DatagramException(ss.str());
            }

            return 0;
        }

        /**
         * Get the service description for this convergence layer. This
         * data is used to contact this node.
         * @return The service description as string.
         */
        const std::string BiCEDatagramService::getServiceDescription() const {
            std::stringstream ss;
            ss << getInterface().toString();
            ss << "://";
            ss << _bindpath;
            return ss.str();
        }

        static const ibrcommon::vinterface vinterface("bice");

        /**
         * The used interface as vinterface object.
         * @return A vinterface object.
         */
        const ibrcommon::vinterface &BiCEDatagramService::getInterface() const {
            return vinterface;
        }

        /**
         * The protocol identifier for this type of service.
         * @return
         */
        dtn::core::Node::Protocol BiCEDatagramService::getProtocol() const {
            return dtn::core::Node::CONN_DGRAM_UNIX;
        }

        const DatagramService::Parameter &BiCEDatagramService::getParameter() const {
            return _params;
        }
    } /* namespace net */
} /* namespace dtn */
