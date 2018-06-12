/*
 * This file is part of the Flowee project
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "NetworkManager.h"
#include "NetworkManager_p.h"
#include "NetworkService.h"
#include "networkmanager/NetworkEnums.h"
#include  <api/APIProtocol.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include <util.h>

#include <fstream>

// #define DEBUG_CONNECTIONS

static const int RECEIVE_STREAM_SIZE = 41000;
static const int CHUNK_SIZE = 8000;
static const int MAX_MESSAGE_SIZE = 9000;

namespace {

int reconnectTimeoutForStep(short step) {
    if (step < 5)
        return step*step*step / 2;
    return 44;
}

}


NetworkManager::NetworkManager(boost::asio::io_service &service)
    : d(std::make_shared<NetworkManagerPrivate>(service))
{
}

NetworkManager::~NetworkManager()
{
    boost::recursive_mutex::scoped_lock lock(d->mutex);
    d->isClosingDown = true;
    for (auto server : d->servers) {
        server->shutdown();
    }
    for (auto it = d->connections.begin(); it != d->connections.end(); ++it) {
        it->second->shutdown(it->second);
    }
    d->connections.clear(); // invalidate NetworkConnection references
    for (auto service : d->services) {
        service->setManager(nullptr);
    }
    d->services.clear();
}

NetworkConnection NetworkManager::connection(const EndPoint &remote, ConnectionEnum connect)
{
    const bool hasHostname = !remote.ipAddress.is_unspecified() && remote.announcePort > 0;

    if (hasHostname) {
        boost::recursive_mutex::scoped_lock lock(d->mutex);
        for (auto iter1 = d->connections.begin(); iter1 != d->connections.end(); ++iter1) {
            EndPoint endPoint = iter1->second->endPoint();
            if (endPoint.ipAddress != remote.ipAddress)
                continue;
            if (!(endPoint.announcePort == 0 || endPoint.announcePort == remote.announcePort || remote.announcePort == 0))
                continue;
            return NetworkConnection(this, iter1->first);
        }

        if (connect == AutoCreate) {
            EndPoint ep(remote);
            ep.peerPort = ep.announcePort; // outgoing connections always have those the same.
            ep.connectionId = ++d->lastConnectionId;
            d->connections.insert(std::make_pair(ep.connectionId, std::make_shared<NetworkManagerConnection>(d, ep)));
            return NetworkConnection(this, ep.connectionId);
        }
    }
    return NetworkConnection();
}

EndPoint NetworkManager::endPoint(int remoteId) const
{
    boost::recursive_mutex::scoped_lock lock(d->mutex);
    NetworkManagerConnection *con = d->connections.at(remoteId).get();
    if (con)
        return con->endPoint();
    return EndPoint();
}

void NetworkManager::punishNode(int remoteId, int punishment)
{
    d->punishNode(remoteId, punishment);
}

void NetworkManager::bind(tcp::endpoint endpoint, const std::function<void(NetworkConnection&)> &callback)
{
    boost::recursive_mutex::scoped_lock lock(d->mutex);
    try {
        NetworkManagerServer *server = new NetworkManagerServer(d, endpoint, callback);
        d->servers.push_back(server);
    } catch (std::exception &ex) {
        logWarning(Log::NWM) << "Creating NetworkMangerServer failed with" << ex.what();
        throw std::runtime_error("Failed to bind to endpoint");
    }

    if (d->servers.size() == 1) // start cron
        d->cronHourly(boost::system::error_code());
}

void NetworkManager::addService(NetworkService *service)
{
    assert(service);
    if (!service) return;
    boost::recursive_mutex::scoped_lock lock(d->mutex);
    d->services.push_back(service);
    service->setManager(this);
}

void NetworkManager::removeService(NetworkService *service)
{
    assert(service);
    if (!service) return;
    boost::recursive_mutex::scoped_lock lock(d->mutex);
    d->services.remove(service);
    service->setManager(nullptr);
}

void NetworkManager::setAutoApiLogin(bool on, const std::string &cookieFilename)
{
    if (on)
        d->apiCookieFilename = cookieFilename;
    else
        d->apiCookieFilename.clear();
}

std::weak_ptr<NetworkManagerPrivate> NetworkManager::priv()
{
    return d;
}


/////////////////////////////////////


NetworkManagerPrivate::NetworkManagerPrivate(boost::asio::io_service &service)
    : ioService(service),
     lastConnectionId(0),
     isClosingDown(false),
     m_cronHourly(service)
{
}

NetworkManagerPrivate::~NetworkManagerPrivate()
{
}

void NetworkManagerPrivate::punishNode(int connectionId, int punishScore)
{
    boost::recursive_mutex::scoped_lock lock(mutex);
    auto con = connections.find(connectionId);
    if (con == connections.end())
        return;
    con->second->m_punishment += punishScore;

    if (con->second->m_punishment >= 1000) {
        BannedNode bn;
        bn.endPoint = con->second->endPoint();
        bn.banTimeout = boost::posix_time::second_clock::universal_time() + boost::posix_time::hours(24);
        logInfo(Log::NWM) << "Banned node for 24 hours due to excessive bad behavior" << bn.endPoint.hostname;
        banned.push_back(bn);
        connections.erase(connectionId);
        con->second->shutdown(con->second);
    }
}

void NetworkManagerPrivate::cronHourly(const boost::system::error_code &error)
{
    if (error)
        return;

    logDebug(Log::NWM) << "cronHourly";
    boost::recursive_mutex::scoped_lock lock(mutex);
    if (isClosingDown)
        return;

    const auto now = boost::posix_time::second_clock::universal_time();
    std::list<BannedNode>::iterator bannedNode = banned.begin();
    // clean out banned nodes
    while (bannedNode != banned.end()) {
        if (bannedNode->banTimeout < now)
            bannedNode = banned.erase(bannedNode);
        else
            ++bannedNode;
    }
    for (auto connection : connections) {
        connection.second->m_punishment = std::max<short>(0, connection.second->m_punishment - 100);
        // logDebug(Log::NWM) << "peer ban scrore;" << connection.second->m_punishment;
    }
    m_cronHourly.expires_from_now(boost::posix_time::hours(1));
    m_cronHourly.async_wait(std::bind(&NetworkManagerPrivate::cronHourly, this, std::placeholders::_1));
}


/////////////////////////////////////

NetworkManagerConnection::NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::socket socket, int connectionId)
    : m_strand(parent->ioService),
    m_punishment(0),
    d(parent),
    m_socket(std::move(socket)),
    m_resolver(parent->ioService),
    m_messageBytesSend(0),
    m_messageBytesSent(0),
    m_receiveStream(RECEIVE_STREAM_SIZE),
    m_lastCallbackId(1),
    m_isClosingDown(false),
    m_firstPacket(true),
    m_isConnecting(false),
    m_sendingInProgress(false),
    m_acceptedConnection(false),
    m_reconnectStep(0),
    m_reconnectDelay(parent->ioService),
    m_pingTimer(parent->ioService),
    m_chunkedServiceId(-1),
    m_chunkedMessageId(-1)
{
    m_remote.ipAddress = m_socket.remote_endpoint().address();
    m_remote.announcePort = m_socket.remote_endpoint().port();
    m_remote.peerPort = 0;
    m_remote.connectionId = connectionId;
}

NetworkManagerConnection::NetworkManagerConnection(const std::shared_ptr<NetworkManagerPrivate> &parent, const EndPoint &remote)
    : m_strand(parent->ioService),
    m_punishment(0),
    m_remote(std::move(remote)),
    d(parent),
    m_socket(parent->ioService),
    m_resolver(parent->ioService),
    m_messageBytesSend(0),
    m_messageBytesSent(0),
    m_receiveStream(RECEIVE_STREAM_SIZE),
    m_lastCallbackId(1),
    m_isClosingDown(false),
    m_firstPacket(true),
    m_isConnecting(false),
    m_sendingInProgress(false),
    m_acceptedConnection(false),
    m_reconnectStep(0),
    m_reconnectDelay(parent->ioService),
    m_pingTimer(parent->ioService),
    m_chunkedServiceId(-1),
    m_chunkedMessageId(-1)
{
    if (m_remote.peerPort == 0)
        m_remote.peerPort = m_remote.announcePort;
}

void NetworkManagerConnection::connect()
{
    if (m_strand.running_in_this_thread())
        connect_priv();
    else
        runOnStrand(std::bind(&NetworkManagerConnection::connect_priv, this));
}

void NetworkManagerConnection::connect_priv()
{
    assert(m_strand.running_in_this_thread());
    if (m_isConnecting)
        return;
    if (m_isClosingDown)
        return;
    m_isConnecting = true;

    if (m_remote.ipAddress.is_unspecified()) {
        tcp::resolver::query query(m_remote.hostname, boost::lexical_cast<std::string>(m_remote.announcePort));
        m_resolver.async_resolve(query, m_strand.wrap(std::bind(&NetworkManagerConnection::onAddressResolveComplete, this, std::placeholders::_1, std::placeholders::_2)));
    } else {
        m_isConnecting = false;
        if (m_remote.hostname.empty())
            m_remote.hostname = m_remote.ipAddress.to_string();
        boost::asio::ip::tcp::endpoint endpoint(m_remote.ipAddress, m_remote.announcePort);
        m_socket.async_connect(endpoint, m_strand.wrap(
           std::bind(&NetworkManagerConnection::onConnectComplete, this, std::placeholders::_1)));
    }
}

void NetworkManagerConnection::onAddressResolveComplete(const boost::system::error_code &error, tcp::resolver::iterator iterator)
{
    assert(m_strand.running_in_this_thread());
    m_isConnecting = false;
    if (m_isClosingDown)
        return;
    if (error) {
        logDebug(Log::NWM) << "connect;" << error.message();
        m_reconnectDelay.expires_from_now(boost::posix_time::seconds(45));
        m_reconnectDelay.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::reconnectWithCheck, this, std::placeholders::_1)));
        return;
    }
    m_remote.ipAddress = (iterator++)->endpoint().address();

    // Notice that we always only use the first reported DNS entry. Which is likely Ok.
    m_socket.async_connect(*iterator, m_strand.wrap(
       std::bind(&NetworkManagerConnection::onConnectComplete, this, std::placeholders::_1)));
}

void NetworkManagerConnection::onConnectComplete(const boost::system::error_code& error)
{
    if (error.value() == boost::asio::error::operation_aborted)
        return;
    assert(m_strand.running_in_this_thread());
    if (m_isClosingDown)
        return;
    if (error) {
        logDebug(Log::NWM) << "connect;" << error.message();
        if (m_remote.peerPort != m_remote.announcePort) // incoming connection
            return;
        m_reconnectDelay.expires_from_now(boost::posix_time::seconds(reconnectTimeoutForStep(++m_reconnectStep)));
        m_reconnectDelay.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::reconnectWithCheck, this, std::placeholders::_1)));
        return;
    }
    logInfo(Log::NWM) << "Successfully connected to" << m_remote.hostname << m_remote.announcePort;

    if (!d->apiCookieFilename.empty()
                //  outgoing means announce and peerPort are the same.
            && m_remote.announcePort == m_remote.peerPort) {
        // do auto-login to the API server we expect on the other side.
        try {
            std::ifstream is(d->apiCookieFilename);
            std::string content; // we read this every time to avoid the need for restarts
            if (std::getline(is, content)) {
                is.close();
                if (content.size() < 1000) { // protect from memory abuse.
                    m_sendHelperBuffer.reserve(20 + static_cast<int>(content.size()));
                    Streaming::MessageBuilder builder(m_sendHelperBuffer);
                    builder.add(Api::Login::CookieData, content);
                    queueMessage(builder.message(Api::LoginService, Api::Login::LoginMessage), NetworkConnection::HighPriority);
                }
            } else
                logCritical(Log::NWM) << "cookie file empty or not a (text) file" << d->apiCookieFilename;
        } catch (std::exception &e) {
            logCritical(Log::NWM) << "Reading and sending login cookie went wrong;" << e;
        }
    }

    for (auto it = m_onConnectedCallbacks.begin(); it != m_onConnectedCallbacks.end(); ++it) {
        try {
            it->second(m_remote);
        } catch (const std::exception &ex) {
            logWarning(Log::NWM) << "onConnected threw exception, ignoring:" << ex.what();
        }
    }

    // setup a callback for receiving.
    m_receiveStream.reserve(MAX_MESSAGE_SIZE);
    assert(m_receiveStream.capacity() > 0);
    m_socket.async_receive(boost::asio::buffer(m_receiveStream.data(), static_cast<size_t>(m_receiveStream.capacity())),
        m_strand.wrap(std::bind(&NetworkManagerConnection::receivedSomeBytes, this, std::placeholders::_1, std::placeholders::_2)));

    if (m_remote.peerPort == m_remote.announcePort) {
        // for outgoing connections, ping.
        m_pingTimer.expires_from_now(boost::posix_time::seconds(90));
        m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::sendPing, this, std::placeholders::_1)));
    } else {
        // for incoming connections, take action when no ping comes in.
        m_pingTimer.expires_from_now(boost::posix_time::seconds(120));
        m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::pingTimeout, this, std::placeholders::_1)));
    }

    runMessageQueue();
}

Streaming::ConstBuffer NetworkManagerConnection::createHeader(const Message &message)
{
    assert(message.serviceId() >= 0);
    const auto map = message.headerData();
    m_sendHelperBuffer.reserve(10 * static_cast<int>(map.size()));
    Streaming::MessageBuilder builder(m_sendHelperBuffer, Streaming::HeaderOnly);
    auto iter = map.begin();
    while (iter != map.end()) {
        assert(iter->first >= 0);
        builder.add(static_cast<uint32_t>(iter->first), iter->second);
        ++iter;
    }
    builder.add(Network::HeaderEnd, true);
    builder.setMessageSize(m_sendHelperBuffer.size() + message.size());
    logDebug(Log::NWM) << "createHeader of message of length;" << m_sendHelperBuffer.size() << '+' << message.size();
    return builder.buffer();
}

void NetworkManagerConnection::runMessageQueue()
{
    assert(m_strand.running_in_this_thread());
    if (m_sendingInProgress || m_isConnecting || m_isClosingDown || (m_messageQueue.empty() && m_priorityMessageQueue.empty()) || !isConnected())
        return;

    m_sendingInProgress = true;

    /*
     * This method will schedule sending of data.
     * The data to send is pushed async to the network stack and the callback will come in essentially
     * the moment the network stack has accepted the data.  This is not at all any confirmation that
     * the other side accepted it!
     * But at the same time, the network stack has limited buffers and will only push to the network
     * an amount based on the TCP window size. So at minimum we know that the speed with which we
     * send stuff is indicative of the throughput.
     *
     * The idea here is to send a maximum amount of 250KB at a time. Which should be enough to avoid
     * delays. The speed limiter here mean we still allow messages that were pushed to the front of the
     * queue to be handled at a good speed.
     */
    int bytesLeft = 250*1024;
    std::vector<Streaming::ConstBuffer> socketQueue; // the stuff we will send over the socket

    std::list<Message>::iterator iter = m_priorityMessageQueue.begin();
    while (iter != m_priorityMessageQueue.end()) {
        const Message &message = *iter;
        if (!iter->hasHeader()) { // build a simple header
            const Streaming::ConstBuffer constBuf = createHeader(message);
            bytesLeft -= constBuf.size();
            socketQueue.push_back(constBuf);
            m_sendQHeaders.push_back(constBuf);
        }
        socketQueue.push_back(message.rawData());
        bytesLeft -= message.rawData().size();
        m_sentPriorityMessages.push_back(message);
        iter = m_priorityMessageQueue.erase(iter);
        if (bytesLeft <= 0)
            break;
    }
    for (const Message &message : m_messageQueue) {
        if (bytesLeft <= 0)
            break;
        if (message.rawData().size() > CHUNK_SIZE) {
            assert(!message.hasHeader()); // should have been blocked from entering in queueMessage();

            /*
             * The maximum size of a message is 9KB. This helps a lot with memory allocations and zero-copy ;)
             * A large message is then split into smaller ones and send with individual headers
             * to the other side where they can be re-connected.
             */
            Streaming::ConstBuffer body(message.body());
            const char *begin = body.begin() + m_messageBytesSend;
            const char *end = body.end();
            Streaming::MessageBuilder headerBuilder(m_sendHelperBuffer, Streaming::HeaderOnly);
            Streaming::ConstBuffer chunkHeader;// the first and last are different, but all the ones in the middle are duplicates.
            bool first = m_messageBytesSend == 0;
            while (begin < end) {
                const char *p = begin + CHUNK_SIZE;
                if (p > end)
                    p = end;
                m_messageBytesSend += p - begin;
                Streaming::ConstBuffer bodyChunk(body.internal_buffer(), begin, p);
                begin = p;

                Streaming::ConstBuffer header;
                if (first || begin == end || !chunkHeader.isValid()) {
                    m_sendHelperBuffer.reserve(20);
                    headerBuilder.add(Network::ServiceId, message.serviceId());
                    if (message.messageId() >= 0)
                        headerBuilder.add(Network::MessageId, message.messageId());
                    if (first)
                        headerBuilder.add(Network::SequenceStart, body.size());
                    headerBuilder.add(Network::LastInSequence, (begin == end));
                    headerBuilder.add(Network::HeaderEnd, true);
                    headerBuilder.setMessageSize(m_sendHelperBuffer.size() + bodyChunk.size());

                    header = headerBuilder.buffer();
                    if (!first)
                        chunkHeader = header;
                    first = false;
                } else {
                    header = chunkHeader;
                }
                bytesLeft -= header.size();
                socketQueue.push_back(header);
                m_sendQHeaders.push_back(header);

                socketQueue.push_back(bodyChunk);
                bytesLeft -= bodyChunk.size();

                if (bytesLeft <= 0)
                    break;
            }
            if (begin >= end) // done with message.
                m_messageBytesSend = 0;
        }
        else {
            if (!message.hasHeader()) { // build a simple header
                const Streaming::ConstBuffer constBuf = createHeader(message);
                bytesLeft -= constBuf.size();
                socketQueue.push_back(constBuf);
                m_sendQHeaders.push_back(constBuf);
            }
            socketQueue.push_back(message.rawData());
            bytesLeft -= message.rawData().size();
        }
    }
    assert(m_messageBytesSend >= 0);

    async_write(m_socket, socketQueue,
        m_strand.wrap(std::bind(&NetworkManagerConnection::sentSomeBytes, this, std::placeholders::_1, std::placeholders::_2)));
}

