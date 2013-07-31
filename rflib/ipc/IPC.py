import bson

class IPCMessage(object):
    def get_type(self):
        raise NotImplementedError

    def from_dict(self, data):
        raise NotImplementedError

    def to_dict(self):
        raise NotImplementedError

    def from_bson(self, data):
        return self.from_dict(bson.BSON(data).decode())

    def to_bson(self):
        return bson.BSON.encode(self.to_dict())


class IPCMessageFactory:
    def build_for_type(self, type_):
        raise NotImplementedError

class IPCMessageProcessor:
    def process(self, from_, to, channel, msg):
        raise NotImplementedError

class IPCMessageService:
    def get_id(self):
        return self._id

    def set_id(self, id_):
        self._id = id_

    def listen(channel_id, factory, processor, block=True):
        raise NotImplementedError

    def send(channel_id, to, msg):
        raise NotImplementedError


class IPCRole(object):
    server = 1
    client = 2
    proxy = 3

