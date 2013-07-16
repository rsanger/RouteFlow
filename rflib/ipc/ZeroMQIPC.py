import logging
import struct
import sys

if 'eventlet' in sys.modules:
    from eventlet.green import zmq
    from rflib.ipc import eventlet_green_zmq_missing
else:
    import zmq

from rflib.defs import *
import rflib.ipc.IPC as IPC

INTERNAL_SEND_CHANNEL = "inproc://sender"
INTERNAL_PUBLISH_CHANNEL = "inproc://channeler"

_logger = logging.getLogger(__name__)
_logger.setLevel(logging.DEBUG)

class ZeroMQIPCMessageService(IPC.IPCMessageService):
    def __init__(self, address, id_, threading_, bind):
        self._id = id_
        self._threading = threading_
        self._ctx = zmq.Context(1)
        self._sender = self._ctx.socket(zmq.PAIR)
        self._sender.bind(INTERNAL_SEND_CHANNEL)

        self._ready = self._threading.Event()

        worker = self._threading.Thread(target=self._main_worker,
                                        args=(address, bind),
                                        name="ipc-main-worker")
        worker.start()

    def listen(self, channel_id, factory, processor, block=True):
        self._ready.wait()
        worker = self._threading.Thread(target=self._sub_worker,
                                        args=(channel_id, factory, processor),
                                        name=("ipc-channel-" + channel_id + "-worker"))
        worker.start()
        if block:
            worker.join()

    def send(self, channel_id, to, msg):
        _logger.debug('send on ch %s to %s:\n%s', channel_id, to, str(msg))
        self._sender.send(to, zmq.SNDMORE)
        self._sender.send(channel_id, zmq.SNDMORE)
        self._sender.send(struct.pack('B', msg.get_type()), zmq.SNDMORE)
        self._sender.send(msg.to_bson())
        return True

    def _main_worker(self, address, bind):
        external = self._ctx.socket(zmq.ROUTER)
        external.identity = self._id

        if bind:
            external.bind(address)
        else:
            external.connect(address)
            external.router_behavior = 1    # ZMQ_ROUTER_MANDATORY, but pyzmq
                                            # doesn't have newer option name

        # The mailbox will receive messages to send to the external socket.
        mailbox = self._ctx.socket(zmq.PAIR)
        mailbox.connect(INTERNAL_SEND_CHANNEL)

        # The publisher will publish messages received from the external socket
        publisher = self._ctx.socket(zmq.PUB)
        publisher.bind(INTERNAL_PUBLISH_CHANNEL)

        self._ready.set()

        # Now, poll both external and sender, and forward messages
        poller = zmq.Poller()
        poller.register(external, zmq.POLLIN)
        poller.register(mailbox, zmq.POLLIN)

        while True:
            ready = dict(poller.poll())

            if external in ready and ready[external] == zmq.POLLIN:
                # copy message from external to publisher... however:
                # message frames are:              <addr>,<channel>,<type>,<msg>
                # these need to be re-shuffled to: <channel>,<addr>,<type>,<msg>
                # This is so that the PUB/SUB mechanism can match the channel.
                parts = external.recv_multipart(copy=False)
                if len(parts) >= 2:
                    parts[0], parts[1] = parts[1], parts[0]
                    publisher.send_multipart(parts, copy=False)

            if mailbox in ready and ready[mailbox] == zmq.POLLIN:
                parts = mailbox.recv_multipart(copy=False)
                retry = False
                for attempt in xrange(1 if bind else 30, 0, -1):
                    try:
                        external.send_multipart(parts, copy=False)
                        if retry:
                            _logger.warning("external send succeeded")
                        break
                    except zmq.ZMQError as e:
                        if e.errno == zmq.EHOSTUNREACH:
                            if attempt > 1:
                                _logger.warning("external send failed, sleeping")
                                self._threading.sleep(0.5)
                                retry = True
                            else:
                                _logger.error("external send failed, gave up")
                                pass # dropped unroutable message
                        else:
                            raise


    def _sub_worker(self, channel_id, factory, processor):
        subscriber = self._ctx.socket(zmq.SUB)
        subscriber.subscribe = channel_id
        subscriber.connect(INTERNAL_PUBLISH_CHANNEL)
        _logger.debug('subscribing to ch %s', channel_id)

        while True:
            parts = subscriber.recv_multipart()
            if len(parts) != 4:
                continue

            (channel, addr, type_, payload) = parts
            msg = factory.build_for_type(struct.unpack('B', type_)[0])
            msg.from_bson(payload)
            _logger.debug('receive on ch %s from %s:\n%s', channel, addr, str(msg))
            processor.process(addr, self._id, channel, msg)


def buildIPC(role, id_, threading_):
    return ZeroMQIPCMessageService(ZEROMQ_ADDRESS, id_,
                                   threading_, role == IPC.IPCRole.server)
