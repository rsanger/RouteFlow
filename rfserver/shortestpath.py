class Path:
    def __init__(self, vm, dp, dest, nh, next_path=None):
        self.vm = vm
        self.dp = dp
        self.dest = dest
        self.nh = nh
        self.send_tag = None
        self.match_tag = dp.next_tag()
        self.next_path = next_path
        self.distance = 1
        if next_path != None:
            self.send_tag = next_path.match_tag
            next_path.prev_paths.add(self)
            self.distance += next_path.distance
        dp.paths[(dest.ct_id, dest.dp_id)] = self
        self.prev_paths = set([])

    # mostly used to delete paths, by setting distance to something
    # effectively infitine
    def set_distance(self, distance):
        self.distance = distance
        for path in children:
            set_distance(distance + 1)

    def delete(self):
        self.next_path.prev_paths.discard(self)
        self.next_path = None
        del self.dp.paths[(dest.ct_id, dest.dp_id)]
        result = [self]
        while self.prev_paths:
            pp = self.prev_paths.pop()
            result = result + pp.delete(self)
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

class Dpdata:
    def __init__(self, vm_id, ct_id, dp_id):
        self.vm_id = vm_id
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.isls = set([])
        self.n_tag = 1
        #paths are keyed by destination
        self.paths = {}

    def path_to(self, ct_id, dp_id):
        result = None
        if (ct_id, dp_id) in self.paths:
            result = self.paths[(ct_id, dp_id)]
        return result

    def next_tag(self):
        result = self.n_tag
        self.n_tag += 1
        return result

    # deletes a path
    # updates children to have effectively infinite distance
    # children are removed one at a time so routemods can be generated to
    # remove them from switches
    def remove_path(self, dest_ct, dest_dp):
        path = self.paths[(dest_ct, dest_id)]
        if path.next_path != None:
            path.parent.children.remove(path)
        path.next_path = None
        path.set_distance(1000)
        for p in path.children:
            p.parent = None
        del self.paths[(dest_ct, dest_dp)]


class RFShortestPaths:
    def __init__(self):
        self.vms = {}

    def isl_up(self, vm_id, ct_id, dp_id, rem_ct, rem_dp):
        if not vm_id in self.vms:
            self.vms[vm_id] = Vmdata(vm_id)
        vm = self.vms[vm_id]

        if not (ct_id, dp_id) in vm.dps:
            vm.dps[(ct_id, dp_id)] = Dpdata(vm_id, ct_id, dp_id)
        dp1 = vm.dps[(ct_id, dp_id)]
        if not (rem_ct, rem_dp) in vm.dps:
            vm.dps[(rem_ct, rem_dp)] = Dpdata(vm_id, ct_id, dp_id)
        dp2 = vm.dps[(rem_ct, rem_dp)]

        dp1.isls.add((rem_ct, rem_dp))
        dp2.isls.add((ct_id, dp_id))

        # delete any existing path between these switches
        if (rem_ct, rem_dp) in dp1.paths:
            dp1.paths[(rem_ct, rem_dp)].delete()
        if (ct_id, dp_id) in dp2.paths:
            dp1.paths[(ct_id, dp_id)].delete()

        # generate new paths
        # Add the two new paths to the list of paths that may be extended.
        # Then check for any paths 
        # the current path you are using
        # if you find a faster path, add all switches that you have ISLs to
        # to the list of unvisited switches with the current switch as the next
        # hop
        # iterate through unvisited until there is nothing left
        # TODO: I might be able to make this faster by only adding the paths
        # that could be updated. Like if the first two are A and B and the only
        # path that B adds is the path to A, it is silly to check all of B's
        # paths to see if they are better than Cs
        patha = Path(vm, dp1, dp2, dp2, None)
        pathb = Path(vm, dp2, dp1, dp1, None)
        
        # paths to add and delete from switches when completed
        padd = []
        pdel = []

        # paths that may be extended
        unvisited = [patha, pathb]

        # find all paths that can be improved between these two switches
        for nhp in dp1.paths.values():
            old = dp2.path_to(nhp.dest.ct_id, nhp.dest.dp_id)
            if old == None or old.distance > nhp.distance + 1:
                if old != None:
                    pdel += old.delete()
                newpath = Path(vm, dp2, nhp.dest, nhp.dp, nhp)
                unvisited.append(newpath)

        # now iterate through the new paths, extending when possible
        while unvisited:
            uv = unvisited.pop(0)
            for rem_ct_id, rem_dp_id in uv.dp.isls:
                rem_dp = vm.dps[(rem_ct_id, rem_dp_id)]
                if rem_dp != uv.nh:
                    old = rem_dp.path_to(uv.dest.ct_id, uv.dest.dp_id)
                    if old == None or old.distance > uv.distance + 1:
                        if old != None:
                            pdel += old.delete()
                        newpath = Path(vm, rem_dp, uv.dest, uv.dp, uv)
                        unvisited.append(newpath)


    def dp_down(self, vm_id, ct_id, dp_id):
        vm = self.vms[vm_id]
        dp = vm.dps[(ct_id, dp_id)]

        #the paths to add and delete from switches when completed
        padd = []
        pdel = []

        distances = {}

        #first delete all paths to this dp
        for rem_ct, rem_dp in dp.isls:
           rem_dp = vm.dps[(rem_ct, rem_dp)]
           for path in rem_dp.paths:
              if path.nh == dp: 
                  # I need the distance, to determine when to start searching
                  # for new links
                  distances[(path.dest.ct_id, path.dest.dp_id)] = path.distance
                  #add path to paths to delete
                  curr_del += rem_dp.remove_path((path.dest_ct, path.dest_dp))

        #find the new paths
        for (dest_ct_id, dest_dp_id), distance in distances:
            dest_dp = vm.dps[(dest_ct_id, dest_dp_id)]
            # the paths that may be extended
            unvisited = []
            for (rem_ct_id, rem_dp_id) in dest_dp.isls:
                rem_dp = vm.dps[(rem_ct_id, rem_dp_id)]
                unvisited.append(rem_dp.path_to(dest_ct_id, dest_dp_id))
            #ok so unvisited now contains all nh paths to destination
            while unvisited:
                uv = unvisited.pop(0)
                # shortest paths, so the new path couldnt be shorter than the
                # existing path
                if uv.distance >= distance - 1:
                    for curr_ct_id, curr_dp_id in uv.dp.isls:
                        curr_dp = vm.dps[(curr_ct_id, curr_dp_id)]
                        # I have deleted all the old paths, so look for dps
                        # without any path to add
                        if curr_dp.path_to(dest_ct_id, dest_dp_id) == None:
                            newpath = Path(curr_dp, uv.dp, dest_dp, uv)
                            padd.append(newpath)
                    uv += list(uv.children)
        #some kind of check that everything has actually been connected up
        #send all the routemods
