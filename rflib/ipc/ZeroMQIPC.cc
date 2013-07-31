#include "ZeroMQIPC.h"

#include <iostream>

#include <boost/lambda/bind.hpp>
#include <mongo/bson/bson.h>

#include "defs.h"

#define INTERNAL_SEND_CHANNEL "inproc://sender"
#define INTERNAL_PUBLISH_CHANNEL "inproc://channeler"

#ifndef DEBUG
#define DEBUG false
#endif

static void messageString(zmq::message_t &msg, const string &str) {
    size_t len = str.size();
    msg.rebuild(len);
    memcpy(msg.data(), str.data(), len);
}

static void noteZmqError(const zmq::error_t &err) {
    std::cerr << "ZMQ error: " << err.what() << std::endl << std::flush;
}

template <class T>
static void noteZmqError(T prefix, const zmq::error_t &err) {
    std::cerr << '[' << prefix << "] ";
    noteZmqError(err);
}

template <class T1, class T2>
static void noteZmqError(T1 p1, T2 p2, const zmq::error_t &err) {
    std::cerr << '[' << p1 << ' ' << p2 << "] ";
    noteZmqError(err);
}

/** This worker manages the external socket. It proxies messages to send out
the external socket, and external messages are proxied into the internal pub-sub
system. We need to do this extra layer because each socket in ZMQ must be
created and managed by exactly one thread. Since the usage model of IPC is that
the main thread sends messages, but listening threads handle incoming messages,
the actual external socket must be managed by yet its own thread. */
void ZeroMQIPCMessageService::mainWorker(
        const string &serverAddress, bool bind) {
    zmq::socket_t external(*ctx, ZMQ_ROUTER);
    external.setsockopt(ZMQ_IDENTITY, id.data(), id.length());

    if (bind) {
        try {
            external.bind(serverAddress.c_str());
        } catch (const zmq::error_t &err) {
            noteZmqError("mainWorker external.bind", serverAddress, err);
            exit(1);
        }
    } else {
        try {
            const int one = 1;
            external.connect(serverAddress.c_str());
            external.setsockopt(ZMQ_ROUTER_MANDATORY, &one, sizeof(one));
        } catch (const zmq::error_t &err) {
            noteZmqError("mainWorker external.connect", serverAddress, err);
            exit(1);
        }
    }

    /* The mailbox will receive messages to send to the external socket. */
    zmq::socket_t mailbox(*ctx, ZMQ_PAIR);
    try {
        mailbox.connect(INTERNAL_SEND_CHANNEL);
    } catch (const zmq::error_t &err) {
        noteZmqError("mainWorker mailbox.connect", INTERNAL_SEND_CHANNEL, err);
    }

    /* The publisher will publish messages received from the external socket */
    zmq::socket_t publisher(*ctx, ZMQ_PUB);
    try {
        publisher.bind(INTERNAL_PUBLISH_CHANNEL);
    } catch (const zmq::error_t &err) {
        noteZmqError("mainWorker publisher.bind", INTERNAL_PUBLISH_CHANNEL, err);
    }

    ready.unlock();

    /* Now, poll both external and sender, and forward messages */
    zmq::pollitem_t items[] = {
        { external, 0, ZMQ_POLLIN, 0 },
        { mailbox, 0, ZMQ_POLLIN, 0 }
    };

    while (true) {
        try {
            zmq::poll(items, sizeof(items)/sizeof(items[0]), -1);

            if (items[0].revents & ZMQ_POLLIN) {
                // copy message from external to publisher... however:
                // message frames are:              <addr>,<channel>,<type>,<message>
                // these need to be re-shuffled to: <channel>,<addr>,<type>,<message>
                // This is so that the PUB?SUB mechanism can select on the channel.
                zmq::message_t addrFrame;
                zmq::message_t channelFrame;

                external.recv(&addrFrame);
                if (!addrFrame.more()) continue;

                external.recv(&channelFrame);
                bool more = channelFrame.more();

                publisher.send(channelFrame, ZMQ_SNDMORE);
                publisher.send(addrFrame, more ? ZMQ_SNDMORE : 0);

                while (more) {
                    zmq::message_t dataFrame;
                    external.recv(&dataFrame);
                    more = dataFrame.more();
                    publisher.send(dataFrame, more ? ZMQ_SNDMORE : 0);
                }
            }

            if (items[1].revents & ZMQ_POLLIN) {
                // copy message from mailbox to external.
                zmq::message_t address;
                zmq::message_t channel;
                zmq::message_t type;
                zmq::message_t payload;

                do {
                    mailbox.recv(&address);     if (!address.more()) break;
                    mailbox.recv(&channel);     if (!channel.more()) break;
                    mailbox.recv(&type);        if (!type.more()) break;
                    mailbox.recv(&payload);
                    if (payload.more()) {
                        for (bool more = true; more;) {
                            zmq::message_t extra;
                            mailbox.recv(&extra);
                            more = extra.more();
                        }
                        break;
                    }

                    bool retry = false;
                    for (int attempt = bind ? 1 : 30; attempt > 0; --attempt) {
                        try {
                            external.send(address, ZMQ_SNDMORE);
                            external.send(channel, ZMQ_SNDMORE);
                            external.send(type, ZMQ_SNDMORE);
                            external.send(payload);
                            if (retry) {
                                std::cerr << "[ZMQIPC mainWorker external.forward] succeeded" << std::endl;
                            }
                            break;
                        } catch (const zmq::error_t &err) {
                            noteZmqError("mainWorker external.forward", err);
                            if (attempt > 1 && err.num() == EHOSTUNREACH) {
                                std::cerr << "[ZMQIPC mainWorker external.forward], sleeping" << std::endl;
                                usleep(500*1000);
                                retry = true;
                            } else {
                                std::cerr << "[ZMQIPC mainWorker external.forward], gave up" << std::endl;
                                break;
                            }
                        }
                    }
                } while (false);
            }
        } catch (const zmq::error_t &err) {
            noteZmqError("mainWorker", err);
            throw;
        }
    }
}

