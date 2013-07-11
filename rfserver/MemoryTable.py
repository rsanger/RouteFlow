from rflib.defs import *

class MemoryTable:
    def __init__(self, name, address=None):
        self.name = name
        self._next_id = 1000
        self._data = {}

    def get_dicts(self, **kwargs):
        if len(kwargs) == 0:
            return self._data.values()

        results = []
        for d in self._data.itervalues():
            add = True
            for (k,v) in kwargs.iteritems():
                if k not in d or d[k] != v:
                    add = False
                    break
            if add:
                results.append(d)
        return results

    def set_dict(self, d):
        if '_id' not in d:
            self._next_id += 1
            d['_id'] = self._next_id
        _id = d['_id']
        self._data[_id] = d
        return _id

    def remove_id(self, _id):
        if _id in self._data:
            del self._data[_id]

    def clear(self):
        self._data = {}
