#!/usr/bin/env python
#-*- coding:utf-8 -*-

import os
import sys
import logging
import binascii
import argparse
import time
import copy
import Queue

from bson.binary import Binary

import rflib.ipc.IPC as IPC
import rflib.ipc.IPCService as IPCService
from rflib.ipc.RFProtocol import *
from rflib.ipc.RFProtocolFactory import RFProtocolFactory
from rflib.defs import *
from rflib.types.Match import *
from rflib.types.Action import *
from rflib.types.Option import *

from rftable import *

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s %(name)-15s %(levelname)-8s %(message)s',
    datefmt='%b %d %H:%M:%S'
    )

# Register actions
REGISTER_IDLE = 0
REGISTER_ASSOCIATED = 1
REGISTER_ISL = 2

class RouteModTranslator(object):

    DROP_PRIORITY = Option.PRIORITY(PRIORITY_LOWEST + PRIORITY_BAND)
    CONTROLLER_PRIORITY = Option.PRIORITY(PRIORITY_HIGH)
    DEFAULT_PRIORITY = Option.PRIORITY(PRIORITY_LOWEST + PRIORITY_BAND + 1000)

    def __init__(self, dp_id, ct_id, rftable, isltable):
        self.dp_id = dp_id
        self.ct_id = ct_id
        self.rftable = rftable
        self.isltable = isltable

    def configure_datapath(self):
        raise NotImplementedError

    def handle_controller_route_mod(self, entry, rm):
        raise NotImplementedError

    def handle_route_mod(self, entry, rm):
        raise NotImplementedError

    def handle_isl_route_mod(self, entry, rm):
        raise NotImplementedError


class DefaultRouteModTranslator(RouteModTranslator):

    def _create_ethernet_match_routemods(self, rm, out_port, entries):
        rms = []
        for entry in entries:
            if out_port != entry.dp_port:
                if (entry.get_status() == RFENTRY_ACTIVE or
                    entry.get_status() == RFISL_ACTIVE):
                    local_rm = copy.deepcopy(rm)
                    local_rm.add_match(Match.ETHERNET(entry.eth_addr))
                    local_rm.add_match(Match.IN_PORT(entry.dp_port))
                    rms.append(local_rm)
        return rms

    def configure_datapath(self):
        rms = []

        # delete all groups
        rm = RouteMod(RMT_DELETE_GROUP, self.dp_id)
        rms.append(rm)

        # delete all flows
        rm = RouteMod(RMT_DELETE, self.dp_id)
        rms.append(rm)

        # default drop
        rm = RouteMod(RMT_ADD, self.dp_id)
        rm.add_option(self.DROP_PRIORITY)
        rms.append(rm)

        # ARP
        rm = RouteMod(RMT_ADD, self.dp_id)
        rm.add_match(Match.ETHERTYPE(ETHERTYPE_ARP))
        rm.add_action(Action.CONTROLLER())
        rm.add_option(self.CONTROLLER_PRIORITY)
        rms.append(rm)

        return rms

    def handle_controller_route_mod(self, entry, rm):
        rm.add_action(Action.CONTROLLER())
        return [rm]

    def handle_route_mod(self, entry, rm):
        rms = []
        entries = self.rftable.get_entries(dp_id=entry.dp_id,
                                           ct_id=entry.ct_id)
        entries.extend(self.isltable.get_entries(dp_id=entry.dp_id,
                                                 ct_id=entry.ct_id))

        # Replace the VM port with the datapath port
        rm.add_action(Action.OUTPUT(entry.dp_port))

        rms.extend(
            self._create_ethernet_match_routemods(rm, entry.dp_port, entries))
        return rms

    def handle_isl_route_mod(self, r, rm):
        rms = []
        rm.set_id(self.dp_id)
        rm.set_table(0)
        rm.set_actions(None)
        rm.add_action(Action.SET_ETH_SRC(r.eth_addr))
        rm.add_action(Action.SET_ETH_DST(r.rem_eth_addr))
        rm.add_action(Action.OUTPUT(r.dp_port))
        entries = self.rftable.get_entries(dp_id=r.dp_id, ct_id=r.ct_id)
        rms.extend(
            self._create_ethernet_match_routemods(rm, r.dp_port, entries))
        return rms


