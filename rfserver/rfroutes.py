from rflib.ipc.RFProtocol import *
from rflib.types.Match import *
from rflib.types.Action import *
from rflib.types.Option import *
from rflib.defs import *

class RouteStore:
    def __init__(self, addr, mask, priority, ct_id, dp_id, outport):
        self.addr = addr
        self.mask = mask
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.priority = priority
        self.outport = outport

    def to_routemod(self):
        rm = RouteMod(RMT_ADD, self.dp_id)
        rm.add_option(Option.CT_ID(self.ct_id))
        rm.add_option(Option.PRIORITY(self.priority))
        rm.add_match(Match.IPV4(self.addr, self.mask))
        return rm


class RFRoutes:
    def __init__(self):
        # contains dicts keyed by (ct_id, dp_id, dp_port)
        # the dicts contain routemods keyed by RFMT_IPV4 values
        # TODO: IPV6
        self.routes = {}

    def new_route(self, rm, ct_id, dp_id, dp_port):
        # TODO: this needs to be more resilient
        addr = None
        mask = None
        print(rm)
        for match in rm.matches:
            if match['type'] == RFMT_IPV4:
                match_ipv4 = Match.from_dict(match)
                addr, mask = match_ipv4.get_value()
        priority = None
        for opt in rm.options:
            if opt['type'] == RFOT_PRIORITY:
                opt_priority = Option.from_dict(opt)
                priority = opt_priority.get_value()

        if mask == None or priority == None:
            print(addr)
            print(mask)
            print(priority)
            return

        key = (ct_id, dp_id, dp_port)
        if not key in self.routes:
            self.routes[(ct_id, dp_id, dp_port)] = {}
        if rm.get_mod() is RMT_DELETE:
            if (addr, mask) in self.routes[key]:
                del self.routes[key][(addr, mask)]
        else:
            self.routes[key][(addr, mask)] = RouteStore(addr, mask, priority,
                                                        ct_id, dp_id, dp_port)

    def delete_route(self, ct_id, dp_id, dp_port, match):
        if match._type == RFMT_IPV4:
            if match.get_value() in self.routes[(ct_id, dp_id, dp_port)]:
                del self.routes[(ct_id, dp_id, dp_port)][match.get_value()]

    def get_routes(self, ct_id, dp_id, dp_port):
        result = []
        if (ct_id, dp_id, dp_port) in self.routes:
            for rs in self.routes[(ct_id, dp_id, dp_port)].values():
                result.append(rs.to_routemod())
        return result

    def get_all_routes(self):
        result = []
        matchroutes = self.routes.values()
        for mr in matchroutes:
            for r in mr.values():
                result.append(r.to_routemod())
        return result 

    # TODO: make this work properly
    def get_routes_to(self, ct_id, dp_id):
        result = []
        for ct, dp, pt in self.routes:
            if ct == ct_id and dp == dp_id:
                for r in self.routes[ct, dp, pt].values():
                    result.append(r.to_routemod())
        return result
