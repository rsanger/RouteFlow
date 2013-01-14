from TLV import *
from bson.binary import Binary

# Action Type Variables ('Enum')
RFAT_OUTPUT = 0         # Output port
RFAT_SET_ETH_SRC = 1    # Ethernet source address
RFAT_SET_ETH_DST = 2    # Ethernet destination address
RFAT_PUSH_MPLS = 3      # Push MPLS label
RFAT_POP_MPLS = 4       # Pop MPLS label
RFAT_SWAP_MPLS = 5      # Swap MPLS label
# Future implementation
#RFAT_DROP = 6           # Drop packet (Unimplemented)
#RFAT_SFLOW = 7          # Generate SFlow messages (Unimplemented)

class Action(TLV):
    def __init__(self, actionType=None, value=None):
        super(Action, self).__init__(actionType, self.type_to_bin(actionType, value))

    @classmethod
    def OUTPUT(cls, port):
        return cls(RFAT_OUTPUT, port)

    @classmethod
    def SET_ETH_SRC(cls, ethernet_src):
        return cls(RFAT_SET_ETH_SRC, ethernet_src)

    @classmethod
    def SET_ETH_DST(cls, ethernet_dst):
        return cls(RFAT_SET_ETH_DST, ethernet_dst)

    @classmethod
    def PUSH_MPLS(cls, label):
        return cls(RFAT_PUSH_MPLS, label)

    @classmethod
    def POP_MPLS(cls):
        return cls(RFAT_POP_MPLS, None)

    @classmethod
    def SWAP_MPLS(cls, label):
        return cls(RFAT_SWAP_MPLS, label)

    @classmethod
    def from_dict(cls, dic):
        ac = cls()
        ac._type = dic['type']
        ac._value = dic['value']
        return ac

    @staticmethod
    def type_to_bin(actionType, value):
        if actionType in (RFAT_OUTPUT, RFAT_PUSH_MPLS, RFAT_SWAP_MPLS):
            return int_to_bin(value, 32)
        elif actionType in (RFAT_SET_ETH_SRC, RFAT_SET_ETH_DST):
            return ether_to_bin(value)
        elif actionType == RFAT_POP_MPLS:
            return ''
        else:
            return None

    def get_value(self):
        if self._type in (RFAT_OUTPUT, RFAT_PUSH_MPLS, RFAT_SWAP_MPLS):
            return bin_to_int(self._value)
        elif self._type in (RFAT_SET_ETH_SRC, RFAT_SET_ETH_DST):
            return bin_to_ether(self._value)
        elif self._type == RFAT_POP_MPLS:
            return None
        else:
            return None

    def set_value(self, value):
        self._value = Binary(self.type_to_bin(self._type, value), 0)
