from rflib.ipc.RFProtocol import *
from rflib.ipc.RFProtocolFactory import RFProtocolFactory

from rflib.types.Match import *
from rflib.types.Action import *
from rflib.types.Option import *


class Path:
    def __init__(self, vm, dp, dest, nh, parent=None):
        self.vm = vm
        self.dp = dp
        self.dest = dest
        self.nh = nh
        self.send_tag = None
        self.match_tag = dp.next_tag()
        self.outport, self.eth_src = dp.isls[(nh.ct_id, nh.dp_id)]
        x, self.eth_dst = nh.isls[(dp.ct_id, dp.dp_id)]
        self.parent = parent
        self.distance = 1
        if parent != None:
            self.send_tag = parent.match_tag
            parent.children.add(self)
            self.distance += parent.distance
        dp.paths[(dest.ct_id, dest.dp_id)] = self
        self.children = set([])
        self.added = False

    # mostly used to delete paths, by setting distance to something
    # effectively infitine
    def set_distance(self, distance):
        self.distance = distance
        for path in children:
            set_distance(distance + 1)

    def delete(self):
        result = [self]
        if self.parent != None:
            self.parent.children.discard(self)
            self.parent = None
        del self.dp.paths[(self.dest.ct_id, self.dest.dp_id)]
        while self.children:
            child = self.children.pop()
            result = result + child.delete()
        return result

    def __eq__(self, other):
        return ((type(other) is type(self))\
            and self.dp == other.dp
            and self.dest == other.dest)

    def __hash__(self):
        return hash((self.dp.ct_id, self.dp.dp_id, 
                      self.dest.ct_id, self.dest.dp_id))

    def __str__(self):
      return "ct id: {0.dp.ct_id} dp id: {0.dp.dp_id} dst ct: {0.dest.ct_id} "\
             "dst dp id: {0.dest.dp_id} next hop ct id: {0.nh.ct_id} "\
             "next hop dp id: {0.nh.dp_id} distance: {0.distance} push tag: "\
             "{0.send_tag} match tag: {0.match_tag}".format(self)


class Vmdata:
    def __init__(self, vm_id):
        self.vm_id = vm_id
        self.dps = {}
        self.root_dp = None

class Dpdata:
    def __init__(self, vm_id, ct_id, dp_id):
        self.vm_id = vm_id
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.macs = {}
        #key is connected dp ct_id and dp_id, value is port
        self.isls = {} 
        self.n_tag = 1
        #paths are keyed by destination
        self.paths = {}
        self.up = False 

    def path_to(self, ct_id, dp_id):
        result = None
        if (ct_id, dp_id) in self.paths:
            result = self.paths[(ct_id, dp_id)]
        return result

    def next_tag(self):
        result = self.n_tag
        self.n_tag += 1
        return result


