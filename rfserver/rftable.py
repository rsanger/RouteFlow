from rflib.defs import *

if DB_TYPE == 'memory':
    from MemoryTable import MemoryTable as TableBase
else:
    from MongoTable import MongoTable as TableBase

RFENTRY_IDLE_VM_PORT = 1
RFENTRY_IDLE_DP_PORT = 2
RFENTRY_ASSOCIATED = 3
RFENTRY_ACTIVE = 4
RFISL_IDLE_DP_PORT = 5
RFISL_IDLE_REMOTE = 6
RFISL_ACTIVE = 7

RFENTRY = 0
RFCONFIGENTRY = 1
RFISLCONFENTRY = 2
RFISLENTRY = 3

class EntryFactory:
    @staticmethod
    def make(type_):
        if type_ == RFENTRY:
            return RFEntry()
        elif type_ == RFCONFIGENTRY:
            return RFConfigEntry()
        elif type_ == RFISLENTRY:
            return RFISLEntry()
        elif type_ == RFISLCONFENTRY:
            return RFISLConfEntry()

class EntryTable(TableBase):
    def __init__(self, name, entry_type):
        TableBase.__init__(self, name)
        self.entry_type = entry_type

    def get_entries(self, **kwargs):
        results = self.get_dicts(**kwargs)
        entries = []
        for result in results:
            entry = EntryFactory.make(self.entry_type)
            entry.from_dict(result)
            entries.append(entry)
        return entries

    def set_entry(self, entry):
        entry.id = self.set_dict(entry.to_dict())

    def remove_entry(self, entry):
        self.remove_id(entry.id)

    def __str__(self):
        s = ""
        for entry in self.get_entries():
            s += str(entry) + "\n\n"
        return s.strip("\n")


class RFTable(EntryTable):
    def __init__(self):
        EntryTable.__init__(self, RFTABLE_NAME, RFENTRY)

    def get_entry_by_vm_port(self, vm_id, vm_port):
        result = self.get_entries(vm_id=vm_id,
                                  vm_port=vm_port)
        if not result:
            return None
        return result[0]

    def get_entry_by_dp_port(self, ct_id, dp_id, dp_port):
        result = self.get_entries(ct_id=ct_id,
                                  dp_id=dp_id,
                                  dp_port=dp_port)
        if not result:
            return None
        return result[0]

    def get_entry_by_vs_port(self, vs_id, vs_port):
        result = self.get_entries(vs_id=vs_id,
                                  vs_port=vs_port)
        if not result:
            return None
        return result[0]

    def get_dp_entries(self, ct_id, dp_id):
        return self.get_entries(ct_id=ct_id, dp_id=dp_id)

    def is_dp_registered(self, ct_id, dp_id):
        return bool(self.get_dp_entries(ct_id, dp_id))


class RFConfig(EntryTable):
    def __init__(self, ifile):
        EntryTable.__init__(self, RFCONFIG_NAME, RFCONFIGENTRY)
        # TODO: perform validation of config
        configfile = file(ifile)
        lines = configfile.readlines()[1:]
        entries = [line.strip("\n").split(",") for line in lines]
        for (a, b, c, d, e) in entries:
            self.set_entry(RFConfigEntry(vm_id=int(a, 16), vm_port=int(b),
                                         ct_id=int(c), dp_id=int(d, 16),
                                         dp_port=int(e)))

    def get_config_for_vm_port(self, vm_id, vm_port):
        result = self.get_entries(vm_id=vm_id,
                                  vm_port=vm_port)
        if not result:
            return None
        return result[0]

    def get_config_for_dp_port(self, ct_id, dp_id, dp_port):
        result = self.get_entries(ct_id=ct_id,
                                  dp_id=dp_id,
                                  dp_port=dp_port)
        if not result:
            return None
        return result[0]