void NetworkManagerConnection::sentSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred)
{
    if (error.value() == boost::asio::error::connection_aborted) // assume instance already deleted
        return;

    assert(m_strand.running_in_this_thread());
    m_sendingInProgress = false;
    if (error) {
        logWarning(Log::NWM) << "sent error" << error.message();
        m_messageBytesSend = 0;
        m_messageBytesSent = 0;
        runOnStrand(std::bind(&NetworkManagerConnection::connect, this));
        return;
    }
    logDebug(Log::NWM) << "Managed to send" << bytes_transferred << "bytes";

    m_reconnectStep = 0;
    // now we remove any message data from our lists so the underlying malloced data can be freed
    // This is needed since boost asio unfortunately doesn't keep our ref-counted pointers.
    m_messageBytesSent += bytes_transferred;
    int bytesLeft = m_messageBytesSent;

    // We can have data spread over 3 lists, we eat at them in proper order.
    while (!m_messageQueue.empty() || !m_sentPriorityMessages.empty()) {
        const Message &message = m_sentPriorityMessages.empty() ? m_messageQueue.front() : m_sentPriorityMessages.front();
        const int bodySize = message.rawData().size();
        if (bodySize > CHUNK_SIZE) {
            const std::int64_t chunks = std::lrint(std::ceil(bodySize / static_cast<float>(CHUNK_SIZE)));
            assert(chunks > 0);
            if (m_sendQHeaders.size() < static_cast<size_t>(chunks))
                break;
            std::list<Streaming::ConstBuffer>::iterator iter = m_sendQHeaders.begin();
            for (unsigned int i = 0; i < chunks; ++i) {
                bytesLeft -= iter->size();
                ++iter;
            }
            if (bytesLeft < bodySize)
                break;

            // still here? Then we remove the message and all the headers
            for (unsigned int i = 0; i < chunks; ++i) {
                m_sendQHeaders.erase(m_sendQHeaders.begin());
            }
        }
        else if (!message.hasHeader()) {
            assert(!m_sendQHeaders.empty());
            const int headerSize = m_sendQHeaders.front().size();
            if (headerSize > bytesLeft)
                break;
            if (bodySize > bytesLeft - headerSize)
                break;
            bytesLeft -= headerSize; // bodysize is subtraced below
            m_sendQHeaders.erase(m_sendQHeaders.begin());
        } else {
            if (bodySize > bytesLeft)
                break;
        }
        assert(bodySize <= bytesLeft);
        bytesLeft -= bodySize;
        if (m_sentPriorityMessages.empty())
            m_messageQueue.erase(m_messageQueue.begin());
        else
            m_sentPriorityMessages.erase(m_sentPriorityMessages.begin());
        if (bytesLeft <= 0)
            break;
    }
    m_messageBytesSent = bytesLeft;
    runMessageQueue();
}

