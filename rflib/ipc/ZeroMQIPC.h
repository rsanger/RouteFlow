#ifndef __ZEORMQIPC_H__
#define __ZEROMQIPC_H__

#include <boost/thread.hpp>
#include <string>
#include <zmq.hpp>

#include "IPC.h"

/** An IPC message service that uses ZeroIPC as the backend. */
class ZeroMQIPCMessageService : public IPCMessageService {
    public:
        ZeroMQIPCMessageService(const string&serverAddress, const string id, bool bind);

        virtual void listen(const string &channelId, IPCMessageFactory *factory, IPCMessageProcessor *processor, bool block=true);
        virtual bool send(const string &channelId, const string &to, IPCMessage& msg);

    private:
        std::string id;
        zmq::context_t* ctx;
        zmq::socket_t* sender;
        boost::mutex ready;

        void mainWorker(const string &serverAddress, bool bind);
        void subWorker(const string &channelId,
            IPCMessageFactory *factory, IPCMessageProcessor *processor);
};


#endif /* __ZEROMQIPC_H__ */
