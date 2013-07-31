from rflib.defs import IPC_TYPE
from IPC import IPCRole

if IPC_TYPE == 'zeromq':
    from ZeroMQIPC import buildIPC
elif IPC_TYPE == 'mongo':
    from MongoIPC import buildIPC
else:
    import sys
    sys.exit("Unknown IPC_TYPE: {}".format(IPC_TYPE))

"""
In these functions, threading_ is an object with following properties:
    Thread  - same api as threading.Thread,
              created objects need to implement start() and join()
    Event   - same api as threading.Event
              created objects need to implement set() and wait()
    sleep   - same api as time.sleep
"""

class StdThreading(object):
    import threading
    import time

    Thread = staticmethod(threading.Thread)
    Event = staticmethod(threading.Event)
    sleep = staticmethod(time.sleep)
    name = "StdThreading"


def for_server(id_, threading_=StdThreading):
    return buildIPC(IPCRole.server, id_, threading_)

def for_client(id_, threading_=StdThreading):
    return buildIPC(IPCRole.client, id_, threading_)

def for_proxy(id_, threading_=StdThreading):
    return buildIPC(IPCRole.proxy, id_, threading_)