void NetworkManagerConnection::receivedSomeBytes(const boost::system::error_code& error, std::size_t bytes_transferred)
{
    if (error.value() == boost::asio::error::connection_aborted) // assume instance already deleted
        return;

    if (m_isClosingDown)
        return;
    assert(m_strand.running_in_this_thread());
    logDebug(Log::NWM) << (static_cast<void*>(this)) << "receivedSomeBytes" << bytes_transferred;
    if (error) {
        logDebug(Log::NWM) << "receivedSomeBytes errored:" << error.message();
        // first copy to avoid problems if a callback removes its callback or closes the connection.
        std::vector<std::function<void(const EndPoint&)> > callbacks;
        callbacks.reserve(m_onDisConnectedCallbacks.size());
        for (auto it = m_onDisConnectedCallbacks.begin(); it != m_onDisConnectedCallbacks.end(); ++it) {
            callbacks.push_back(it->second);
        }

        for (auto callback : callbacks) {
            try {
                callback(m_remote);
            } catch (const std::exception &ex) {
                logWarning(Log::NWM) << "onDisconnected caused exception, ignoring:" << ex;
            }
        }
        close();
        m_firstPacket = true;
        return;
    }
    m_receiveStream.markUsed(static_cast<int>(bytes_transferred)); // move write pointer

    while (true) { // get all packets out
        const size_t blockSize = static_cast<size_t>(m_receiveStream.size());
        if (blockSize < 4) // need more data
            break;
        Streaming::ConstBuffer data = m_receiveStream.createBufferSlice(m_receiveStream.begin(), m_receiveStream.end());

        if (m_firstPacket) {
            m_firstPacket = false;
            if (data.begin()[2] != 8) { // Positive integer (0) and Network::ServiceId (1 << 3)
                logWarning(Log::NWM) << "receive; Data error from server - this is NOT an NWM server. Disconnecting";
                close();
                return;
            }
        }

        const unsigned int rawHeader = *(reinterpret_cast<const unsigned int*>(data.begin()));
        const int packetLength = (rawHeader & 0xFFFF);
        logDebug(Log::NWM) << "Processing incoming packet. Size" << packetLength;
        if (packetLength > MAX_MESSAGE_SIZE) {
            logWarning(Log::NWM) << "receive; Data error from server- stream is corrupt";
            close();
            return;
        }
        if (data.size() < packetLength) // do we have all data for this one?
            break;
        if (!processPacket(m_receiveStream.internal_buffer(), data.begin()))
            return;
        m_receiveStream.forget(packetLength);
    }

    m_receiveStream.reserve(MAX_MESSAGE_SIZE);
    m_socket.async_receive(boost::asio::buffer(m_receiveStream.data(), static_cast<size_t>(m_receiveStream.capacity())),
            m_strand.wrap(std::bind(&NetworkManagerConnection::receivedSomeBytes, this, std::placeholders::_1, std::placeholders::_2)));
}