class RFISLTable(EntryTable):
    def __init__(self):
        EntryTable.__init__(self, RFISL_NAME, RFISLENTRY)

    def get_entry_by_addr(self, ct_id, dp_id, dp_port, eth_addr):
        result = self.get_entries(ct_id=ct_id, dp_id=dp_id, dp_port=dp_port,
                                  eth_addr=eth_addr)
        if not result:
            return None
        return result[0]

    def get_entry_by_remote(self, rem_ct, rem_id, rem_port, rem_eth_addr):
        result = self.get_entries(rem_ct=rem_ct, rem_id=rem_id,
                                  rem_port=rem_port, rem_eth_addr=rem_eth_addr)
        if not result:
            return None
        return result[0]

    def get_dp_entries(self, ct_id, dp_id):
        return self.get_entries(ct_id=ct_id, dp_id=dp_id)

    def is_dp_registered(self, ct_id, dp_id):
        return bool(self.get_dp_entries(ct_id, dp_id))

class RFISLConf(EntryTable):
    def __init__(self, ifile):
        EntryTable.__init__(self, RFISLCONF_NAME, RFISLCONFENTRY)
        # TODO: perform validation of config
        try:
            internalfile = file(ifile)
        except:
            # Default to no ISL config
            return
        lines = internalfile.readlines()[1:]
        entries = [line.strip("\n").split(",") for line in lines]
        for (a, b, c, d, e, f, g, h, i) in entries:
            self.set_entry(RFISLConfEntry(vm_id=int(a, 16), ct_id=int(b),
                                          dp_id=int(c, 16), dp_port=int(d),
                                          eth_addr=e, rem_ct=int(f),
                                          rem_id=int(g, 16), rem_port=int(h),
                                          rem_eth_addr=i))

    def get_entries_by_port(self, ct, id_, port):
        results = self.get_entries(ct_id=ct, dp_id=id_, dp_port=port)
        results.extend(self.get_entries(rem_ct=ct, rem_id=id_, rem_port=port))
        return results


# Convenience functions for packing/unpacking to a dict for BSON representation
def load_from_dict(src, obj, attr):
    setattr(obj, attr, src[attr])


def pack_into_dict(dest, obj, attr):
    dest[attr] = getattr(obj, attr)


class RFEntry:
    def __init__(self, vm_id=None, vm_port=None, ct_id=None, dp_id=None,
                 dp_port=None, vs_id=None, vs_port=None, eth_addr=None):
        self.id = None
        self.vm_id = vm_id
        self.vm_port = vm_port
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.dp_port = dp_port
        self.vs_id = vs_id
        self.vs_port = vs_port
        self.eth_addr = eth_addr

    def _is_idle_vm_port(self):
        return (self.vm_id is not None and
                self.vm_port is not None and
                self.ct_id is None and
                self.dp_id is None and
                self.dp_port is None and
                self.vs_id is None and
                self.vs_port is None)

    def _is_idle_dp_port(self):
        return (self.vm_id is None and
                self.vm_port is None and
                self.ct_id is not None and
                self.dp_id is not None and
                self.dp_port is not None and
                self.vs_id is None and
                self.vs_port is None)

    def make_idle(self, type_):
        if type_ == RFENTRY_IDLE_VM_PORT:
            self.ct_id = None
            self.dp_id = None
            self.dp_port = None
            self.vs_id = None
            self.vs_port = None
        elif type_ == RFENTRY_IDLE_DP_PORT:
            self.vm_id = None
            self.vm_port = None
            self.vs_id = None
            self.vs_port = None
            self.eth_addr = None

    def associate(self, id_, port, ct_id=None, eth_addr=None):
        if self._is_idle_vm_port():
            self.ct_id = ct_id
            self.dp_id = id_
            self.dp_port = port
        elif self._is_idle_dp_port():
            self.vm_id = id_
            self.vm_port = port
            self.eth_addr = eth_addr
        else:
            raise ValueError

    def activate(self, vs_id, vs_port):
        self.vs_id = vs_id
        self.vs_port = vs_port

    def get_status(self):
        if self._is_idle_vm_port():
            return RFENTRY_IDLE_VM_PORT
        elif self._is_idle_dp_port():
            return RFENTRY_IDLE_DP_PORT
        elif self.vs_id is None and self.vs_port is None:
            return RFENTRY_ASSOCIATED
        else:
            return RFENTRY_ACTIVE

    def __str__(self):
        return "vm_id: %s\nvm_port: %s\n"\
               "dp_id: %s\ndp_port: %s\n"\
               "vs_id: %s\nvs_port: %s\n"\
               "eth_addr: %s\nct_id: %s\n"\
               "status:%s" % (self.vm_id,
                              self.vm_port,
                              self.dp_id,
                              self.dp_port,
                              self.vs_id,
                              self.vs_port,
                              self.eth_addr,
                              self.ct_id,
                              self.get_status())

    def from_dict(self, data):
        self.id = data["_id"]
        load_from_dict(data, self, "vm_id")
        load_from_dict(data, self, "vm_port")
        load_from_dict(data, self, "ct_id")
        load_from_dict(data, self, "dp_id")
        load_from_dict(data, self, "dp_port")
        load_from_dict(data, self, "vs_id")
        load_from_dict(data, self, "vs_port")
        load_from_dict(data, self, "eth_addr")

    def to_dict(self):
        data = {}
        if self.id is not None:
            data["_id"] = self.id
        pack_into_dict(data, self, "vm_id")
        pack_into_dict(data, self, "vm_port")
        pack_into_dict(data, self, "ct_id")
        pack_into_dict(data, self, "dp_id")
        pack_into_dict(data, self, "dp_port")
        pack_into_dict(data, self, "vs_id")
        pack_into_dict(data, self, "vs_port")
        pack_into_dict(data, self, "eth_addr")
        return data


