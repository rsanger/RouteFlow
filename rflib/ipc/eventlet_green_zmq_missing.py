"""The :mod:`eventlet_green_zmq_missing` implements parts of the :mod:`pyzmq`
interface that were not handled in :mod:`eventlet.green.zmq`.
"""
from eventlet.green import select
from eventlet.green import zmq

original_zmq_poller = zmq.Poller

class Poller(original_zmq_poller):
    """Green version of :class:`zmq.core.Poller`

    The polling is still performed by the base implementation, but any timeout
    is handled by calling the (now wrapped) green version of
    :func:`select.select`.

    Based on the implementation of Poller from gevent-zmq.
    """

    def poll(self, timeout=None):
        rlist = []
        wlist = []
        xlist = []

        for socket, flags in self.sockets.items():
            if isinstance(socket, zmq.Socket):
                rlist.append(socket.getsockopt(zmq.FD))
                continue
            elif isinstance(socket, int):
                fd = socket
            elif hasattr(socket, 'fileno'):
                try:
                    fd = int(socket.fileno())
                except:
                    raise ValueError('fileno() must return an valid integer fd')
            else:
                raise TypeError('Socket must be a 0MQ socket, an integer fd '
                                'or have a fileno() method: %r' % socket)

            if flags & zmq.POLLIN:
                rlist.append(fd)
            if flags & zmq.POLLOUT:
                wlist.append(fd)
            if flags & zmq.POLLERR:
                xlist.append(fd)

        didSelect = False

        # Loop until timeout or events available
        while True:
            events = super(Poller, self).poll(0)
            if events or timeout == 0 or didSelect:
                return events

            # wait for activity on sockets in a green way
            select.select(rlist, wlist, xlist, timeout)
            didSelect = True

zmq.Poller = Poller