class SatelliteRouteModTranslator(DefaultRouteModTranslator):

    def __init__(self, dp_id, ct_id, rftable, isltable):
        super(SatelliteRouteModTranslator, self).__init__(
            dp_id, ct_id, rftable, isltable)
        self.sent_isl_dl = set()

    def handle_isl_route_mod(self, r, rm):
        rms = []
        for ethertype in (ETHERTYPE_IP, ETHERTYPE_IPV6):
            rm.set_matches(None)
            rm.add_match(Match.ETHERTYPE(ethertype))
            rm.set_options(None)
            rm.add_option(self.DEFAULT_PRIORITY)
            if r.rem_eth_addr not in self.sent_isl_dl:
                self.sent_isl_dl.add(r.rem_eth_addr)
                rm.set_id(self.dp_id)
                rm.set_table(0)
                rm.set_actions(None)
                rm.add_action(Action.SET_ETH_SRC(r.eth_addr))
                rm.add_action(Action.SET_ETH_DST(r.rem_eth_addr))
                rm.add_action(Action.OUTPUT(r.dp_port))
                entries = self.rftable.get_entries(
                    dp_id=r.dp_id, ct_id=r.ct_id)
                rms.extend(self._create_ethernet_match_routemods(
                    rm, r.dp_port, entries))
        return rms


class NoviFlowMultitableRouteModTranslator(RouteModTranslator):
    # NoviFlow as of Aug 2014 doesn't handle flow add operations
    # in unsorted priority order well (very slow). For the moment
    # make all flows in a table same priority.

    FIB_TABLE = 2
    ETHER_TABLE = 1

    def __init__(self, dp_id, ct_id, rftable, isltable):
        super(NoviFlowMultitableRouteModTranslator, self).__init__(
            dp_id, ct_id, rftable, isltable)

    def _create_ethernet_match_routemods(self, rm, out_port, entries):
        rms = []
        for entry in entries:
            if out_port != entry.dp_port:
                if (entry.get_status() == RFENTRY_ACTIVE or
                    entry.get_status() == RFISL_ACTIVE):
                    rms.append(rm)
                    break
        return rms

    def configure_datapath(self):
        rms = []

        # delete all groups
        rm = RouteMod(RMT_DELETE_GROUP, self.dp_id)
        rms.append(rm)
        # default group - send to controller
        rm = RouteMod(RMT_ADD_GROUP, self.dp_id)
        rm.set_group(CONTROLLER_GROUP);
        rm.add_action(Action.CONTROLLER())
        rms.append(rm)

        # delete all flows
        rm = RouteMod(RMT_DELETE, self.dp_id)
        rms.append(rm)

        # default drop
        for table_id in (0, self.ETHER_TABLE, self.FIB_TABLE):
            rm = RouteMod(RMT_ADD, self.dp_id)
            rm.set_table(table_id)
            rm.add_option(self.DROP_PRIORITY)
            rms.append(rm)
        rm = RouteMod(RMT_ADD, self.dp_id)
        rm.add_match(Match.ETHERNET("ff:ff:ff:ff:ff:ff"))
        rm.add_action(Action.GOTO(self.ETHER_TABLE))
        rm.add_option(self.CONTROLLER_PRIORITY)
        rms.append(rm)
        # ARP
        rm = RouteMod(RMT_ADD, self.dp_id)
        rm.set_table(self.ETHER_TABLE)
        rm.add_match(Match.ETHERTYPE(ETHERTYPE_ARP))
        rm.add_action(Action.GROUP(CONTROLLER_GROUP))
        rm.add_option(self.CONTROLLER_PRIORITY)
        rms.append(rm)
        # IPv4
        rm = RouteMod(RMT_ADD, self.dp_id)
        rm.set_table(self.ETHER_TABLE)
        rm.add_match(Match.ETHERTYPE(ETHERTYPE_IP))
        rm.add_option(self.DEFAULT_PRIORITY)
        rm.add_action(Action.GOTO(self.FIB_TABLE))
        rms.append(rm)
        return rms

    def handle_controller_route_mod(self, entry, rm):
        rms = []
        rm.add_action(Action.GROUP(CONTROLLER_GROUP))
        # should be FIB_TABLE, but see NoviFlow note.
        rm.set_table(self.ETHER_TABLE)
        dl_dst = None
        orig_matches = rm.get_matches()
        rm.set_matches(None)
        for match_dict in orig_matches:
            match = Match.from_dict(match_dict)
            match_type = match.type_to_str(match._type)
            if match_type == "RFMT_ETHERNET":
                dl_dst = match
            else:
                rm.add_match(match)
        rms.append(rm)

        if dl_dst is not None:
            hw_rm = RouteMod(RMT_CONTROLLER, entry.dp_id)
            hw_rm.set_id(rm.get_id())
            hw_rm.set_vm_port(rm.get_vm_port())
            hw_rm.add_match(dl_dst)
            hw_rm.add_action(Action.GOTO(self.ETHER_TABLE))
            hw_rm.add_option(self.DEFAULT_PRIORITY)
            rms.append(hw_rm)

        return rms

    def handle_route_mod(self, entry, rm):
        rms = []
        entries = self.rftable.get_entries(dp_id=entry.dp_id,
                                           ct_id=entry.ct_id)
        entries.extend(self.isltable.get_entries(dp_id=entry.dp_id,
                                                 ct_id=entry.ct_id))

        # Replace the VM port with the datapath port
        rm.add_action(Action.OUTPUT(entry.dp_port))

        rm.set_table(self.FIB_TABLE)
        # See NoviFlow note
        rm.set_options(None)
        rm.add_option(Option.PRIORITY(PRIORITY_HIGH))

        rms.extend(
            self._create_ethernet_match_routemods(rm, entry.dp_port, entries))
        return rms

    def handle_isl_route_mod(self, r, rm):
        rms = []
        # See NoviFlow note
        rm.set_options(None)
        rm.add_option(Option.PRIORITY(PRIORITY_HIGH))
        rm.set_id(self.dp_id)
        rm.set_table(self.FIB_TABLE)
        rm.set_actions(None)
        rm.add_action(Action.SET_ETH_SRC(r.eth_addr))
        rm.add_action(Action.SET_ETH_DST(r.rem_eth_addr))
        rm.add_action(Action.OUTPUT(r.dp_port))
        entries = self.rftable.get_entries(dp_id=r.dp_id, ct_id=r.ct_id)
        rms.extend(
            self._create_ethernet_match_routemods(rm, r.dp_port, entries))
        return rms