class RFISLEntry:
    def __init__(self, vm_id=None, ct_id=None, dp_id=None,  dp_port=None,
                 eth_addr=None, rem_ct=None, rem_id=None, rem_port=None,
                 rem_eth_addr=None):
        self.id = None
        self.vm_id = vm_id
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.dp_port = dp_port
        self.eth_addr = eth_addr
        self.rem_ct = rem_ct
        self.rem_id = rem_id
        self.rem_port = rem_port
        self.rem_eth_addr = rem_eth_addr

    def __str__(self):
        return "vm_id: %s "\
               "ct_id: %s dp_id: %s dp_port: %s eth_addr: %s"\
               "rem_ct: %s rem_id: %s rem_port: %s rem_addr: %s"\
               % (self.vm_id, self.ct_id,
                  self.dp_id, self.dp_port, self.eth_addr,
                  self.rem_ct, self.rem_id, self.rem_port,
                  self.rem_eth_addr)

    def make_idle(self, type_):
        if type_ == RFISL_IDLE_REMOTE:
            self.ct_id = None
            self.dp_id = None
            self.dp_port = None
            self.eth_addr = None
        elif type_ == RFISL_IDLE_DP_PORT:
            self.rem_ct = None
            self.rem_id = None
            self.rem_port = None
            self.rem_eth_addr = None

    def is_idle_dp_port(self):
        return (self.ct_id is not None and
                self.dp_id is not None and
                self.dp_port is not None and
                self.rem_ct is None and
                self.rem_id is None and
                self.rem_port is None)

    def is_idle_remote(self):
        return (self.ct_id is None and
                self.dp_id is None and
                self.dp_port is None and
                self.rem_ct is not None and
                self.rem_id is not None and
                self.rem_port is not None)

    def associate(self, ct, id_, port, eth_addr):
        if self.is_idle_dp_port():
            self.rem_ct = ct
            self.rem_id = id_
            self.rem_port = port
            self.rem_eth_addr = eth_addr
        elif self.is_idle_remote():
            self.ct_id = ct
            self.dp_id = id_
            self.dp_port = port
            self.eth_addr = eth_addr

    def get_status(self):
        if self.is_idle_dp_port():
            return RFISL_IDLE_DP_PORT
        elif self.is_idle_remote():
            return RFISL_IDLE_REMOTE
        else:
            return RFISL_ACTIVE

    def from_dict(self, data):
        self.id = data["_id"]
        load_from_dict(data, self, "vm_id")
        load_from_dict(data, self, "ct_id")
        load_from_dict(data, self, "dp_id")
        load_from_dict(data, self, "dp_port")
        load_from_dict(data, self, "eth_addr")
        load_from_dict(data, self, "rem_ct")
        load_from_dict(data, self, "rem_id")
        load_from_dict(data, self, "rem_port")
        load_from_dict(data, self, "rem_eth_addr")

    def to_dict(self):
        data = {}
        if self.id is not None:
            data["_id"] = self.id
        pack_into_dict(data, self, "vm_id")
        pack_into_dict(data, self, "ct_id")
        pack_into_dict(data, self, "dp_id")
        pack_into_dict(data, self, "dp_port")
        pack_into_dict(data, self, "eth_addr")
        pack_into_dict(data, self, "rem_ct")
        pack_into_dict(data, self, "rem_id")
        pack_into_dict(data, self, "rem_port")
        pack_into_dict(data, self, "rem_eth_addr")
        return data


