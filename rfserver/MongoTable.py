import pymongo as mongo

from rflib.defs import *
from rflib.ipc.MongoIPC import format_address

class MongoTable:
    def __init__(self, name, address=MONGO_ADDRESS):
        self.name = name
        self.address = format_address(address)
        self.connection = mongo.Connection(*self.address)
        self.data = self.connection[MONGO_DB_NAME][name]

    def get_dicts(self, **kwargs):
        return self.data.find(kwargs)

    def set_dict(self, d):
        # TODO: enforce (*_id, *_port) uniqueness restriction
        return self.data.save(d)

    def remove_id(self, _id):
        self.data.remove(_id)

    def clear(self):
        self.data.remove()