class RFServer(RFProtocolFactory, IPC.IPCMessageProcessor):
    def __init__(self, configfile, islconffile, multitabledps, satellitedps):
        self.config = RFConfig(configfile)
        self.islconf = RFISLConf(islconffile)
        try:
            self.multitabledps = set(
                [int(x, 16) for x in multitabledps.split(",")])
        except ValueError:
            self.multitabledps = set()
        try:
            self.satellitedps = set(
                [int(x, 16) for x in satellitedps.split(",")])
        except ValueError:
            self.satellitedps = set()

        # Initialise state tables
        self.rftable = RFTable()
        self.isltable = RFISLTable()

        self.sent_flows = set()
        self.route_mod_translator = {}

        # Logging
        self.log = logging.getLogger("rfserver")

        if self.satellitedps:
            self.log.info("Datapaths that are ISL satellites: %s",
                          list(self.satellitedps))
        if self.multitabledps:
            self.log.info("Datapaths that support multiple tables: %s",
                          list(self.multitabledps))

        self.ipc = IPCService.for_server(RFSERVER_ID)
        self.ipc.listen(RFCLIENT_RFSERVER_CHANNEL, self, self, False)
        self.ipc.listen(RFSERVER_RFPROXY_CHANNEL, self, self, True)

    def process(self, from_, to, channel, msg):
        """Process RFP messages received by RFServer.

        Args:
            from_, to, channel: Unused
            msg (IPCMessage): the RFP message for processing."""
        type_ = msg.get_type()
        if type_ == PORT_REGISTER:
            self.register_vm_port(msg.get_vm_id(), msg.get_vm_port(),
                                  msg.get_hwaddress())
        elif type_ == ROUTE_MOD:
            self.register_route_mod(msg)
        elif type_ == DATAPATH_PORT_REGISTER:
            self.register_dp_port(msg.get_ct_id(),
                                  msg.get_dp_id(),
                                  msg.get_dp_port())
        elif type_ == DATAPATH_DOWN:
            self.set_dp_down(msg.get_ct_id(), msg.get_dp_id())
        elif type_ == VIRTUAL_PLANE_MAP:
            self.map_port(msg.get_vm_id(), msg.get_vm_port(),
                          msg.get_vs_id(), msg.get_vs_port())


    # Port register methods
    def register_vm_port(self, vm_id, vm_port, eth_addr):
        """Process PORT_REGISTER type RFP messages.

        PORT_REGISTER messages indicate that a port on the vm has come up, it
        also contains the MAC address used by the port.

        Find the config entry for this port and check for an existing rftable
        entry for the associated dp port. If the entry exists then set the
        entry as associated, if not create a new entry and set it as idle.

        Args:
            vm_id (int): The id of the virtual machine
            vm_port (int): the number of the port
            eth_addr (string): the ethernet address associated for the port.
        """
        action = None
        config_entry = self.config.get_config_for_vm_port(vm_id, vm_port)
        if config_entry is None:
            # Register idle VM awaiting for configuration
            action = REGISTER_IDLE
            self.log.warning(
                'No config entry for client port (vm_id=%s, vm_port=%i)'
                % (format_id(vm_id), vm_port))
        else:
            entry = self.rftable.get_entry_by_dp_port(config_entry.ct_id,
                                                      config_entry.dp_id,
                                                      config_entry.dp_port)
            # If there's no entry, we have no DP, register VM as idle
            if entry is None:
                action = REGISTER_IDLE
            # If there's an idle DP entry matching configuration, associate
            elif entry.get_status() == RFENTRY_IDLE_DP_PORT:
                action = REGISTER_ASSOCIATED

        # Apply action
        if action == REGISTER_IDLE:
            self.rftable.set_entry(RFEntry(vm_id=vm_id, vm_port=vm_port,
                                           eth_addr=eth_addr))
            self.log.info("Registering client port as idle (vm_id=%s, "
                          "vm_port=%i, eth_addr=%s)" % (format_id(vm_id),
                                                        vm_port, eth_addr))
        elif action == REGISTER_ASSOCIATED:
            entry.associate(vm_id, vm_port, eth_addr=eth_addr)
            self.rftable.set_entry(entry)
            self.log.info("Registering client port and associating to "
                          "datapath port (vm_id=%s, vm_port=%i, "
                          "eth_addr = %s, dp_id=%s, dp_port=%s)"
                          % (format_id(vm_id), vm_port, eth_addr,
                             format_id(entry.dp_id), entry.dp_port))

    def acknowledge_route_mod(self, ct_id, vm_id, vm_port):
        self.ipc.send(
            RFCLIENT_RFSERVER_CHANNEL, str(vm_id),
            PortConfig(
                vm_id=vm_id, vm_port=vm_port, operation_id=PCT_ROUTEMOD_ACK))

    def send_route_mod(self, ct_id, rm):
        rm.add_option(Option.CT_ID(ct_id))
        self.ipc.send(RFSERVER_RFPROXY_CHANNEL, str(ct_id), rm)

    def register_route_mod(self, rm):
        """Process ROUTE_MOD type RFP messages.

        Create and translate copies of the routemod for any associated
        datapaths and send them to rfproxy.

        The id of the routemod must be replaced to represent the datapath as
        opposed to the switch.

        The modifications to the routemod copies for each datapath are
        dependant on its vendo and configuration, so these are handled by the
        translator. Any routemods received back from the translator are then
        sent to the appropriate controller.

        Args:
            rm (IPCMessage): the routemod for processing.
        """
        vm_id = rm.get_id()
        vm_port = rm.get_vm_port()

        # Find the (vmid, vm_port), (dpid, dpport) pair
        entry = self.rftable.get_entry_by_vm_port(vm_id, vm_port)
        translator = self.route_mod_translator[entry.dp_id]

        # If we can't find an associated datapath for this RouteMod,
        # drop it.
        if entry is None or entry.get_status() == RFENTRY_IDLE_VM_PORT:
            self.log.info("Received RouteMod destined for unknown "
                          "datapath - Dropping (vm_id=%s, vm_port=%d)" %
                          (format_id(vm_id), vm_port))
            return

        # Replace the VM id,port with the Datapath id.port
        rm.set_id(int(entry.dp_id))

        rms = []

        if rm.get_mod() is RMT_CONTROLLER:
            rms.extend(translator.handle_controller_route_mod(entry, rm))

        elif rm.get_mod() in (RMT_ADD, RMT_DELETE):
            rms.extend(translator.handle_route_mod(entry, rm))

            remote_dps = self.isltable.get_entries(rem_ct=entry.ct_id,
                                                   rem_id=entry.dp_id)

            for r in remote_dps:
                if r.get_status() == RFISL_ACTIVE:
                    local_rm = copy.deepcopy(rm)
                    remote_translator = self.route_mod_translator[int(r.dp_id)]
                    rms.extend(
                        remote_translator.handle_isl_route_mod(r, local_rm))
        else:
            self.log.info("Received RouteMod with unknown type: %s " % rm)

        for rm in rms:
            self.send_route_mod(entry.ct_id, rm)
        self.acknowledge_route_mod(entry.ct_id, vm_id, vm_port)

    # DatapathPortRegister methods
    def register_dp_port(self, ct_id, dp_id, dp_port):
        """process REGISTER_DP_PORT type RFP messages.

        REGISTER_DP_PORT messages indicate a port on a datapath has been found
        by rfproxy.

        There are two types of ports, external ports and isl ports.

        Firstly the config entry for the port is found, which determines the
        port type.

        Then rftable or rfisl table is checked for an entry indicating the
        associated port has been registered.

        For an isl port if the associated port is registered then the port is
        considered active and can be used for passing traffic. This is
        inadequate checking and should be modified.

        For external ports the two ports also need to determine their mapping
        to the port on the ovs instance on the vm server machine. So their
        status is set to associated until a Virtual Plane Map RFP message
        confirms the association and the port can be made active.

        Args:
            ct_id (int): the id of the controller
            dp_id (int): the id of the switch
            dp_port (int): the id of the port
        """
        stop = self.config_dp(ct_id, dp_id)
        if stop:
            return

        # The logic down here is pretty much the same as register_vm_port
        action = None
        config_entry = self.config.get_config_for_dp_port(ct_id, dp_id,
                                                          dp_port)
        if config_entry is None:
            islconfs = self.islconf.get_entries_by_port(ct_id, dp_id, dp_port)
            if islconfs:
                action = REGISTER_ISL
            else:
                # Register idle DP awaiting for configuration
                action = REGISTER_IDLE
        else:
            entry = self.rftable.get_entry_by_vm_port(config_entry.vm_id,
                                                      config_entry.vm_port)
            # If there's no entry, we have no VM, register DP as idle
            if entry is None:
                action = REGISTER_IDLE
            # If there's an idle VM entry matching configuration, associate
            elif entry.get_status() == RFENTRY_IDLE_VM_PORT:
                action = REGISTER_ASSOCIATED

        # Apply action
        if action == REGISTER_IDLE:
            self.rftable.set_entry(RFEntry(ct_id=ct_id, dp_id=dp_id,
                                           dp_port=dp_port))
            self.log.info("Registering datapath port as idle (dp_id=%s, "
                          "dp_port=%i)" % (format_id(dp_id), dp_port))
        elif action == REGISTER_ASSOCIATED:
            entry.associate(dp_id, dp_port, ct_id)
            self.rftable.set_entry(entry)
            self.log.info("Registering datapath port and associating to "
                          "client port (dp_id=%s, dp_port=%i, vm_id=%s, "
                          "vm_port=%s)" % (format_id(dp_id), dp_port,
                                           format_id(entry.vm_id),
                                           entry.vm_port))
        elif action == REGISTER_ISL:
            self._register_islconf(islconfs, ct_id, dp_id, dp_port)

    def _register_islconf(self, c_entries, ct_id, dp_id, dp_port):
        for conf in c_entries:
            entry = None
            eth_addr = None
            if conf.rem_id != dp_id or conf.rem_ct != ct_id:
                entry = self.isltable.get_entry_by_addr(conf.rem_ct,
                                                        conf.rem_id,
                                                        conf.rem_port,
                                                        conf.rem_eth_addr)
                eth_addr = conf.eth_addr
            else:
                entry = self.isltable.get_entry_by_addr(conf.ct_id,
                                                        conf.dp_id,
                                                        conf.dp_port,
                                                        conf.eth_addr)
                eth_addr = conf.rem_eth_addr

            if entry is None:
                n_entry = RFISLEntry(vm_id=conf.vm_id, ct_id=ct_id,
                                     dp_id=dp_id, dp_port=dp_port,
                                     eth_addr=eth_addr)
                self.isltable.set_entry(n_entry)
                self.log.info("Registering ISL port as idle "
                              "(dp_id=%s, dp_port=%i, eth_addr=%s)" %
                              (format_id(dp_id), dp_port, eth_addr))
            elif entry.get_status() == RFISL_IDLE_DP_PORT:
                entry.associate(ct_id, dp_id, dp_port, eth_addr)
                self.isltable.set_entry(entry)
                n_entry = self.isltable.get_entry_by_remote(entry.ct_id,
                                                            entry.dp_id,
                                                            entry.dp_port,
                                                            entry.eth_addr)
                if n_entry is None:
                    n_entry = RFISLEntry(vm_id=entry.vm_id, ct_id=ct_id,
                                         dp_id=dp_id, dp_port=dp_port,
                                         eth_addr=entry.rem_eth_addr,
                                         rem_ct=entry.ct_id,
                                         rem_id=entry.dp_id,
                                         rem_port=entry.dp_port,
                                         rem_eth_addr=entry.eth_addr)
                    self.isltable.set_entry(n_entry)
                else:
                    n_entry.associate(ct_id, dp_id, dp_port, eth_addr)
                    self.isltable.set_entry(n_entry)
                self.log.info("Registering ISL port and associating to "
                              "remote ISL port (ct_id=%s, dp_id=%s, "
                              "dp_port=%s, rem_ct=%s, rem_id=%s, "
                              "rem_port=%s)" % (ct_id, format_id(dp_id),
                                                dp_port, entry.ct_id,
                                                format_id(entry.dp_id),
                                                entry.dp_port))

    def send_datapath_config_messages(self, ct_id, dp_id):
        rms = self.route_mod_translator[dp_id].configure_datapath()
        for rm in rms:
            self.send_route_mod(ct_id, rm)

    def config_dp(self, ct_id, dp_id):
        """"Send the initial configuration to a datapath when it first comes
        active.

        The configuration is dependant on the datapath, so is handled by
        translators. The initial configuration deletes any existing rules,
        adds the default drop rule and the controller rule for ARP traffic.

        Args:
            ct_id(int): the id of the controller
            dp_id (int): id of the datapath
        """
        if is_rfvs(dp_id):
            return True
        else:
            if (self.rftable.is_dp_registered(ct_id, dp_id) or
                self.isltable.is_dp_registered(ct_id, dp_id)):
                if dp_id not in self.route_mod_translator:
                    self.log.info(
                        "Configuring datapath (dp_id=%s)" % format_id(dp_id))
                    if dp_id in self.multitabledps:
                        self.route_mod_translator[dp_id] = NoviFlowMultitableRouteModTranslator(
                            dp_id, ct_id, self.rftable, self.isltable)
                    elif dp_id in self.satellitedps:
                        self.route_mod_translator[dp_id] = SatelliteRouteModTranslator(
                            dp_id, ct_id, self.rftable, self.isltable)
                    else:
                        self.route_mod_translator[dp_id] = DefaultRouteModTranslator(
                            dp_id, ct_id, self.rftable, self.isltable)
                    self.send_datapath_config_messages(ct_id, dp_id)
            return False
    # DatapathDown methods
    def set_dp_down(self, ct_id, dp_id):
        for entry in self.rftable.get_dp_entries(ct_id, dp_id):
            # For every port registered in that datapath, put it down
            self.set_dp_port_down(entry.ct_id, entry.dp_id, entry.dp_port)
        for entry in self.isltable.get_dp_entries(ct_id, dp_id):
            entry.make_idle(RFISL_IDLE_REMOTE)
            self.isltable.set_entry(entry)
        for entry in self.isltable.get_entries(rem_ct=ct_id, rem_id=dp_id):
            entry.make_idle(RFISL_IDLE_DP_PORT)
            self.isltable.set_entry(entry)
        self.log.info("Datapath down (dp_id=%s)" % format_id(dp_id))

    def set_dp_port_down(self, ct_id, dp_id, dp_port):
        entry = self.rftable.get_entry_by_dp_port(ct_id, dp_id, dp_port)
        if entry is not None:
            # If the DP port is registered, delete it and leave only the
            # associated VM port. Reset this VM port so it can be reused.
            vm_id, vm_port = entry.vm_id, entry.vm_port
            entry.make_idle(RFENTRY_IDLE_VM_PORT)
            self.rftable.set_entry(entry)
            if vm_id is not None:
                self.reset_vm_port(vm_id, vm_port)
            self.log.debug("Datapath port down (dp_id=%s, dp_port=%i)" %
                           (format_id(dp_id), dp_port))

    def reset_vm_port(self, vm_id, vm_port):
        """Send a PCT_RESET message to the vm to inform it when a port has
        gone down."""
        if vm_id is None:
            return
        self.ipc.send(RFCLIENT_RFSERVER_CHANNEL, str(vm_id),
                      PortConfig(vm_id=vm_id, vm_port=vm_port,
                                 operation_id=PCT_RESET))
        self.log.info("Resetting client port (vm_id=%s, vm_port=%i)" %
                      (format_id(vm_id), vm_port))

    # PortMap methods
    def map_port(self, vm_id, vm_port, vs_id, vs_port):
        """Process VIRTUAL_PLANE_MAP messages.

        The virtual plane map message indicates that a mapping packet has been
        received by rfproxy from the specified vm port. That port's status is
        set to active and a data plane map message is sent to rfproxy to
        inform it which datapath port is associated with that vm port, and
        a PortConfig message is sent to the vm to acknowledge the receipt of
        the mapping packet.

        Args:
            vm_id (int): the vm id
            vm_port (int): the vm port
            vs_id (int): the dp id of the server ovs instance from which the
                mapping packet was received
            vs_port (int): the id of the port on the ovs instance
        """
        entry = self.rftable.get_entry_by_vm_port(vm_id, vm_port)
        if entry is not None and entry.get_status() == RFENTRY_ASSOCIATED:
            # If the association is valid, activate it
            entry.activate(vs_id, vs_port)
            self.rftable.set_entry(entry)
            msg = DataPlaneMap(ct_id=entry.ct_id,
                               dp_id=entry.dp_id, dp_port=entry.dp_port,
                               vs_id=vs_id, vs_port=vs_port)
            self.ipc.send(RFSERVER_RFPROXY_CHANNEL, str(entry.ct_id), msg)
            msg = PortConfig(vm_id=vm_id, vm_port=vm_port,
                             operation_id=PCT_MAP_SUCCESS)
            self.ipc.send(RFCLIENT_RFSERVER_CHANNEL, str(entry.vm_id), msg)
            self.log.info("Mapping client-datapath association "
                          "(vm_id=%s, vm_port=%i, dp_id=%s, "
                          "dp_port=%i, vs_id=%s, vs_port=%i)" %
                          (format_id(entry.vm_id), entry.vm_port,
                           format_id(entry.dp_id), entry.dp_port,
                           format_id(entry.vs_id), entry.vs_port))

if __name__ == "__main__":
    description = 'RFServer co-ordinates RFClient and RFProxy instances, ' \
                  'listens for route updates, and configures flow tables'
    epilog = 'Report bugs to: https://github.com/routeflow/RouteFlow/issues'

    config = os.path.dirname(os.path.realpath(__file__)) + "/config.csv"
    islconf = os.path.dirname(os.path.realpath(__file__)) + "/islconf.csv"

    parser = argparse.ArgumentParser(description=description, epilog=epilog)
    parser.add_argument('configfile', default=config,
                        help='VM-VS-DP mapping configuration file')
    parser.add_argument('-i', '--islconfig', default=islconf,
                        help='ISL mapping configuration file')
    parser.add_argument('-m', '--multitabledps', default='',
                        help='List of datapaths that support multiple tables')
    parser.add_argument(
        '-s', '--satellitedps', default='',
        help='List of datapaths that default forward to ISL peer')

    args = parser.parse_args()
    server = RFServer(
        args.configfile, args.islconfig, args.multitabledps, args.satellitedps)
