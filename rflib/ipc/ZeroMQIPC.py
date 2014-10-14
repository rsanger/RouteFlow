import logging
import struct
import sys

if 'eventlet' in sys.modules:
    from eventlet.green import zmq
    __using_eventlet__ = True
else:
    import zmq
    __using_eventlet__ = False

from rflib.defs import *
import rflib.ipc.IPC as IPC

INTERNAL_SEND_CHANNEL = "inproc://sender"
INTERNAL_PUBLISH_CHANNEL = "inproc://channeler"

_logger = logging.getLogger(__name__)
_handler    = logging.StreamHandler()
_log_format = '%(asctime)s %(name)-12s %(levelname)-8s %(message)s'
_formatter  = logging.Formatter(_log_format, '%b %d %H:%M:%S')
_handler.setFormatter(_formatter)
_logger.addHandler(_handler)
_logger.propagate = 0
_logger.setLevel(logging.INFO)

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

        def handle_external():
            # Copy message from external to publisher... however:
            # message frames are:              <addr>,<channel>,<type>,<msg>
            # these need to be re-shuffled to: <channel>,<addr>,<type>,<msg>
            # This is so that the PUB/SUB mechanism can match the channel.
            parts = external.recv_multipart(copy=False)
            if len(parts) >= 2:
                parts[0], parts[1] = parts[1], parts[0]
                publisher.send_multipart(parts, copy=False)

        def handle_mailbox():
            # Copy messages from mailbox to external. Retry if sending fails,
            # as the external socket may take time to connect.
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

        def loop_external():
            while True:
                handle_external()

        def loop_mailbox():
            while True:
                handle_mailbox()

        self._ready.set()

        if __using_eventlet__:
            # Since all eventlet threads are in the same OS thread, this code
            # can read from external in one eventlet thread, and write to
            # external in another. Furthermore, zmq.Poller isn't supported in
            # eventlet, so this is the easiest way.
            self._threading.Thread(target=loop_external,
                                   name="ipc-main-external").start()
            self._threading.Thread(target=loop_mailbox,
                                   name="ipc-main-mailbox").start()
        else:
            # Since we must read and write external from the same OS thread (a
            # ZMQ constraint), and we read mailbox to write external, this code,
            # running in a single OS thread, uses zmq.Poller to poll both
            # external and mailbox for incoming messages.
            poller = zmq.Poller()
            poller.register(external, zmq.POLLIN)
            poller.register(mailbox, zmq.POLLIN)

            while True:
                ready = dict(poller.poll())

                if external in ready and ready[external] == zmq.POLLIN:
                    handle_external()

                if mailbox in ready and ready[mailbox] == zmq.POLLIN:
                    handle_mailbox()



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
