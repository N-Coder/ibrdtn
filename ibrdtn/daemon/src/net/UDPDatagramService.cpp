/*
 * UDPDatagramService.cpp
 *
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "net/UDPDatagramService.h"
#include <ibrdtn/utils/Utils.h>
#include <ibrcommon/Logger.h>
#include <ibrcommon/net/socket.h>
#include <vector>
#include <cstring>

#ifdef __WIN32__
#define EADDRINUSE WSAEADDRINUSE
#endif

namespace dtn {
    namespace net {
        const ibrcommon::vaddress UDPDatagramService::BROADCAST_ADDR(
                "ff02::1", UDPDatagramService::BROADCAST_PORT, AF_INET6);
        static const char *const TAG = "UDPDatagramService";


        UDPDatagramService::UDPDatagramService(const ibrcommon::vinterface &iface, int port, size_t mtu)
                : _msock(NULL), _iface(iface), _bind_port(port) {
            _params.max_msg_length = mtu - FRAME_HEADER_LONG_LENGTH;
            _params.max_seq_numbers = FRAME_SEQNO_LONG_MAX;
            _params.send_window_size = 8;
            _params.recv_window_size = 8;
        }

        UDPDatagramService::~UDPDatagramService() {
            // delete all sockets
            _vsocket.destroy();
        }

        /**
		 * Bind to the local socket.
		 * @throw If the bind fails, an DatagramException is thrown.
		 */
        void UDPDatagramService::bind() throw(DatagramException) {
            // delete all sockets
            _vsocket.destroy();

            try {
                // bind socket to the multicast port
                _msock = new ibrcommon::multicastsocket(BROADCAST_PORT);

                try {
                    _msock->join(BROADCAST_ADDR, _iface);
                } catch (const ibrcommon::socket_raw_error &e) {
                    if (e.error() != EADDRINUSE) {
                        IBRCOMMON_LOGGER_TAG(TAG, error) << "join failed on " << _iface.toString() << "; " << e.what()
                                                         << IBRCOMMON_LOGGER_ENDL;
                    }
                } catch (const ibrcommon::socket_exception &e) {
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 10) << "can not join " << BROADCAST_ADDR.toString() << " on "
                                                        << _iface.toString() << "; " << e.what()
                                                        << IBRCOMMON_LOGGER_ENDL;
                }

                // add multicast socket to receiver sockets
                _vsocket.add(_msock);

                if (_iface.isAny()) {
                    // bind socket to interface
                    _vsocket.add(new ibrcommon::udpsocket(_bind_port));
                } else {
                    // create sockets for all addresses on the interface
                    std::list<ibrcommon::vaddress> addrs = _iface.getAddresses();

                    // convert the port into a string
                    std::stringstream ss;
                    ss << _bind_port;

                    for (std::list<ibrcommon::vaddress>::iterator iter = addrs.begin(); iter != addrs.end(); ++iter) {
                        ibrcommon::vaddress &addr = (*iter);

                        // handle the addresses according to their family
                        switch (addr.family()) {
                            case AF_INET:
                            case AF_INET6:
                                addr.setService(ss.str());
                                _vsocket.add(new ibrcommon::udpsocket(addr), _iface);
                                break;
                            default:
                                break;
                        }
                    }
                }
            } catch (const ibrcommon::Exception &) {
                throw DatagramException("bind failed");
            }

            // setup socket operations
            _vsocket.up();
        }

        /**
         * Shutdown the socket. Unblock all calls on the socket (recv, send, etc.)
         */
        void UDPDatagramService::shutdown() {
            // abort socket operations
            _vsocket.down();
        }

        /**
         * Send the payload as datagram to a defined destination
         * @param address The destination address encoded as string.
         * @param buf The buffer to send.
         * @param length The number of available bytes in the buffer.
         */
        void UDPDatagramService::send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno,
                                      const std::string &address, const char *buf, size_t length) throw(DatagramException) {
            // decode address identifier
            ibrcommon::vaddress destination;
            UDPDatagramService::decode(address, destination);

            // forward to actually send method
            send(type, flags, seqno, destination, buf, length);
        }

        /**
         * Send the payload as datagram to all neighbors (broadcast)
         * @param buf The buffer to send.
         * @param length The number of available bytes in the buffer.
         */
        void UDPDatagramService::send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno, const char *buf,
                                      size_t length) throw(DatagramException) {
            // forward to actually send method using the broadcast address
            send(type, flags, seqno, BROADCAST_ADDR, buf, length);
        }

        void UDPDatagramService::send(const DatagramService::FRAME_TYPE &type, const DatagramService::FLAG_BITS &flags, const unsigned int &seqno,
                                      const ibrcommon::vaddress &destination, const char *buf,
                                      size_t length) throw(DatagramException) {
            try {
                std::vector<char> tmp(length + 2);

                DatagramService::write_header_long(&tmp[0], type, flags, seqno);

                // copy payload to the new buffer
                ::memcpy(&tmp[2], buf, length);

                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 60)
                    << "send() "
                    << packet_to_string(type, flags, seqno, buf, length, destination.toString())
                    << IBRCOMMON_LOGGER_ENDL;

                // create vaddress
                ibrcommon::socketset sockset = _vsocket.getAll();
                for (ibrcommon::socketset::iterator iter = sockset.begin(); iter != sockset.end(); ++iter) {
                    if ((*iter) == _msock) continue;
                    try {
                        ibrcommon::udpsocket &sock = dynamic_cast<ibrcommon::udpsocket &>(**iter);
                        sock.sendto(&tmp[0], length + 2, 0, destination);
                        return;
                    } catch (const ibrcommon::Exception &) {
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
         * Receive an incoming datagram.
         * @param buf A buffer to catch the incoming data.
         * @param length The length of the buffer.
         * @param address A buffer for the address of the sender.
         * @throw If the receive call failed for any reason, an DatagramException is thrown.
         * @return The number of received bytes.
         */
        size_t UDPDatagramService::recvfrom(char *buf, size_t length, DatagramService::FRAME_TYPE &type, DatagramService::FLAG_BITS &flags, unsigned int &seqno,
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
                            address = UDPDatagramService::encode(peeraddr);

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
        const std::string UDPDatagramService::getServiceDescription() const {
            // get all addresses
            std::list<ibrcommon::vaddress> addrs = _iface.getAddresses();

            for (std::list<ibrcommon::vaddress>::iterator iter = addrs.begin(); iter != addrs.end(); ++iter) {
                ibrcommon::vaddress &addr = (*iter);

                try {
                    // handle the addresses according to their family
                    switch (addr.family()) {
                        case AF_INET:
                        case AF_INET6:
                            return UDPDatagramService::encode(addr, _bind_port);
                            break;
                        default:
                            break;
                    }
                } catch (const ibrcommon::vaddress::address_exception &ex) {
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25) << ex.what() << IBRCOMMON_LOGGER_ENDL;
                }
            }

            // no addresses available, return empty string
            return "";
        }

        /**
         * The used interface as vinterface object.
         * @return A vinterface object.
         */
        const ibrcommon::vinterface &UDPDatagramService::getInterface() const {
            return _iface;
        }

        /**
         * The protocol identifier for this type of service.
         * @return
         */
        dtn::core::Node::Protocol UDPDatagramService::getProtocol() const {
            return dtn::core::Node::CONN_DGRAM_UDP;
        }

        const DatagramService::Parameter &UDPDatagramService::getParameter() const {
            return _params;
        }

        const std::string UDPDatagramService::encode(const ibrcommon::vaddress &address, const int port) {
            std::stringstream ss;
            ss << "ip=" << address.address() << ";port=";

            if (port == 0) ss << address.service();
            else ss << port;

            ss << ";";
            return ss.str();
        }

        void UDPDatagramService::decode(const std::string &identifier, ibrcommon::vaddress &address) {
            std::string addr;
            std::string port;

            // parse parameters
            std::vector<std::string> parameters = dtn::utils::Utils::tokenize(";", identifier);
            std::vector<std::string>::const_iterator param_iter = parameters.begin();

            while (param_iter != parameters.end()) {
                std::vector<std::string> p = dtn::utils::Utils::tokenize("=", (*param_iter));

                if (p[0].compare("ip") == 0) {
                    addr = p[1];
                }

                if (p[0].compare("port") == 0) {
                    port = p[1];
                }

                ++param_iter;
            }

            address = ibrcommon::vaddress(addr, port);
        }
    } /* namespace net */
} /* namespace dtn */