class RFISLConfEntry:
    def __init__(self, vm_id=None, ct_id=None, dp_id=None,  dp_port=None,
                 eth_addr=None, rem_ct=None, rem_id=None, rem_port=None,
                 rem_eth_addr=None):
        self.id = None
        self.vm_id = vm_id
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.dp_port = dp_port
        self.eth_addr = eth_addr
        self.rem_ct = rem_ct
        self.rem_id = rem_id
        self.rem_port = rem_port
        self.rem_eth_addr = rem_eth_addr

    def __str__(self):
        return "vm_id: %s "\
               "ct_id: %s dp_id: %s dp_port: %s eth_addr: %s"\
               "rem_ct: %s rem_id: %s rem_port: %s rem_addr: %s"\
               % (self.vm_id, self.ct_id,
                  self.dp_id, self.dp_port, self.eth_addr,
                  self.rem_ct, self.rem_id, self.rem_port,
                  self.rem_eth_addr)

    def get_status(self):
        return RFENTRY_ACTIVE

    def from_dict(self, data):
        self.id = data["_id"]
        load_from_dict(data, self, "vm_id")
        load_from_dict(data, self, "ct_id")
        load_from_dict(data, self, "dp_id")
        load_from_dict(data, self, "dp_port")
        load_from_dict(data, self, "eth_addr")
        load_from_dict(data, self, "rem_ct")
        load_from_dict(data, self, "rem_id")
        load_from_dict(data, self, "rem_port")
        load_from_dict(data, self, "rem_eth_addr")

    def to_dict(self):
        data = {}
        if self.id is not None:
            data["_id"] = self.id
        pack_into_dict(data, self, "vm_id")
        pack_into_dict(data, self, "ct_id")
        pack_into_dict(data, self, "dp_id")
        pack_into_dict(data, self, "dp_port")
        pack_into_dict(data, self, "eth_addr")
        pack_into_dict(data, self, "rem_ct")
        pack_into_dict(data, self, "rem_id")
        pack_into_dict(data, self, "rem_port")
        pack_into_dict(data, self, "rem_eth_addr")
        return data


class RFConfigEntry:
    def __init__(self, vm_id=None, vm_port=None, ct_id=None, dp_id=None,
                 dp_port=None):
        self.id = None
        self.vm_id = vm_id
        self.vm_port = vm_port
        self.ct_id = ct_id
        self.dp_id = dp_id
        self.dp_port = dp_port

    def __str__(self):
        return "vm_id: %s vm_port: %s "\
               "dp_id: %s dp_port: %s "\
               "ct_id: %s" % (self.vm_id, self.vm_port,
                              self.dp_id, self.dp_port,
                              self.ct_id)

    def from_dict(self, data):
        self.id = data["_id"]
        load_from_dict(data, self, "vm_id")
        load_from_dict(data, self, "vm_port")
        load_from_dict(data, self, "ct_id")
        load_from_dict(data, self, "dp_id")
        load_from_dict(data, self, "dp_port")

    def to_dict(self):
        data = {}
        if self.id is not None:
            data["_id"] = self.id
        pack_into_dict(data, self, "vm_id")
        pack_into_dict(data, self, "vm_port")
        pack_into_dict(data, self, "ct_id")
        pack_into_dict(data, self, "dp_id")
        pack_into_dict(data, self, "dp_port")
        return data