class RFShortestPaths:
    def __init__(self):
        self.vms = {}

    def isl_up(self, vm_id, ct_id1, dp_id1, port1, mac1, ct_id2, dp_id2,
                port2, mac2):
        if not vm_id in self.vms:
            self.vms[vm_id] = Vmdata(vm_id)
            
        vm = self.vms[vm_id]

        if not (ct_id1, dp_id1) in vm.dps:
            vm.dps[(ct_id1, dp_id1)] = Dpdata(vm_id, ct_id1, dp_id1)
        dp1 = vm.dps[(ct_id1, dp_id1)]
        if not (ct_id2, dp_id2) in vm.dps:
            vm.dps[(ct_id2, dp_id2)] = Dpdata(vm_id, ct_id2, dp_id2)
        dp2 = vm.dps[(ct_id2, dp_id2)]

        # Determine if there is a connection between these dps and the root dp
        # If there is no root dp for this vm, then set dp1 as root dp
        if vm.root_dp == None:
            vm.root_dp = dp1
            dp1.up = True
            dp2.up = True
        elif dp1.up:
            dp2.up = True
        elif dp2.up:
            dp1.up = True

        dp1.isls[(ct_id2, dp_id2)] = (port1, mac1)
        dp2.isls[(ct_id1, dp_id1)] = (port2, mac2)

        # paths to add and delete from switches when completed
        padd = set([])
        pdel = []

        # delete any existing path between these switches
        if (ct_id2, dp_id2) in dp1.paths:
            pdel +=  dp1.paths[(ct_id2, dp_id2)].delete()
        if (ct_id1, dp_id1) in dp2.paths:
            pdel += dp2.paths[(ct_id1, dp_id1)].delete()

        # generate new paths
        # Add the two new paths to the list of paths that may be extended.
        # check each of the two dps for paths that can be improved
        # add any improved paths to unvisited
        # check all isls of that path.dp for a path that may be
        # improved, if found, add it to unvisited
        # because paths are removed from the head and added to the tail,
        # paths will be visited in order they are created, IE from shortest
        # to longest. Therefore a new path will never be faster than an
        # already created new path
        # iterate through unvisited until there is nothing left
        patha = Path(vm, dp1, dp2, dp2, None)
        pathb = Path(vm, dp2, dp1, dp1, None)
        padd.update([patha, pathb])
        
        # paths that may be extended
        unvisited = [patha, pathb]

        # find all paths that can be improved between these two switches
        for nhp in dp1.paths.values():
            if nhp.dest != dp2:
                old = dp2.path_to(nhp.dest.ct_id, nhp.dest.dp_id)
                if old == None or old.distance > nhp.distance + 1:
                    if old != None:
                        pdel += old.delete()
                    newpath = Path(vm, dp2, nhp.dest, nhp.dp, nhp)
                    padd.add(newpath)
                    unvisited.append(newpath)
        for nhp in dp2.paths.values():
            if nhp.dest != dp1:
                old = dp1.path_to(nhp.dest.ct_id, nhp.dest.dp_id)
                if old == None or old.distance > nhp.distance + 1:
                    if old != None:
                        pdel += old.delete()
                    newpath = Path(vm, dp1, nhp.dest, nhp.dp, nhp)
                    padd.add(newpath)
                    unvisited.append(newpath)

        # now iterate through the new paths, extending when possible
        while unvisited:
            uv = unvisited.pop(0)
            for curr_ct_id, curr_dp_id in uv.dp.isls.keys():
                curr_dp = vm.dps[(curr_ct_id, curr_dp_id)]
                if curr_dp != uv.dest:
                    old = curr_dp.path_to(uv.dest.ct_id, uv.dest.dp_id)
                    if old == None or old.distance > uv.distance + 1:
                        if old != None:
                            pdel += old.delete()
                        newpath = Path(vm, curr_dp, uv.dest, uv.dp, uv)
                        padd.discard(newpath)
                        padd.add(newpath)
                        unvisited.append(newpath)

        return (padd, pdel)


    def dp_down(self, vm_id, ct_id, dp_id):
        vm = self.vms[vm_id]
        dp = vm.dps[(ct_id, dp_id)]

        # Check if this is the last dp up for this VM
        dp.up = False
        if vm.root_dp == dp:
            for rdp in vm.dps.values():
                if rdp.up:
                    vm.root_dp = rdp

        #the paths to add and delete from switches when completed
        padd = set([])
        pdel = set([])

        distances = {}

        # delete all isls and all paths to the dp
        for rem_ct_id, rem_dp_id in dp.isls.keys():
            rem_dp = vm.dps[(rem_ct_id, rem_dp_id)]
            for path in rem_dp.paths.values():
                if path.dest == dp:
                    pdel.update(path.delete())
            del rem_dp.isls[(ct_id, dp_id)]

        dp.isls.clear()

        # delete all paths through this dp, keep distances because new paths
        # must be at least as long as the path deleted
        for path in dp.paths.values():
            distances[(path.dest.ct_id, path.dest.dp_id)] = path.distance
            pdel.update(path.delete())

        #find the new paths
        for (dest_ct_id, dest_dp_id) in distances.keys():
            distance = distances[(dest_ct_id, dest_dp_id)]
            dest_dp = vm.dps[(dest_ct_id, dest_dp_id)]
            # the paths that may be extended
            unvisited = []
            # add the shortest paths to each destination
            for (rem_ct_id, rem_dp_id) in dest_dp.isls.keys():
                rem_dp = vm.dps[(rem_ct_id, rem_dp_id)]
                unvisited.append(rem_dp.path_to(dest_ct_id, dest_dp_id))

            while unvisited:
                uv = unvisited.pop(0)
                unvisited += list(uv.children)
                # shortest paths, so the new path couldnt be shorter than the
                # existing path
                if uv.distance < distance:
                    continue

                for curr_ct_id, curr_dp_id in uv.dp.isls.keys():
                    curr_dp = vm.dps[(curr_ct_id, curr_dp_id)]
                    if curr_dp != uv.dest:
                        old = curr_dp.path_to(dest_ct_id, dest_dp_id)
                        # all old paths have been deleted, and paths will be
                        # generated in order of shortest to longest, therefore
                        # only empty paths need to be replaced
                        if old == None:
                            newpath = Path(vm_id, curr_dp, dest_dp, uv.dp, uv)
                            padd.discard(newpath)
                            padd.add(newpath)
                            unvisited.append(newpath)

        # dont send routemods to the switch that just got taken down
        pdontdel = set([])
        for pd in pdel:
            if pd.dp == dp:
                pdontdel.add(pd)

        pdel -= pdontdel
        return (padd, pdel - pdontdel)

    def dp_is_down(self, vm_id, ct_id, dp_id):
        result = False
        if vm_id in self.vms:
            if (ct_id, dp_id) in self.vms[vm_id].dps:
                dp = self.vms[vm_id].dps[ct_id, dp_id]
                if not dp.up:
                    result = True
        return result

    def paths_to_routemod(self, paths, del_bool):
        rms = []
        for path in paths:
            if del_bool and path.added:
                rm = RouteMod(RMT_DELETE, path.dp.dp_id)
            elif del_bool:
                # if this is a path that is being deleted but has not been
                # added, then there is no need to delete it
                continue
            else:
                rm = RouteMod(RMT_ADD, path.dp.dp_id)
                path.added = True
            rm.add_option(Option.CT_ID(path.dp.ct_id))
            rm.add_option(Option.PRIORITY(PRIORITY_HIGHEST))
            #TODO: This needs to differentiate MPLS packets received via ISLs
            #      and MPLS packets received from external ports
            #      that may mean that every time an ISL comes up I need to send
            #      new routemods with new matches. Though assuming of1.2 I can
            #      cope with this I think.
            #      This could be fixed nicely with tables, when multiple table
            #      support is added
            rm.add_match(Match.MPLS(path.match_tag))
            rm.add_action(Action.OUTPUT(path.outport))
            if path.send_tag == None:
                rm.add_action(Action.POP_MPLS())
            else:
                rm.add_action(Action.SWAP_MPLS(path.send_tag))
            rms.append(rm)
        return rms
