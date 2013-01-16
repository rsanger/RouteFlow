from TLV import *
from bson.binary import Binary

RFOT_PRIORITY = 1     # Route priority
RFOT_IDLE_TIMEOUT = 2 # Drop route after specified idle time
RFOT_HARD_TIMEOUT = 3 # Drop route after specified time has passed
# Optional
RFOT_CT_ID = 255      # Specify destination controller

class Option(TLV):
    def __init__(self, optionType=None, value=None):
        super(Option, self).__init__(optionType, self.type_to_bin(optionType, value))

    @classmethod
    def PRIORITY(cls, priority):
        return cls(RFOT_PRIORITY, priority)

    @classmethod
    def IDLE_TIMEOUT(cls, timeout):
        return cls(RFOT_IDLE_TIMEOUT, timeout)

    @classmethod
    def HARD_TIMEOUT(cls, timeout):
        return cls(RFOT_HARD_TIMEOUT, timeout)

    @classmethod
    def CT_ID(cls, controller):
        return cls(RFOT_CT_ID, controller)

    @classmethod
    def from_dict(cls, dic):
        ma = cls()
        ma._type = dic['type']
        ma._value = dic['value']
        return ma

    @staticmethod
    def optional(optionType):
        if optionType in (RFOT_CT_ID):
            return true
        return false

    @staticmethod
    def type_to_bin(optionType, value):
        if optionType in (RFOT_PRIORITY, RFOT_IDLE_TIMEOUT, RFOT_HARD_TIMEOUT):
            return int_to_bin(value, 16)
        elif optionType == RFOT_CT_ID:
            return int_to_bin(value, 64)
        else:
            return None

    def get_value(self):
        if self._type in (RFOT_PRIORITY, RFOT_IDLE_TIMEOUT, RFOT_HARD_TIMEOUT,
                          RFOT_CT_ID):
            return bin_to_int(self._value)
        else:
            return None

    def set_value(value):
        _value = Binary(self.type_to_bin(self._type, value), 0)
