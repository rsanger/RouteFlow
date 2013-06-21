from rflib.ipc.RFProtocol import *
from rflib.types.Match import *
from rflib.defs import *

class RFRoutes:
    def __init__(self):
        # contains dicts keyed by vm_id
        # the dicts contain routemods keyed by RFMT_IPV4 values
        # TODO: IPV6
        self.routes = {}

    def new_route(self, rm, ct_id, dp_id, dp_port):
        # TODO: this needs to be more resilient
        match = Match.from_dict(rm.get_matches()[0])
        key = (ct_id, dp_id, dp_port)
        if match._type != RFMT_IPV4:
            # some kind of error here?
            return
            if not key in self.routes:
                self.routes[(ct_id, dp_id, dp_port)] = {}
            if rm.get_mod() is RMT_DELETE:
                if match.get_value() in self.routes[key]:
                    del self.routes[key][match.get_value()]
            else:
                self.routes[key][match.get_value()] = rm

    def delete_route(self, ct_id, dp_id, dp_port, match):
        if match._type == RFMT_IPV4:
            if match.get_value() in self.routes[(ct_id, dp_id, dp_port)]:
                del self.routes[(ct_id, dp_id, dp_port)][match.get_value()]

    def get_routes(self, ct_id, dp_id, dp_port):
        result = self.routes[(ct_id, dp_id, dp_port)].values()
        return result

    # TODO: make this work properly
    def get_routes_to(self, ct_id, dp_id):
        result = []
        for ct, dp, pt in self.routes:
            if ct == ct_id and dp == dp_id:
                result.append(self.routes[ct, dp, pt])
        return result