bool NetworkManagerConnection::processPacket(const std::shared_ptr<char> &buffer, const char *data)
{
    assert(m_strand.running_in_this_thread());
    const unsigned int rawHeader = *(reinterpret_cast<const unsigned int*>(data));
    const int packetLength = (rawHeader & 0xFFFF);
    logDebug(Log::NWM) << "Receive packet length" << packetLength;

    const char *messageStart = data + 2;
    Streaming::MessageParser parser(Streaming::ConstBuffer(buffer, messageStart, messageStart + packetLength));
    Streaming::ParsedType type = parser.next();
    int headerSize = 0;
    int messageId = -1;
    int serviceId = -1;
    int lastInSequence = -1;
    int sequenceSize = -1;
    bool isPing = false;
    // TODO have a variable on the NetworkManger that indicates the maximum allowed combined message-size.

    std::map<int, int> messageHeaderData;
    bool inHeader = true;
    while (inHeader && type == Streaming::FoundTag) {
        switch (parser.tag()) {
        case Network::HeaderEnd:
            headerSize = parser.consumed();
            inHeader = false;
            break;
        case Network::MessageId:
            if (!parser.isInt()) {
                close();
                return false;
            }
            messageId = parser.intData();
            break;
        case Network::ServiceId:
            if (!parser.isInt()) {
                close();
                return false;
            }
            serviceId = parser.intData();
            break;
        case Network::LastInSequence:
            if (!parser.isBool()) {
                close();
                return false;
            }
            lastInSequence = parser.boolData() ? 1 : 0;
            break;
        case Network::SequenceStart:
            if (!parser.isInt()) {
                close();
                return false;
            }
            sequenceSize = parser.intData();
            break;
        case Network::Ping:
            isPing = true;
            break;
        default:
            if (parser.isInt() && parser.tag() < 0xFFFFFF)
                messageHeaderData.insert(std::make_pair(static_cast<int>(parser.tag()), parser.intData()));
            break;
        }

        type = parser.next();
    }
    if (inHeader) {
        logDebug(Log::NWM) << "  header malformed, disconnecting";
        close();
        return false;
    }

    if (serviceId == -1) { // an obligatory field
        logWarning(Log::NWM) << "peer sent message without serviceId";
        close();
        return false;
    }

    if (serviceId == Network::SystemServiceId) { // Handle System level messages
        if (isPing) {
            if (m_pingMessage.rawData().size() == 0) {
                Streaming::MessageBuilder builder(Streaming::HeaderOnly, 10);
                builder.add(Network::ServiceId, Network::SystemServiceId);
                builder.add(Network::Pong, true);
                builder.add(Network::HeaderEnd, true);
                m_pingMessage = builder.message();
            }
            m_messageQueue.push_back(m_pingMessage);
            m_pingTimer.cancel();
            runMessageQueue();
            m_pingTimer.expires_from_now(boost::posix_time::seconds(120));
            m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::pingTimeout, this, std::placeholders::_1)));
        }
        return true;
    }

    Message message;
    // we assume they are in sequence (which is Ok with TCP sockets), but we don't assume that
    // each packet is part of the sequence.
    if (lastInSequence != -1) {
        if (sequenceSize != -1) {
            if (m_chunkedMessageId != -1 || m_chunkedServiceId != -1) { // Didn't finish another. Thats illegal.
                logWarning(Log::NWM) << "peer sent sequenced message with wrong combination of headers";
                close();
                return false;
            }
            m_chunkedMessageId = messageId;
            m_chunkedServiceId = serviceId;
            m_chunkedMessageBuffer = Streaming::BufferPool(sequenceSize);
        }
        else if (m_chunkedMessageId != messageId || m_chunkedServiceId != serviceId) { // Changed. Thats illegal.
            close();
            logWarning(Log::NWM) << "peer sent sequenced message with inconsistent service/messageId";
            return false;
        }
        const int bodyLength = packetLength - headerSize - 2;
        if (m_chunkedMessageBuffer.capacity() < bodyLength) {
            logWarning(Log::NWM) << "peer sent sequenced message with too much data";
            return false;
        }

        logDebug(Log::NWM) << "Message received as part of sequence; last:" << lastInSequence
                 << "total-size:" << sequenceSize;
        std::copy(data + headerSize + 2, data + packetLength, m_chunkedMessageBuffer.data());
        m_chunkedMessageBuffer.markUsed(bodyLength);
        if (lastInSequence == 0)
            return true;

        message = Message(m_chunkedMessageBuffer.commit(), m_chunkedServiceId);
        m_chunkedMessageId = -1;
        m_chunkedServiceId = -1;
        m_chunkedMessageBuffer.clear();
    }
    else {
        message = Message(buffer, data + 2, data + 2 + headerSize, data + packetLength);
    }
    message.setMessageId(messageId);
    message.setServiceId(serviceId);
    for (auto iter = messageHeaderData.begin(); iter != messageHeaderData.end(); ++iter) {
        message.setHeaderInt(iter->first, iter->second);
    }
    message.remote = m_remote.connectionId;

    // first copy to avoid problems if a callback removes its callback or closes the connection.
    std::vector<std::function<void(const Message&)> > callbacks;
    callbacks.reserve(m_onIncomingMessageCallbacks.size());
    for (auto it = m_onIncomingMessageCallbacks.begin(); it != m_onIncomingMessageCallbacks.end(); ++it) {
        callbacks.push_back(it->second);
    }

    for (auto callback : callbacks) {
        try {
            callback(message);
        } catch (const std::exception &ex) {
            logWarning(Log::NWM) << "connection::onIncomingMessage threw exception, ignoring:" << ex;
        }
        if (!m_socket.is_open())
            break;
    }
    std::list<NetworkService*> servicesCopy;
    {
        boost::recursive_mutex::scoped_lock lock(d->mutex);
        servicesCopy = d->services;
    }
    for (auto service : servicesCopy) {
        if (!m_socket.is_open())
            break;
        if (service->id() == serviceId) {
            try {
                service->onIncomingMessage(message, m_remote);
            } catch (const std::exception &ex) {
                logWarning(Log::NWM) << "service::onIncomingMessage threw exception, ignoring:" << ex;
            }
        }
    }

    return m_socket.is_open(); // if the user called disconnect, then stop processing packages
}

