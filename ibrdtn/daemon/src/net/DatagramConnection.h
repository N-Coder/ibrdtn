/*
 * DatagramConnection.h
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

#ifndef DATAGRAMCONNECTION_H_
#define DATAGRAMCONNECTION_H_

#include "net/ConvergenceLayer.h"
#include "net/DatagramService.h"
#include <ibrcommon/thread/Thread.h>
#include <ibrcommon/thread/Queue.h>
#include <ibrcommon/thread/Conditional.h>
#include <ibrcommon/TimeMeasurement.h>
#include <streambuf>
#include <iostream>
#include <vector>
#include <stdint.h>

namespace dtn
{
	namespace net
	{
		class DatagramConnection;

		class DatagramConnectionCallback
		{
		public:
			virtual ~DatagramConnectionCallback() = default;;
			virtual void callback_send(DatagramConnection &connection, const char &flags, const unsigned int &seqno, const std::string &destination, const char *buf, const dtn::data::Length &len) throw (DatagramException) = 0;
			virtual void callback_ack(DatagramConnection &connection, const unsigned int &seqno, const std::string &destination) throw (DatagramException) = 0;
			virtual void callback_nack(DatagramConnection &connection, const unsigned int &seqno, const std::string &destination) throw (DatagramException) = 0;

			virtual void connectionUp(const DatagramConnection *conn) = 0;
			virtual void connectionDown(const DatagramConnection *conn) = 0;

			virtual void reportSuccess(size_t retries, double rtt) { };
			virtual void reportFailure() { };

			virtual dtn::core::Node::Protocol getDiscoveryProtocol() const = 0;
		};

		class DatagramConnection : public ibrcommon::JoinableThread
		{
		public:
			DatagramConnection(const std::string &identifier, const DatagramService::Parameter &params, DatagramConnectionCallback &callback);
			~DatagramConnection() override;

			void run() throw () override;
			void setup() throw () override;
			void finally() throw () override;
			void __cancellation() throw () override;
			void shutdown();

			const std::string& getIdentifier() const;
            void setPeerEID(const dtn::data::EID &peer);
            const dtn::data::EID& getPeerEID();

			/**
			 * Queue job for delivery to another node
			 * @param job
			 */
			void queue_data_to_send(const dtn::net::BundleTransfer &job);

			/**
			 * queue data for delivery to the stream
			 * @param buf
			 * @param len
			 */
			void data_received(const char &flags, const unsigned int &seqno, const char *buf, const dtn::data::Length &len);

			/**
			 * This method is called by the DatagramCL, if an ACK is received.
			 * @param seq
			 */
			void ack_received(const unsigned int &seqno);

			/**
			 * This method is called by the DatagramCL, if an permanent NACK is received.
			 */
			void nack_received(const unsigned int &seqno, bool temporary);

		private:
			class Stream : public std::basic_streambuf<char, std::char_traits<char> >, public std::iostream
			{
			public:
				Stream(DatagramConnection &conn, const dtn::data::Length &maxmsglen);
				~Stream() override;

				/**
				 * Queueing data received from the CL worker thread for the LOWPANConnection
				 * @param buf Buffer with received data
				 * @param len Length of the buffer
				 */
				void queue_received_data(const char *buf, const dtn::data::Length &len) throw (DatagramException);

				/**
				 * Close the stream to terminate all blocking
				 * calls on it.
				 */
				void close();

			protected:
				int sync() override;
				std::char_traits<char>::int_type overflow(std::char_traits<char>::int_type) override;
				std::char_traits<char>::int_type underflow() override;

			private:
				// buffer size and maximum message size
				const dtn::data::Length _buf_size;

				// buffer for incoming data to queue
				// the underflow method will block until
				// this buffer contains any data
				std::vector<char> _recv_queue_buf;

				// the number of bytes available in the queue buffer
				dtn::data::Length _recv_queue_buf_len;

				// conditional to lock the queue buffer and the
				// corresponding length variable
				ibrcommon::Conditional _recv_queue_buf_cond;

				// outgoing data from the upper layer is stored
				// here first and processed by the overflow() method
				std::vector<char> _out_buf;

				// incoming data to deliver data to the upper layer
				// is stored in this buffer by the underflow() method
				std::vector<char> _in_buf;

				// this variable is set to true to shutdown
				// this stream
				bool _abort;

				// callback to the corresponding connection object
				DatagramConnection &_callback;
			};

			class Sender : public ibrcommon::JoinableThread
			{
			public:
				Sender(DatagramConnection &conn, Stream &stream);
				~Sender() override;

				void run() throw () override;
				void finally() throw () override;
				void __cancellation() throw () override;

				ibrcommon::Queue<dtn::net::BundleTransfer> queue;
			private:
				DatagramConnection::Stream &_stream;
				DatagramConnection &_connection;
			};

			/**
			 * Send a new frame
			 */
			void send_serialized_stream_data(const char *buf, const dtn::data::Length &len, bool last) throw (DatagramException);

			/**
			 * Adjust the average RTT by the new measured value
			 */
			void adjust_rtt(double value);

            /**
             * Retransmit the whole sliding window buffer on a timeout
             */
			void handle_ack_timeout(bool last);

			DatagramConnectionCallback &_callback;
			DatagramConnection::Stream _stream;
			DatagramConnection::Sender _sender;

            double _avg_rtt;
			ibrcommon::Conditional _send_ack_cond;
			unsigned int _send_next_used_seqno;
			unsigned int _recv_next_expected_seqno;

			const DatagramService::Parameter _params;
			dtn::data::EID _peer_eid;
            const std::string _identifier;

			// buffer for sliding window approach
			class window_frame {
			public:
				// default constructor
				window_frame()
				: flags(0), seqno(0), retry(0) { }

				virtual ~window_frame() = default;

				char flags;
				unsigned int seqno;
				std::vector<char> buf;
				unsigned int retry;
				ibrcommon::TimeMeasurement tm;
			};
			std::list<window_frame> _send_window_frames;
            std::string window_to_string(std::list<window_frame>, size_t max_width);
            size_t window_width(std::list<window_frame> &frames) const;
        };
	} /* namespace data */
} /* namespace dtn */
#endif /* DATAGRAMCONNECTION_H_ */
