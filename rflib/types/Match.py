from TLV import *
<<<<<<< HEAD
=======
from bson.binary import Binary
>>>>>>> 1f9426f4d828e2412adb59bad20789174beb9515

RFMT_IPV4 = 0        # Match IPv4 Destination
RFMT_IPV6 = 1        # Match IPv6 Destination
RFMT_ETHERNET = 2    # Match Ethernet Destination
RFMT_MPLS = 4        # Match MPLS label_in
# Future implementation
#RFMT_IN_PORT = 5     # Match incoming port (Unimplemented)
#RFMT_VLAN = 6        # Match incoming VLAN (Unimplemented)

class Match(TLV):
    def __init__(self, matchType=None, value=None):
        super(Match, self).__init__(matchType, self.type_to_bin(matchType, value))

    @classmethod
    def IPV4(cls, address, netmask):
        return cls(RFMT_IPV4, (address, netmask))

    @classmethod
    def IPV6(cls, address, netmask):
        return cls(RFMT_IPV6, (address, netmask))

    @classmethod
    def ETHERNET(cls, ethernet_dst):
        return cls(RFMT_ETHERNET, ethernet_dst)

    @classmethod
    def MPLS(cls, label):
        return cls(RFMT_MPLS, label)

    @classmethod
    def from_dict(cls, dic):
        ma = cls()
        ma._type = dic['type']
        ma._value = dic['value']
        return ma

    @staticmethod
    def type_to_bin(matchType, value):
        if matchType == RFMT_IPV4:
            return inet_pton(AF_INET, value[0]) + inet_pton(AF_INET, value[1])
        elif matchType == RFMT_IPV6:
            return inet_pton(AF_INET6, value[0]) + inet_pton(AF_INET6, value[1])
        elif matchType == RFMT_ETHERNET:
            return ether_to_bin(value)
        elif matchType ==  RFMT_MPLS:
            return int_to_bin(value, 32)
        else:
            return None

    def get_value(self):
        if self._type == RFMT_IPV4:
            return (inet_ntop(AF_INET, self._value[:4]), inet_ntop(AF_INET, self._value[4:]))
        elif self._type == RFMT_IPV6:
            return (inet_ntop(AF_INET6, self._value[:16]), inet_ntop(AF_INET6, self._value[16:]))
        elif self._type == RFMT_ETHERNET:
            return bin_to_ether(self._value)
        elif self._type == RFMT_MPLS:
            return bin_to_int(self._value)
        else:
            return None

    def set_value(value):
<<<<<<< HEAD
        _value = self.type_to_bin(self._type, value)
=======
        _value = Binary(self.type_to_bin(self._type, value), 0)
>>>>>>> 1f9426f4d828e2412adb59bad20789174beb9515