void NetworkManagerConnection::addOnConnectedCallback(int id, const std::function<void(const EndPoint&)> &callback)
{
    assert(m_strand.running_in_this_thread());
    m_onConnectedCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::addOnDisconnectedCallback(int id, const std::function<void(const EndPoint&)> &callback)
{
    assert(m_strand.running_in_this_thread());
    m_onDisConnectedCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::addOnIncomingMessageCallback(int id, const std::function<void(const Message&)> &callback)
{
    assert(m_strand.running_in_this_thread());
    m_onIncomingMessageCallbacks.insert(std::make_pair(id, callback));
}

void NetworkManagerConnection::queueMessage(const Message &message, NetworkConnection::MessagePriority priority)
{
    if (!message.hasHeader() && message.serviceId() == -1) {
        logWarning(Log::NWM) << "queueMessage: Can't deliver a message with unset service ID";
#ifdef DEBUG_CONNECTIONS
        assert(false);
#endif
        return;
    }
    if (message.hasHeader() && message.body().size() > CHUNK_SIZE) {
        logWarning(Log::NWM) << "queueMessage: Can't send large message and can't auto-chunk because it already has a header";
#ifdef DEBUG_CONNECTIONS
        assert(false);
#endif
        return;
    }
    if (priority == NetworkConnection::HighPriority && message.rawData().size() > CHUNK_SIZE) {
        logWarning(Log::NWM) << "queueMessage: Can't send large message in the priority queue";
#ifdef DEBUG_CONNECTIONS
        assert(false);
#endif
        return;
    }

    if (m_strand.running_in_this_thread()) {
        if (priority == NetworkConnection::NormalPriority)
            m_messageQueue.push_back(message);
        else
            m_priorityMessageQueue.push_back(message);
        if (isConnected())
            runMessageQueue();
        else
            connect_priv();
    } else {
        runOnStrand(std::bind(&NetworkManagerConnection::queueMessage, this, message, priority));
    }
}

void NetworkManagerConnection::close(bool reconnect)
{
    assert(m_strand.running_in_this_thread());
    if (!isOutgoing()) {
        boost::recursive_mutex::scoped_lock lock(d->mutex);
        shutdown(d->connections.at(m_remote.connectionId));
        d->connections.erase(m_remote.connectionId);
        return;
    }
    m_receiveStream.clear();
    m_sendHelperBuffer.clear();
    m_chunkedMessageBuffer.clear();
    m_messageBytesSend = 0;
    m_messageBytesSent = 0;

    m_priorityMessageQueue.clear();
    m_sentPriorityMessages.clear();
    m_messageQueue.clear();
    m_sendQHeaders.clear();

    m_socket.close();
    m_pingTimer.cancel();
    if (reconnect && !m_isClosingDown) { // auto reconnect.
        if (m_firstPacket) { // this means the network is there, someone is listening. They just don't speak our language.
            // slow down reconnect due to bad peer.
            m_reconnectDelay.expires_from_now(boost::posix_time::seconds(15));
            m_reconnectDelay.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::reconnectWithCheck, this, std::placeholders::_1)));
        } else {
            connect_priv();
        }
    }
}

