# Copyright (C) 2006, Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import dbus, dbus.glib, gobject

class ObjectCache(object):
    def __init__(self):
        self._cache = {}

    def get(self, object_path):
        try:
            return self._cache[object_path]
        except KeyError:
            return None

    def add(self, obj):
        op = obj.object_path()
        if not self._cache.has_key(op):
            self._cache[op] = obj

    def remove(self, object_path):
        if self._cache.has_key(object_path):
            del self._cache[object_path]


DS_DBUS_SERVICE = "org.laptop.sugar.DataStore"
DS_DBUS_INTERFACE = "org.laptop.sugar.DataStore"
DS_DBUS_PATH = "/org/laptop/sugar/DataStore"

class DSObject(gobject.GObject):

    __gsignals__ = {
        'updated': (gobject.SIGNAL_RUN_FIRST, gobject.TYPE_NONE,
                    ([gobject.TYPE_PYOBJECT, gobject.TYPE_PYOBJECT, gobject.TYPE_PYOBJECT]))
    }

    _DS_OBJECT_DBUS_INTERFACE = "org.laptop.sugar.DataStore.Object"

    def __init__(self, bus, new_obj_cb, del_obj_cb, object_path):
        gobject.GObject.__init__(self)
        self._object_path = object_path
        self._ps_new_object = new_obj_cb
        self._ps_del_object = del_obj_cb
        bobj = bus.get_object(DS_DBUS_SERVICE, object_path)
        self._dsobj = dbus.Interface(bobj, self.__DS_OBJECT_DBUS_INTERFACE)
        self._dsobj.connect_to_signal('Updated', self._updated_cb)
        self._data = None
        self._data_needs_update = True
        self._properties = self._dsobj.get_properties()
        self._deleted = False

    def object_path(self):
        return self._object_path

    def _emit_updated_signal(self, data, prop_dict, deleted):
        self.emit('updated', data, prop_dict, deleted)
        return False

    def _update_internal_properties(self, prop_dict):
        did_update = False
        for (key, value) in prop_dict.items():
            if not len(value):
                if self._properties.has_key(ley):
                    did_update = True
                    del self._properties[key]
            else:
                if self._properties.has_key(key):
                    if self._properties[key] != value:
                        did_update = True
                        self._properties[key] = value
                else:
                    did_update = True
                    self._properties[key] = value
        return did_update

    def _updated_cb(self, data=False, prop_dict={}, deleted=False):
        if self._update_internal_properties(prop_dict):
            gobject.idle_add(self._emit_updated_signal, data, prop_dict, deleted)
        self._deleted = deleted

    def get_data(self):
        if self._data_needs_update:
            self._data = self._dsobj.get_data()
        return self._data

    def set_data(self, data):
        old_data = self._data
        self._data = data
        try:
            self._dsobj.set_data(dbus.ByteArray(data))
            del old_data
        except dbus.DBusException, e:
            self._data = old_data
            raise e

    def set_properties(self, prop_dict):
        old_props = self._properties
        self._update_internal_properties(prop_dict)
        try:
            self._dsobj.set_properties(prop_dict)
            del old_props
        except dbus.DBusException, e:
            self._properties = old_props
            raise e

    def get_properties(self, prop_dict):
        return self._properties

class DataStore(gobject.GObject):

    _DS_DBUS_OBJECT_PATH = DBUS_PATH + "/Object/"

    def __init__(self):
        gobject.GObject.__init__(self)
        self._objcache = ObjectCache()
        self._bus = dbus.SessionBus()
        self._ds = dbus.Interface(self._bus.get_object(DS_DBUS_SERVICE,
                DS_DBUS_PATH), DS_DBUS_INTERFACE)

    def _new_object(self, object_path):
        obj = self._objcache.get(object_path)
        if not obj:
            if object_path.startswith(self._DS_DBUS_OBJECT_PATH):
                obj = DSObject(self._bus, self._new_object,
                        self._del_object, object_path)
            else:
                raise RuntimeError("Unknown object type")
            self._objcache.add(obj)
        return obj

    def _del_object(self, object_path):
        # FIXME
        pass

    def get(self, uid):
        return self._new_object(self._ds.get(uid))

    def create(self, data, prop_dict={}):
        op = self._ds.create(dbus.ByteArray(data), dbus.Dictionary(prop_dict))
        return self._new_object(op)

    def delete(self, obj):
        op = obj.object_path()
        obj = self._objcache.get(op)
        if not obj:
            raise RuntimeError("Object not found.")
        self._ds.delete(op)

    def find(self, prop_dict):
        ops = self._ds.find(dbus.Dictionary(prop_dict))
        objs = []
        for op in ops:
            objs.append(self._new_object(op))
        return objs

_ds = None
def get_instance():
    global _ds
    if not _ds:
        _ds = DataStore()
    return _ds
