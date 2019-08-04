/*
 * DatagramConnection.cpp
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

#include "net/DatagramConnection.h"
#include "net/TransferAbortedEvent.h"
#include "core/BundleCore.h"

#include <ibrdtn/utils/Utils.h>
#include <ibrdtn/data/Serializer.h>

#include <ibrcommon/TimeMeasurement.h>
#include <ibrcommon/Logger.h>
#include <string.h>

#include <iomanip>
#include <cassert>

#define AVG_RTT_WEIGHT 0.875

#define NEXT_SEQNO(s) ((s + 1) % _params.max_seq_numbers)
#define SEND_WINDOW_STRING window_to_string(_send_window_frames, _params.send_window_size)
#define RECV_WINDOW_STRING window_to_string(_recv_window_frames, _params.recv_window_size)

namespace dtn {
    namespace net {
        static const char *const TAG = "DatagramConnection";

        DatagramConnection::DatagramConnection(
                const std::string &identifier, const DatagramService::Parameter &params,
                DatagramConnectionCallback &callback)
                : _callback(callback), _identifier(identifier),
                  _stream(*this, params.max_msg_length), _sender(*this, _stream),
                  _send_next_used_seqno(0), _recv_next_expected_seqno(0),
                  _params(params), _avg_rtt(static_cast<double>(params.initial_timeout)) {
        }

        DatagramConnection::~DatagramConnection() {
            _sender.join();
            join();
        }

        void DatagramConnection::shutdown() {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "shutdown(" << getIdentifier() << ")" << IBRCOMMON_LOGGER_ENDL;

            __cancellation();
        }

        void DatagramConnection::__cancellation() throw() {
            try {
                _stream.close();
            } catch (const ibrcommon::Exception &) {};
        }

        void DatagramConnection::setup() throw() {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "setup(" << getIdentifier() << ")" << IBRCOMMON_LOGGER_ENDL;

            _callback.connectionUp(this);
            _sender.start();
        }

        void DatagramConnection::finally() throw() {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "finally(" << getIdentifier() << ")" << IBRCOMMON_LOGGER_ENDL;

            try {
                ibrcommon::MutexLock l(_send_ack_cond);
                _send_ack_cond.abort();
            } catch (const std::exception &) {};

            try {
                // shutdown the sender thread
                _sender.stop();

                // wait until all operations are stopped
                _sender.join();
            } catch (const std::exception &) {};

            try {
                // remove this connection from the connection list
                _callback.connectionDown(this);
            } catch (const ibrcommon::MutexException &) {};
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        const std::string &DatagramConnection::getIdentifier() const {
            return _identifier;
        }

        void DatagramConnection::setPeerEID(const dtn::data::EID &peer) {
            _peer_eid = peer;
        }

        const dtn::data::EID &DatagramConnection::getPeerEID() {
            return _peer_eid;
        }

        void DatagramConnection::adjust_rtt(double value) {
            _avg_rtt = (_avg_rtt * AVG_RTT_WEIGHT) + ((1 - AVG_RTT_WEIGHT) * value);
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "RTT adjusted, measured value: " << std::setprecision(4) << value << ", new avg. RTT: "
                << _avg_rtt << IBRCOMMON_LOGGER_ENDL;
        }

        std::string DatagramConnection::window_to_string(std::list<window_frame> &frames, size_t max_width) {
            std::stringstream ss;
            ss << "{";
            auto last = frames.end();
            last--;
            for (auto it = frames.begin(); it != frames.end(); it++) {
                window_frame &frame = *it;
                ss << "[" << (uint8_t) frame.flags;
                ss << " #" << frame.seqno << ", len " << frame.buf.size();
                ss << " | retry " << frame.retry << ", t ";
                ss << (frame.tm.getSeconds() * 1000 + frame.tm.getMilliseconds()) << "ms]";
                if (it != last) {
                    ss << ", ";
                }
            }
            size_t width = window_width(frames);
            ss << "}(" << frames.size() << "/" << width << "/" << max_width << ")";
            //assert(frames.size() <= width && width <= max_width);
            if (frames.size() > width || width > max_width) {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                    << "window width violation: " << ss.str() << IBRCOMMON_LOGGER_ENDL;
            }
            return ss.str();
        }

        size_t DatagramConnection::window_width(std::list<window_frame> &frames) const {
            if (frames.empty()) {
                return 0;
            } else {
                size_t width = _params.max_seq_numbers + frames.back().seqno - frames.front().seqno;
                return (width + 1) % _params.max_seq_numbers;
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        DatagramConnection::Sender::Sender(DatagramConnection &conn, Stream &stream)
                : _stream(stream), _connection(conn) {}

        DatagramConnection::Sender::~Sender() = default;

        void DatagramConnection::Sender::finally() throw() {
        }

        void DatagramConnection::Sender::__cancellation() throw() {
            // abort all blocking operations on the stream
            _stream.close();

            // abort blocking calls on the queue
            queue.abort();
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        DatagramConnection::Stream::Stream(DatagramConnection &conn, const dtn::data::Length &maxmsglen)
                : std::iostream(this), _buf_size(maxmsglen), _recv_queue_buf(_buf_size), _recv_queue_buf_len(0),
                  _out_buf(_buf_size), _in_buf(_buf_size),
                  _abort(false), _callback(conn) {
            // Initialize get pointer. This should be zero so that underflow is called upon first read.
            setg(0, 0, 0);

            // mark the buffer for outgoing data as free
            // leave 1 byte space for the byte c causing the overflow
            setp(&_out_buf[0], &_out_buf[0] + _buf_size - 1);
        }

        DatagramConnection::Stream::~Stream() = default;


        void DatagramConnection::Stream::close() {
            ibrcommon::MutexLock l(_recv_queue_buf_cond);
            _abort = true;
            _recv_queue_buf_cond.abort();
        }

        int DatagramConnection::Stream::sync() {
            return std::char_traits<char>::eq_int_type(
                    this->overflow(std::char_traits<char>::eof()),
                    std::char_traits<char>::eof()
            ) ? -1 : 0;
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        void DatagramConnection::nack_received(const unsigned int &seqno, const bool temporary) {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 20) << "nack received for seqno " << seqno << IBRCOMMON_LOGGER_ENDL;
        }

        void DatagramConnection::ack_received(const unsigned int &received_seqno) {
            ibrcommon::MutexLock l(_send_ack_cond);

            for (auto itr = _send_window_frames.begin(); itr != _send_window_frames.end(); ++itr) {
                window_frame &f = *itr;
                if (received_seqno == NEXT_SEQNO(f.seqno)) {
                    f.tm.stop();
                    adjust_rtt(f.tm.getMilliseconds());
                    _callback.reportSuccess(f.retry, f.tm.getMilliseconds());

                    _send_window_frames.erase(itr);
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                        << "received ACK " << received_seqno << ", new window is " << SEND_WINDOW_STRING
                        << IBRCOMMON_LOGGER_ENDL;

                    _send_ack_cond.signal(true);
                    return;
                }
            }

            // TODO handle double ACK
            if (_send_window_frames.empty()) {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                    << "received ACK " << received_seqno << " while window is empty, discarding"
                    << IBRCOMMON_LOGGER_ENDL;
            } else {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                    << "received ACK " << received_seqno << " which is not in window "
                    << SEND_WINDOW_STRING << ", discarding"
                    << IBRCOMMON_LOGGER_ENDL;
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        void DatagramConnection::data_received(
                const char &flags, const unsigned int &received_seqno, const char *buf, const dtn::data::Length &len) {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                << "frame received, flags: " << (int) flags << ", seqno: " << received_seqno
                << ", len: " << len << " via " << getIdentifier() << " in receive window "
                << RECV_WINDOW_STRING << IBRCOMMON_LOGGER_ENDL;

            // TODO what if stream on the other side was reset? => drop / flush window depending on first / last markers

            const std::list<window_frame>::iterator &frame = get_recv_window_frame(received_seqno);
            if (frame != _recv_window_frames.end()) {
                frame->buf.assign(buf, buf + len);
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 35)
                    << "inserted received data, new receive window is " << RECV_WINDOW_STRING << ", flushing"
                    << IBRCOMMON_LOGGER_ENDL;

                unsigned int flushed = flush_recv_window();
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 35)
                    << "flushed " << flushed << " frames of received data, new receive window is " << RECV_WINDOW_STRING
                    << ", sending selective ACK" << IBRCOMMON_LOGGER_ENDL;

                _callback.callback_ack(*this, NEXT_SEQNO(received_seqno), getIdentifier());
            } else {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 35)
                    << "frame " << received_seqno << " is not in the current receive window, sending ACK nonetheless"
                    << IBRCOMMON_LOGGER_ENDL;
                // TODO handle lost ACK / too big sender window properly
            }
        }

        std::list<DatagramConnection::window_frame>::iterator
        DatagramConnection::get_recv_window_frame(const unsigned int &for_seqno) {
            size_t seqno = _recv_next_expected_seqno;
            auto it = _recv_window_frames.begin();
            while (true) {
                if (it == _recv_window_frames.end()) {
                    _recv_window_frames.emplace_back();
                    _recv_window_frames.back().seqno = seqno;
                    it--; // we're still 1 past the end, so step back to get to the inserted / last element
                }
                //assert(it->seqno == seqno);
                if (it->seqno != seqno) {
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 5)
                        << "looking for " << for_seqno << " in " << RECV_WINDOW_STRING
                        << "; element at position for seqno " << seqno << " claims to have seqno " << it->seqno
                        << IBRCOMMON_LOGGER_ENDL;
                }
                if (for_seqno == seqno) {
                    assert(it != _recv_window_frames.end());
                    return it;
                }
                if (window_width(_recv_window_frames) >= _params.recv_window_size) {
                    return _recv_window_frames.end();
                }
                seqno = NEXT_SEQNO(seqno);
                it++;
            }
        }

        unsigned int DatagramConnection::flush_recv_window() {
            unsigned int flushed = 0;
            while (!_recv_window_frames.empty()) {
                std::vector<char> &buf = _recv_window_frames.front().buf;
                //assert(_recv_window_frames.front().seqno == _recv_next_expected_seqno);
                if (_recv_window_frames.front().seqno != _recv_next_expected_seqno) {
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 5)
                        << "flushing " << RECV_WINDOW_STRING
                        << "; element at front claims to have seqno " << _recv_window_frames.front().seqno
                        << IBRCOMMON_LOGGER_ENDL;
                }
                if (!buf.empty()) {
                    flushed++;
                    _stream.queue_received_data(&buf[0], buf.size());
                    _recv_next_expected_seqno = NEXT_SEQNO(_recv_next_expected_seqno);
                    _callback.callback_ack(*this, _recv_next_expected_seqno, getIdentifier());
                    _recv_window_frames.pop_front();
                } else {
                    break;
                }
            }
            return flushed;
        }

        void DatagramConnection::Stream::queue_received_data(
                const char *buf, const dtn::data::Length &len) throw(DatagramException) {
            try {
                ibrcommon::MutexLock l(_recv_queue_buf_cond);
                if (_abort) throw DatagramException("stream aborted");
                // wait until the buffer is free
                while (_recv_queue_buf_len > 0) {
                    _recv_queue_buf_cond.wait();
                    if (_abort) throw DatagramException("stream aborted");
                }

                // copy the new data into the buffer, but leave out the first byte (header)
                ::memcpy(&_recv_queue_buf[0], buf, len);

                // store the buffer length
                _recv_queue_buf_len = len;

                // notify waiting threads
                _recv_queue_buf_cond.signal();
            } catch (ibrcommon::Conditional::ConditionalAbortException &ex) {
                throw DatagramException("stream aborted");
            }
        }

        std::char_traits<char>::int_type DatagramConnection::Stream::underflow() {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "Stream::underflow()" << IBRCOMMON_LOGGER_ENDL;

            try {
                ibrcommon::MutexLock l(_recv_queue_buf_cond);
                if (_abort) throw DatagramException("stream aborted");
                while (_recv_queue_buf_len == 0) {
                    _recv_queue_buf_cond.wait();
                    if (_abort) throw DatagramException("stream aborted");
                }

                // copy the queue buffer to an internal buffer
                ::memcpy(&_in_buf[0], &_recv_queue_buf[0], _recv_queue_buf_len);

                // Since the input buffer content is now valid (or is new)
                // the get pointer should be initialized (or reset).
                setg(&_in_buf[0], &_in_buf[0], &_in_buf[0] + _recv_queue_buf_len);

                // mark the queue buffer as free
                _recv_queue_buf_len = 0;
                _recv_queue_buf_cond.signal();

                return std::char_traits<char>::not_eof(_in_buf[0]);
            } catch (ibrcommon::Conditional::ConditionalAbortException &ex) {
                throw DatagramException("stream aborted");
            }
        }

        void DatagramConnection::run() throw() { // deserialize data from stream and forward to CL
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "run(" << getIdentifier() << ")" << IBRCOMMON_LOGGER_ENDL;

            // create a filter context
            dtn::core::FilterContext context;
            context.setPeer(_peer_eid);
            context.setProtocol(_callback.getDiscoveryProtocol());

            // create a deserializer for the stream
            dtn::data::DefaultDeserializer deserializer(_stream, dtn::core::BundleCore::getInstance());

            try {
                while (_stream.good()) {
                    try {
                        dtn::data::Bundle bundle;

                        // read the bundle out of the stream
                        deserializer >> bundle;
                        // TODO validate bundle data for plausibility

                        // push bundle through the filter routines
                        context.setBundle(bundle);
                        BundleFilter::ACTION ret = dtn::core::BundleCore::getInstance().filter(
                                dtn::core::BundleFilter::INPUT, context, bundle);

                        switch (ret) {
                            case BundleFilter::ACCEPT:
                                // inject bundle into core
                                dtn::core::BundleCore::getInstance().inject(_peer_eid, bundle, false);
                                break;

                            case BundleFilter::REJECT:
                                throw dtn::data::Validator::RejectedException("rejected by input filter");
                                break;

                            case BundleFilter::DROP:
                            default:
                                break;
                        }
                    } catch (const dtn::data::Validator::RejectedException &ex) {
                        IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                            << "Bundle rejected: " << ex.what() << IBRCOMMON_LOGGER_ENDL;
                    } catch (const dtn::InvalidDataException &ex) {
                        IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                            << "Received an invalid bundle: " << ex.what() << IBRCOMMON_LOGGER_ENDL;
                    }
                }
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                    << "deserialization stream went bad" << IBRCOMMON_LOGGER_ENDL;
            } catch (std::exception &ex) {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                    << "Main-thread died: " << ex.what() << IBRCOMMON_LOGGER_ENDL;
            }
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        void DatagramConnection::queue_data_to_send(const dtn::net::BundleTransfer &job) {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 15)
                << "queue sending of bundle " << job.getBundle().toString() << " to " << job.getNeighbor().getString()
                << " via " << getIdentifier() << IBRCOMMON_LOGGER_ENDL;

            _sender.queue.push(job);
        }

        void DatagramConnection::Sender::run() throw() {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40) << "Sender::run()" << IBRCOMMON_LOGGER_ENDL;

            try {
                // get reference to the storage
                dtn::storage::BundleStorage &storage = dtn::core::BundleCore::getInstance().getStorage();

                // create a filter context
                dtn::core::FilterContext context;
                context.setProtocol(_connection._callback.getDiscoveryProtocol());

                // create a standard serializer
                dtn::data::DefaultSerializer serializer(_stream);

                // as long as the stream is marked as good ...
                while (_stream.good()) {
                    // get the next job
                    dtn::net::BundleTransfer job = queue.poll(); // from DatagramConnection::queue_data_to_send

                    dtn::data::Bundle bundle;
                    try {
                        // read the bundle out of the storage
                        bundle = storage.get(job.getBundle());
                    } catch (const dtn::storage::NoBundleFoundException &) {
                        // could not load the bundle, abort the job
                        job.abort(dtn::net::TransferAbortedEvent::REASON_BUNDLE_DELETED);
                        continue;
                    }

                    // push bundle through the filter routines
                    context.setBundle(bundle);
                    context.setPeer(job.getNeighbor());
                    // TODO add connection identifier to filter context
                    BundleFilter::ACTION ret = dtn::core::BundleCore::getInstance().filter(
                            dtn::core::BundleFilter::OUTPUT, context, bundle);

                    if (ret != BundleFilter::ACCEPT) {
                        job.abort(dtn::net::TransferAbortedEvent::REASON_REFUSED_BY_FILTER);
                        continue;
                    }

                    // write the bundle into the stream
                    serializer << bundle;
                    _stream.flush();

                    // check if the stream is still marked as good
                    if (_stream.good()) {
                        // bundle send completely - raise bundle event
                        job.complete();
                    }
                }

                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                    << "Sender::run() stream destroyed" << IBRCOMMON_LOGGER_ENDL;
            } catch (const ibrcommon::QueueUnblockedException &ex) {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                    << "Sender::run() exception: " << ex.what() << IBRCOMMON_LOGGER_ENDL;
            } catch (std::exception &ex) {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                    << "Sender::run() exception: " << ex.what() << IBRCOMMON_LOGGER_ENDL;
                // if transmission failed due to too many retries, Sender will terminate here
                // and the DatagramConvergenceLayer will then open a new DatagramConnection
            }
        }

        std::char_traits<char>::int_type DatagramConnection::Stream::overflow(std::char_traits<char>::int_type c) {
            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 40)
                << "Stream::overflow()" << IBRCOMMON_LOGGER_ENDL;

            if (_abort) throw DatagramException("stream aborted");

            char *ibegin = &_out_buf[0];
            char *iend = pptr();

            // mark the buffer for outgoing data as free
            // leave 1 byte space for the byte c causing the overflow
            setp(&_out_buf[0], &_out_buf[0] + _buf_size - 1);

            // copy the overflowing byte
            bool not_eof = std::char_traits<char>::not_eof(c);
            if (not_eof) {
                *iend++ = std::char_traits<char>::to_char_type(c);
            }

            // bytes to send
            const dtn::data::Length bytes = (iend - ibegin);

            // if there is nothing to send, just return
            if (bytes != 0) {
                try {
                    _callback.send_serialized_stream_data(&_out_buf[0], bytes, !not_eof);
                } catch (const DatagramException &ex) {
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 35)
                        << "Stream::overflow() exception: " << ex.what() << IBRCOMMON_LOGGER_ENDL;

                    close(); // close this stream
                    throw; // re-throw the DatagramException
                }
            } else {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 35)
                    << "Stream::overflow() nothing to send" << IBRCOMMON_LOGGER_ENDL;
            }

            return not_eof;
        }

        void DatagramConnection::send_serialized_stream_data(
                const char *buf, const dtn::data::Length &len, bool last) throw(DatagramException) {
            char flags = 0;

            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 25)
                << "frame to send, flags: " << (int) flags << ", seqno: " << _send_next_used_seqno
                << ", len: " << len << " via " << getIdentifier() << IBRCOMMON_LOGGER_ENDL;

            // lock the ACK variables and frame window
            ibrcommon::MutexLock l(_send_ack_cond);

            // add new frame to the window
            assert(window_width(_send_window_frames) < _params.send_window_size);
            _send_window_frames.emplace_back(); // constructs new window_frame() and appends it
            window_frame &new_frame = _send_window_frames.back();
            new_frame.flags = flags;
            unsigned int seqno = new_frame.seqno = _send_next_used_seqno;
            new_frame.buf.assign(buf, buf + len);
            new_frame.retry = 0;
            new_frame.tm.start();

            // send the datagram
            _callback.callback_send(*this, new_frame.flags, seqno, getIdentifier(), &new_frame.buf[0],
                                    new_frame.buf.size());

            IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                << "appended datagram with seqno " << seqno << " to window " << SEND_WINDOW_STRING
                << IBRCOMMON_LOGGER_ENDL;

            // increment next sequence number
            _send_next_used_seqno = NEXT_SEQNO(_send_next_used_seqno);

            // set timeout to twice the average round-trip-time
            size_t timeout = static_cast<size_t>(_avg_rtt * 2) + 1;
            struct timespec ts;
            ibrcommon::Conditional::gettimeout(timeout, &ts);

            try {
                // wait until one more slot is available => next block can safely be queued
                // or no more frames are to ACK if this was the last frame => all buffers flushed
                while (window_width(_send_window_frames) >= _params.send_window_size
                       || (last && !_send_window_frames.empty())) {
                    _send_ack_cond.wait(&ts);
                }
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                    << "got ack and window is no longer full, sender of seqno " << seqno
                    << " can be unblocked, new window is " << SEND_WINDOW_STRING << IBRCOMMON_LOGGER_ENDL;
            } catch (const ibrcommon::Conditional::ConditionalAbortException &e) {
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                    << "waiting interrupted (" << e.reason << ") while handling datagram with seqno " << seqno
                    << " in window " << SEND_WINDOW_STRING << IBRCOMMON_LOGGER_ENDL;
                if (e.reason == ibrcommon::Conditional::ConditionalAbortException::COND_TIMEOUT) {
                    handle_ack_timeout(last); // timeout - retransmit the whole window
                } else {
                    throw;
                }
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                    << "done handling timeouts after handling seqno " << seqno << ", new window is "
                    << SEND_WINDOW_STRING
                    << IBRCOMMON_LOGGER_ENDL;
            }
        }

        void DatagramConnection::handle_ack_timeout(bool last) {
            // timeout value
            struct timespec ts;

            while (true) {
                if (_send_window_frames.empty()) return;

                window_frame &front_frame = _send_window_frames.front();
                unsigned int seqno = front_frame.seqno;
                IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 20)
                    << "ack timeout for seqno " << seqno << " in window "
                    << SEND_WINDOW_STRING
                    << ", " << front_frame.retry << " of " << _params.retry_limit << " retries made"
                    << IBRCOMMON_LOGGER_ENDL;

                // fail -> increment the future timeout
                adjust_rtt(static_cast<double>(_avg_rtt) * 2);

                // retransmit the window
                for (auto &retry_frame : _send_window_frames) {
                    if (retry_frame.retry > _params.retry_limit) {
                        _callback.reportFailure();
                        throw DatagramException("transmission failed (reached retry limit) - abort the stream");
                    } else {
                        _callback.callback_send(*this, retry_frame.flags, retry_frame.seqno, getIdentifier(),
                                                &retry_frame.buf[0], retry_frame.buf.size());
                        retry_frame.retry++;
                    }
                }

                // set timeout to twice the average round-trip-time
                ibrcommon::Conditional::gettimeout(static_cast<size_t>(_avg_rtt * 2) + 1, &ts);

                try {
                    // wait until all frames are flushed after the ack timeout occured
                    while (!_send_window_frames.empty()) {
                        _send_ack_cond.wait(&ts);
                    }
                    IBRCOMMON_LOGGER_DEBUG_TAG(TAG, 30)
                        << "got acks and window is empty => timed-out datagram with seqno " << seqno
                        << " processed"
                        << IBRCOMMON_LOGGER_ENDL;
                    return; // done
                } catch (const ibrcommon::Conditional::ConditionalAbortException &e) {
                    if (e.reason == ibrcommon::Conditional::ConditionalAbortException::COND_TIMEOUT) {
                        continue; // timeout again - repeat at while loop
                    } else {
                        throw;
                    }
                }
            }
        }
    } /* namespace data */
} /* namespace dtn */
