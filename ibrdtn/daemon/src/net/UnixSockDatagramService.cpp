#include "net/UnixSockDatagramService.h"
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
        static const char *const TAG = "UnixSockDatagramService";
        static const ibrcommon::vinterface vinterface("unix");

        UnixSockDatagramService::UnixSockDatagramService(const std::string &path, size_t mtu)
                : _bindpath(path), _remove_on_exit(false) {
            _params.max_msg_length = mtu - FRAME_HEADER_LONG_LENGTH;
            _params.max_seq_numbers = FRAME_SEQNO_LONG_MAX;
            _params.send_window_size = 8;
            _params.recv_window_size = 8;
        }

        UnixSockDatagramService::~UnixSockDatagramService() {
            _vsocket.destroy();
        }

        /**
         * Bind to the local socket.
         * @throw If the bind fails, an DatagramException is thrown.
         */
        void UnixSockDatagramService::bind() throw(DatagramException) {
            // https://stackoverflow.com/a/10260885/805569
            // need to bind the socket(AF_UNIX, SOCK_DGRAM, 0) to a file here for recvfrom to work
            try {
                _vsocket.destroy();

                // ibrcommon::File file(_bindpath);
                ibrcommon::vaddress addr(_bindpath, "", ibrcommon::vaddress::SCOPE_LOCAL, AF_UNIX);
                // TODO make parent directories, overwrite file itself
                ibrcommon::udpsocket *udpsocket = new ibrcommon::udpsocket(vinterface, addr);
                _vsocket.add(udpsocket);

                _vsocket.up();

                if (udpsocket->ready()) { // TODO exceptions in up will cause socket file to persist => unconditionally delete and log warning?
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
        void UnixSockDatagramService::shutdown() {
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
        void UnixSockDatagramService::send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno,
                                           const std::string &identifier, const char *buf,
                                           size_t length) throw(DatagramException) {
            ibrcommon::vaddress destination(identifier, "", ibrcommon::vaddress::SCOPE_LOCAL, AF_UNIX);

            try {
                std::vector<char> tmp(length + 2);

                DatagramService::write_header_long(&tmp[0], type, flags, seqno);

                // copy payload to the new buffer
                ::memcpy(&tmp[2], buf, length);

                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 60)
                    << "send() "
                    << packet_to_string(type, flags, seqno, buf, length, identifier)
                    << IBRCOMMON_LOGGER_ENDL;

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
        void UnixSockDatagramService::send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno, const char *buf,
                                           size_t length) throw(DatagramException) {
            send(type, flags, seqno, _bindpath + ".broadcast", buf, length);
        }

        /**
         * Receive an incoming datagram.
         * @param buf A buffer to catch the incoming data.
         * @param length The length of the buffer.
         * @param address A buffer for the address of the sender.
         * @throw If the receive call failed for any reason, an DatagramException is thrown.
         * @return The number of received bytes.
         */
        size_t UnixSockDatagramService::recvfrom(char *buf, size_t length, DatagramService::FRAME_TYPE &type, DatagramService::FLAG_BITS &flags, unsigned int &seqno,
                                                 std::string &address) throw(DatagramException) {
            while (true) {
                try {
                    ibrcommon::socketset readfds;
                    _vsocket.select(&readfds, NULL, NULL, NULL);

                    for (auto iter = readfds.begin(); iter != readfds.end(); ++iter) {
                        try {
                            auto &sock = dynamic_cast<ibrcommon::udpsocket &>(**iter);

                            std::vector<char> tmp(length + 2);
                            ibrcommon::vaddress peeraddr;
                            size_t ret = sock.recvfrom(&tmp[0], length + 2, 0, peeraddr);
                            if (ret == 0) {
                                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 45)
                                    << "recvfrom() got empty datagram from " << peeraddr.toString()
                                    << ", discarding" << IBRCOMMON_LOGGER_ENDL;
                                continue;
                            } else if (ret == 1) {
                                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 45)
                                    << "recvfrom() got datagram with only one byte (0x" << SS_HEX(tmp[0])
                                    << ") from " << peeraddr.toString()
                                    << ", discarding" << IBRCOMMON_LOGGER_ENDL;
                                continue;
                            }

                            DatagramService::read_header_long(&tmp[0], type, flags, seqno);

                            // return the encoded format
                            address = peeraddr.address();

                            // copy payload to the destination buffer
                            ::memcpy(buf, &tmp[2], ret - 2);

                            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 60)
                                << "recvfrom() "
                                << packet_to_string(type, flags, seqno, buf, ret - 2, peeraddr.toString())
                                << IBRCOMMON_LOGGER_ENDL;

                            return ret - 2;
                        } catch (const std::bad_cast &) {}
                    }
                } catch (const ibrcommon::Exception &e) {
                    std::stringstream ss;
                    ss << "receive failed: " << e.what();
                    throw DatagramException(ss.str());
                }
            }
        }

        /**
         * Get the service description for this convergence layer. This
         * data is used to contact this node.
         * @return The service description as string.
         */
        const std::string UnixSockDatagramService::getServiceDescription() const {
            std::stringstream ss;
            ss << getInterface().toString();
            ss << "://";
            ss << _bindpath;
            return ss.str();
        }

        /**
         * The used interface as vinterface object.
         * @return A vinterface object.
         */
        const ibrcommon::vinterface &UnixSockDatagramService::getInterface() const {
            return vinterface;
        }

        /**
         * The protocol identifier for this type of service.
         * @return
         */
        dtn::core::Node::Protocol UnixSockDatagramService::getProtocol() const {
            return dtn::core::Node::CONN_DGRAM_UNIX;
        }

        const DatagramService::Parameter &UnixSockDatagramService::getParameter() const {
            return _params;
        }
    } /* namespace net */
} /* namespace dtn */