void NetworkManagerConnection::sendPing(const boost::system::error_code& error)
{
    if (error)
        return;
    logDebug(Log::NWM) << "ping";

    if (m_isClosingDown)
        return;
    assert(m_strand.running_in_this_thread());
    if (!m_socket.is_open())
        return;
    if (m_pingMessage.rawData().size() == 0) {
        Streaming::MessageBuilder builder(Streaming::HeaderOnly, 10);
        builder.add(Network::ServiceId, Network::SystemServiceId);
        builder.add(Network::Ping, true);
        builder.add(Network::HeaderEnd, true);
        m_pingMessage = builder.message();
    }
    m_messageQueue.push_back(m_pingMessage);
    runMessageQueue();

    m_pingTimer.expires_from_now(boost::posix_time::seconds(90));
    m_pingTimer.async_wait(m_strand.wrap(std::bind(&NetworkManagerConnection::sendPing, this, std::placeholders::_1)));
}

void NetworkManagerConnection::pingTimeout(const boost::system::error_code &error)
{
    if (!error) {
        logWarning(Log::NWM) << "Didn't receive a ping from peer for too long, disconnecting dead connection";
        close(false);
    }
}

void NetworkManagerConnection::reconnectWithCheck(const boost::system::error_code& error)
{
    if (!error) {
        m_socket.close();
        connect_priv();
    }
}