/** This worker subscribes to a given channel, and builds and processes messages
that it receives. */
void ZeroMQIPCMessageService::subWorker(const string &channelId,
        IPCMessageFactory *factory, IPCMessageProcessor *processor) {

    {
        // ensure the mainWorker is ready before proceeding
        boost::lock_guard<boost::mutex> lock(ready);
    }

    zmq::socket_t subscriber(*ctx, ZMQ_SUB);
    try {

        subscriber.setsockopt(ZMQ_SUBSCRIBE, channelId.data(), channelId.length());
        subscriber.connect(INTERNAL_PUBLISH_CHANNEL);
    } catch (const zmq::error_t &err) {
        noteZmqError("subWorker subscriber.connect", INTERNAL_PUBLISH_CHANNEL, err);
        exit(2);
    }

    while (true) {
        try {
            zmq::message_t channel;
            zmq::message_t addr;
            zmq::message_t type;
            zmq::message_t payload;

            subscriber.recv(&channel);
            if (!channel.more()) continue;

            subscriber.recv(&addr);
            if (!addr.more()) continue;

            subscriber.recv(&type);
            if (!type.more()) continue;

            subscriber.recv(&payload);
            bool more = payload.more();

            while (more) {
                zmq::message_t extra;
                subscriber.recv(&extra);
                more = extra.more();
            }

            if (type.size() == 1) {
                IPCMessage *msg = factory->buildForType(static_cast<const char*>(type.data())[0]);
                msg->from_BSON(static_cast<const char*>(payload.data()));

                if (DEBUG) {
                    std::string ch(static_cast<const char*>(channel.data()), channel.size());
                    std::string from(static_cast<const char*>(addr.data()), addr.size());

                    std::cerr << "[ZMQIPC subWorker subscribe.recv] " << id
                        << " ch " << ch << " from " << from
                        << std::endl << msg->str();
                }

                processor->process(static_cast<const char*>(addr.data()), channelId, static_cast<const char*>(channel.data()), *msg);
                delete msg;
            }
        } catch (const zmq::error_t &err) {
            noteZmqError("subWorker", err);
        }
    }
}


ZeroMQIPCMessageService::ZeroMQIPCMessageService(
        const string& serverAddress, const string _id, bool bind) : id(_id) {
    string address = serverAddress.size() > 0 ? serverAddress : ZEROMQ_ADDRESS;

    ready.lock();

    ctx = new zmq::context_t(1);

    try {
        // the first socket of a pair must be bound first, and since the other end
        // is in a thread, we don't know when that will actually get created.
        this->sender = new zmq::socket_t(*this->ctx, ZMQ_PAIR);
        this->sender->bind(INTERNAL_SEND_CHANNEL);
    } catch (const zmq::error_t &err) {
        noteZmqError("ZeroMQIPCMessageService::ctor sender.bind", INTERNAL_SEND_CHANNEL, err);
        exit(3);
    }

    boost::thread t(boost::bind(&ZeroMQIPCMessageService::mainWorker, this,
                                address, bind));
    t.detach();
}


void ZeroMQIPCMessageService::listen(const string &channelId, IPCMessageFactory *factory, IPCMessageProcessor *processor, bool block) {
    boost::thread t(boost::bind(&ZeroMQIPCMessageService::subWorker, this,
                                channelId, factory, processor));
    if (block)
        t.join();
    else
        t.detach();
}

bool ZeroMQIPCMessageService::send(const string &channelId, const string &to, IPCMessage& msg) {
    try {
        if (DEBUG) {
            std::cerr << "[ZMQIPC send] " << id
                << "ch " << channelId << " to " << to
                << std::endl << msg.str();
        }

        zmq::message_t destination;
        messageString(destination, to);

        zmq::message_t channel;
        messageString(channel, channelId);

        zmq::message_t type(1);
        static_cast<char *>(type.data())[0] = (char)msg.get_type();

        const char* msgBSONBytes = msg.to_BSON();
        size_t msgBSONSize = mongo::BSONObj(msgBSONBytes).objsize();
        zmq::message_t payload(msgBSONSize);
        memcpy(payload.data(), msgBSONBytes, msgBSONSize);
        delete msgBSONBytes;

        this->sender->send(destination, ZMQ_SNDMORE);
        this->sender->send(channel, ZMQ_SNDMORE);
        this->sender->send(type, ZMQ_SNDMORE);
        this->sender->send(payload);
    } catch (const zmq::error_t &err) {
        noteZmqError("send", err);
        return false;
    }

    return true;
}


IPCMessageService* IPCMessageServiceFactory::forServer(const string& server, const string& id) {
    return new ZeroMQIPCMessageService(server, id, true);
}
IPCMessageService* IPCMessageServiceFactory::forClient(const string& server, const string& id) {
    return new ZeroMQIPCMessageService(server, id, false);
}
IPCMessageService* IPCMessageServiceFactory::forProxy(const string& server, const string& id) {
    return new ZeroMQIPCMessageService(server, id, false);
}

