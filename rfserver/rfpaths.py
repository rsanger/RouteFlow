class Path:
    def __init__(self, ct_id=None, dp_id=None, dest_ct=None, dest_dp=None,
                 nh_ct=None, nh_dp=None, distance=None, send_tag=None,
                 match_tag=None):
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.dest_ct = dest_ct
        self.dest_dp = dest_dp
        self.nh_ct = nh_ct
        self.nh_dp = nh_dp
        self.distance = distance
        self.send_tag = send_tag
        self.match_tag = match_tag
        
    def __eq__(self, other):
        return ((type(other) is type(self))
            and self.ct_id == other.ct_id
            and self.dp_id == other.dp_id
            and self.dest_ct == other.dest_ct
            and self.dest_dp == other.dest_dp)

    def __hash__(self):
        return hash((self.ct_id, self.dp_id, self.dest_ct, self.dest_dp))

    def __str__(self):
      return "ct id: {0.ct_id} dp id: {0.dp_id} dest ct: {0.dest_ct} "\
             "dest dp id: {0.dest_dp} next hop ct id: {0.nh_ct} "\
             "next hop dp id: {0.nh_dp} distance: {0.distance} push tag: "\
             "{0.send_tag} match tag: {0.match_tag}".format(self)



class RFPaths(dict):
    def __init__(self):
        self.pathstore_by_vm = {}

    def get_paths_by_dp(self, vm_id, *args, **kwargs):
        return self.pathstore_by_vm[vm_id].get_paths_by_dp(*args, **kwargs)

    def get_path_from_to(self, vm_id, *args, **kwargs):
        return self.pathstore_by_vm[vm_id].get_path_from_to(*args, **kwargs)

    def get_isls(self, vm_id, ct_id, dp_id):
        return self.pathstore_by_vm[vm_id].get_isls(ct_id, dp_id)

    def new_nh_path(self, vm_id, *args, **kwargs):
        if not vm_id in self.pathstore_by_vm:
            self.pathstore_by_vm[vm_id] = Path_Storage()
        return self.pathstore_by_vm[vm_id].new_nh_path(*args, **kwargs)

    def new_path(self, vm_id, *args, **kwargs):
        if not vm_id in self.pathstore_by_vm:
            self.pathstore_by_vm[vm_id] = Path_Storage()
        return self.pathstore_by_vm[vm_id].new_path(*args, **kwargs)

    def discard(self, vm_id, path):
        self.pathstore_by_vm[vm_id].discard(path)

class Path_Storage:
    def __init__(self):
        self.paths_by_dp = {}
        self.paths_by_nh = {}
        self.paths_from_to = {}
        self.match_tags_by_dp = {}
        self.isls = {}

    def get_isls(self, ct_id, dp_id):
        return self.isls[(ct_id, dp_id)]

    def get_paths_by_dp(self, ct_id, dp_id):
        result = None
        if (ct_id, dp_id) in self.paths_by_dp:
            result = self.paths_by_dp[(ct_id, dp_id)]
        return result

    def get_path_from_to(self, ct_id, dp_id, dest_ct, dest_dp):
        result = None
        if (ct_id, dp_id, dest_ct, dest_dp) in self.paths_from_to:
            result = self.paths_from_to[(ct_id, dp_id, dest_ct, dest_dp)]
        return result

    def _new_tag(self, ct_id, dp_id):
        if not (ct_id, dp_id) in self.match_tags_by_dp:
            self.match_tags_by_dp[(ct_id, dp_id)] = 0
        self.match_tags_by_dp[(ct_id, dp_id)] += 1
        return self.match_tags_by_dp[(ct_id, dp_id)]
        
    def new_nh_path(self, ct_id, dp_id, dest_ct, dest_dp):
        if not (ct_id, dp_id) in self.isls:
            self.isls[ct_id, dp_id] = set([])
        self.isls[(ct_id, dp_id)].add((dest_ct, dest_dp))
        #TODO: make this not run out of tags
        match_tag = self._new_tag(ct_id, dp_id)
        newpath = Path(ct_id, dp_id, dest_ct, dest_dp, dest_ct,
                       dest_dp, 1, None, match_tag)
        self._add(newpath)
        return newpath
        
    def new_path(self, ct_id, dp_id, dest_dp, dest_ct, nh_ct, nh_dp):
        if nh_ct == dest_ct and nh_dp == dest_dp:
            newpath = new_nh_path(ct_id, dp_id, dest_ct, dest_dp)
        else:
            nh_path = self.paths_from_to[(nh_ct, nh_dp,
                                          dest_dp, dest_ct)]
            match_tag = self._new_tag(ct_id, dp_id)
            newpath = Path(ct_id, dp_id, dest_dp, dest_ct, nh_ct,
                           nh_dp, nh_path.distance + 1, nh_path.match_tag,
                           match_tag);
            self._add(newpath)
        return newpath
   
    def _add(self, path):
        #TODO: might make this public, but I need to ensure the mpls tag is new
        if not (path.ct_id, path.dp_id) in self.paths_by_dp:
            self.paths_by_dp[(path.ct_id, path.dp_id)] = set([])
        self.paths_by_dp[(path.ct_id, path.dp_id)].add(path)
        if not (path.nh_ct, path.nh_dp) in self.paths_by_nh:
            self.paths_by_nh[(path.nh_ct, path.nh_dp)] = set([])
        self.paths_by_nh[(path.nh_ct, path.nh_dp)].add(path)
        fromto = (path.ct_id, path.dp_id, path.dest_ct, path.dest_dp)
        self.paths_from_to[fromto] = path

    def discard(self, path):
        self.paths_by_dp[(path.ct_id, path.dp_id)].discard(path)
        self.paths_by_nh[(paths.nh_ct, paths.nh_dp)].discard(path)
        self.paths_from_to.discard((ct_id, dp_id, dest_ct, dest_dp))