int NetworkManagerConnection::nextCallbackId()
{
    return m_lastCallbackId.fetch_add(1);
}

void NetworkManagerConnection::removeAllCallbacksFor(int id)
{
    assert(m_strand.running_in_this_thread());
    m_onConnectedCallbacks.erase(id);
    m_onDisConnectedCallbacks.erase(id);
    m_onIncomingMessageCallbacks.erase(id);
}

void NetworkManagerConnection::shutdown(std::shared_ptr<NetworkManagerConnection> me)
{
    m_isClosingDown = true;
    if (m_strand.running_in_this_thread()) {
        m_onConnectedCallbacks.clear();
        m_onDisConnectedCallbacks.clear();
        m_onIncomingMessageCallbacks.clear();
        if (isConnected())
            m_socket.close();
        m_resolver.cancel();
        m_reconnectDelay.cancel();
        m_strand.post(std::bind(&NetworkManagerConnection::finalShutdown, this, me));
    } else {
        m_strand.post(std::bind(&NetworkManagerConnection::shutdown, this, me));
    }
}

void NetworkManagerConnection::accept()
{
    if (m_acceptedConnection)
        return;
    m_acceptedConnection = true;

    // setup a callback for receiving.
    m_socket.async_receive(boost::asio::buffer(m_receiveStream.data(), static_cast<size_t>(m_receiveStream.capacity())),
        m_strand.wrap(std::bind(&NetworkManagerConnection::receivedSomeBytes, this, std::placeholders::_1, std::placeholders::_2)));
}

