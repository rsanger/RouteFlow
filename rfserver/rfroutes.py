import threading
from rflib.ipc.RFProtocol import *
from rflib.types.Match import *
from rflib.defs import *

class RFRoutes:
    def __init__(self):
        self.routes = {}
        self.routeslock = threading.Lock()

    def new_route(self, rm):
        vm_id = rm.get_id()
        match = Match.from_dict(rm.get_matches()[0])
        if match._type != RFMT_IPV4:
            # some kind of error here?
            return
        with self.routeslock:
            if not vm_id in self.routes:
                self.routes[vm_id] = {}
            if rm.get_mod() is RMT_DELETE:
                if match.get_value() in self.routes[vm_id]:
                    del self.routes[vm_id][match.get_value()]
            else:
                self.routes[vm_id][match.get_value()] = rm

    def delete_route(self, vm_id, match):
        with self.routeslock:
            if match._type == RFMT_IPV4:
                if match.get_value() in self.routes[vm_id]:
                    del self.routes[vm_id][match.get_value()]

    def get_routes(self, vm_id):
        with self.routeslock:
            result = self.routes[vm_id].values()
        return result