void NetworkManagerConnection::runOnStrand(const std::function<void()> &function)
{
    if (m_isClosingDown)
        return;
    m_strand.post(function);
}

void NetworkManagerConnection::punish(int amount)
{
    d->punishNode(m_remote.connectionId, amount);
}

void NetworkManagerConnection::finalShutdown(std::shared_ptr<NetworkManagerConnection>)
{
}

NetworkManagerServer::NetworkManagerServer(const std::shared_ptr<NetworkManagerPrivate> &parent, tcp::endpoint endpoint, const std::function<void(NetworkConnection&)> &callback)
    : d(parent),
      m_acceptor(parent->ioService, endpoint),
      m_socket(parent->ioService),
      onIncomingConnection(callback)
{
    setupCallback();
}

void NetworkManagerServer::shutdown()
{
    m_socket.close();
}

void NetworkManagerServer::setupCallback()
{
    m_acceptor.async_accept(m_socket, std::bind(&NetworkManagerServer::acceptConnection, this, std::placeholders::_1));
}

void NetworkManagerServer::acceptConnection(boost::system::error_code error)
{
    if (error.value() == boost::asio::error::operation_aborted)
        return;
    logDebug(Log::NWM) << "acceptTcpConnection" << error.message();
    if (error) {
        setupCallback();
        return;
    }
    std::shared_ptr<NetworkManagerPrivate> priv = d.lock();
    if (!priv.get())
        return;

    boost::recursive_mutex::scoped_lock lock(priv->mutex);
    if (priv->isClosingDown)
        return;

    const boost::asio::ip::address peerAddress = m_socket.remote_endpoint().address();
    for (const BannedNode &bn : priv->banned) {
        if (bn.endPoint.ipAddress == peerAddress) {
            if (bn.banTimeout > boost::posix_time::second_clock::universal_time()) { // incoming connection is banned.
                logDebug(Log::NWM) << "acceptTcpConnection; closing incoming connection (banned)"
                          << bn.endPoint.hostname;
                m_socket.close();
            }
            setupCallback();
            return;
        }
    }

    const int conId = ++priv->lastConnectionId;
    logDebug(Log::NWM) << "acceptTcpConnection; creating new connection object" << conId;
    std::shared_ptr<NetworkManagerConnection> connection = std::make_shared<NetworkManagerConnection>(priv, std::move(m_socket), conId);
    priv->connections.insert(std::make_pair(conId, connection));
    logDebug(Log::NWM) << "Total connections now;" << priv->connections.size();

    setupCallback(); // only after we std::move socket, to avoid an "Already open" error
    NetworkConnection con(connection, conId);
    onIncomingConnection(con);
}
