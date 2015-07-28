#!/usr/bin/env python
#-*- coding:utf-8 -*-

from ryu.ofproto import ofproto_v1_3, ether
from rftable import *
from rflib.types.Match import *
from rflib.types.Action import *
import logging

import os
import argparse
import subprocess

logging.basicConfig(
    level=logging.INFO,
    format='%(name)-15s %(levelname)-8s %(message)s',
    datefmt='%b %d %H:%M:%S'
    )

class MetaLabel:
    """An abstract class to label packets with additional meta information

    See MetaVLAN for an implementation
    Adds the port as meta data to an OpenFlow action and assigns labels etc.

    Functions included are
    ovs: Open vSwitch commands for ovs-ofctl add-flows (Passed as a list of strings)
    of: OpenFlow commands for RYU
    rf: RouteFlow commands
    """

    def ovsaction_push_meta(self, label, action=None):
        """Creates/appends to an OVS action list the addition of a meta label"""
        raise NotImplementedError("Should have implemented this")


    def ofaction_push_meta(self, parser, label, action=None):
        """Creates/appends to a Ryu action the addition of a meta label"""
        raise NotImplementedError("Should have implemented this" )

    def rfaction_push_meta(self, label, routemod):
        """Appends to a RouteFlow routemod the addition of a meta label"""
        raise NotImplementedError("Should have implemented this" )

    def ovsaction_pop_meta(self, action=None):
        """Creates/appends to an OVS action list the removal of a meta label"""
        raise NotImplementedError("Should have implemented this")

    def ofaction_pop_meta(self, parser, action=None):
        """Creates/appends to a Ryu action the removal of a meta label"""
        raise NotImplementedError("Should have implemented this" )

    def rfaction_pop_meta(self, routemod):
        """Appends to a RouteFlow routemod the removal of a meta label"""
        raise NotImplementedError("Should have implemented this" )

    def ovsmatch_meta(self, label, match=None):
        """Creates/appends to an OVS match list the meta label match"""
        raise NotImplementedError("Should have implemented this" )

    def ofmatch_meta(self, parser, label, match=None):
        """Creates/appends to a a Ryu match the meta label match"""
        raise NotImplementedError("Should have implemented this" )

    def rfmatch_meta(self, label, routemod):
        """Appends to a RouteFlow routemod the meta label match"""
        raise NotImplementedError("Should have implemented this" )

    def allocate_label(self):
        """Allocates a new label"""
        raise NotImplementedError("Should have implemented this" )

    def bad_label(self):
        """Returns a non existant label, indicating unassigned"""
        return -1

class MetaVLAN(MetaLabel):
    label = 2

    def ovsaction_push_meta(self, label, action=None):
         if action == None:
             action = []
         action += ["push_vlan:0x%0.4X" % ether.ETH_TYPE_8021Q]
         action += ["mod_vlan_vid:%d" % label]
         return action

    def ofaction_push_meta(self, parser, label, action=None):
        if action == None:
            action = []
        action += [parser.OFPActionPushVlan(ether.ETH_TYPE_8021Q),
               parser.OFPActionSetField(vlan_vid=(label|ofproto_v1_3.OFPVID_PRESENT))]
        return action

    def rfaction_push_meta(self, label, routemod):
        routemod.add_action(Action.SET_VLAN_ID(label))
        return routemod

    def ovsaction_pop_meta(self, action=None):
        if action == None:
            action = []
        action += ["strip_vlan"]
        return action

    def ofaction_pop_meta(self, parser, action=None):
        if action == None:
            action = []
        action.append(parser.OFPActionPopVlan())
        return action

    def rfaction_pop_meta(self, routemod):
        routemod.add_action(Action.STRIP_VLAN())
        return routemod

    def ofmatch_meta(self, parser, label, match=None):
        if match == None:
            match = {}
        match['vlan_vid'] = label|ofproto_v1_3.OFPVID_PRESENT
        return match

    def ovsmatch_meta(self, label, match=None):
        if match == None:
            match = []
        match += ["dl_vlan=%d" % label]
        return match

    def rfmatch_meta(self, label, routemod):
        routemod.add_match(Match.VLAN_ID(label))
        return routemod

    def allocate_label(self):
        ret = self.label
        self.label += 1
        if ret >= (1<<11):
            raise OverflowError("We've run out of VLAN labels for ports")
        return ret

class Log:
    def info(self, string):
        print string

def shortest_recursive(labeller, log, check_set, conf, fpconf, islconf):
    """Finds the shortest path to the controller for every OF switch

    We label paths (isls and fastpath links) with a list of labels stored
    as the 'fast_paths' attribute. This is set and stored against the configuration.

    Multiple fastpaths can be used. Much like spanning tree we will dedicate a
    port upwards towards the controller on each switch.

    """
    if not log:
        log = logging.getLogger("rffastpath")
    next_set = []
    for ct_id, parent_dpid, link in check_set:
        # Find the oppisite end of the link from the parent
        # NOTE the link could be a fplink and not have a rem_id or a isl link
        # with both dp_id and rem_id
        if parent_dpid == link.dp_id:
            my_dpid = link.rem_id
        else:
            my_dpid = link.dp_id
        # Allocate label for all directly attached ports as these will use
        # link to do fast path (assuming someone didn't beat us to it)
        ports = conf.get_config_for_dp(ct_id, my_dpid)
        if ports == None:
            ports = []
        for port in ports:
            if not hasattr(port, "fp_label"):
                port.fp_label = labeller.allocate_label()
                conf.set_entry(port)
                # Format (label, direct connection, vm port (i.e. dp0 port))
                link.fast_paths.append((port.fp_label, port.vm_port))
            else:
                # Someones already tagged this
                log.info("Skipping dp_id %d already fastpath'd" % my_dpid)
                link.fp_master = None
                 # Save the updated fast path attachments to DB
                if isinstance(link, RFISLConfEntry):
                    islconf.set_entry(link)
                elif isinstance(link, RFFPConfEntry):
                    fpconf.set_entry(link)
                else:
                    raise TypeError("Expected either a ISL or fastpath config")
                break
        else: # nobreak
            link.fp_master = parent_dpid
            log.info("Adding fastpath parent %d to %d" %(parent_dpid, my_dpid))
            # Save the updated fast path attachments to DB
            if isinstance(link, RFISLConfEntry):
                islconf.set_entry(link)
            elif isinstance(link, RFFPConfEntry):
                fpconf.set_entry(link)
            else:
                raise TypeError("Expected either a ISL or fastpath config")
            # Build a list of isl to check next iteration
            for next_isl in islconf.get_entries_by_dpid(ct_id, my_dpid):
                if not hasattr(next_isl, "fast_paths"):
                    next_isl.fast_paths = []
                    islconf.set_entry(next_isl)
                    next_set.append((ct_id, my_dpid, next_isl))
    # Do recursion
    if len(next_set) > 0:
        shortest_recursive(labeller, log, next_set, conf, fpconf, islconf)

    # Note which labels are using this path to the parent i.e. pull these up
    it = iter(check_set)
    x = it.next()[2]
    for _, parent_dpid, next_isl in next_set:
        while (x.dp_id != parent_dpid and (x.rem_id if hasattr(x,"rem_id") else -1) != parent_dpid):
            x = it.next()[2]
        x.fast_paths += next_isl.fast_paths
        if isinstance(x, RFISLConfEntry):
            islconf.set_entry(x)
        elif isinstance(x, RFFPConfEntry):
            fpconf.set_entry(x)
        else:
            raise TypeError("Expected either a ISL or fastpath config")
    # Print out these sets
    for _, _, link in check_set:
        if len(link.fast_paths):
            log.info("Link %d -> %d carrying labels %s" % (
                link.rem_id if link.dp_id == link.fp_master else link.dp_id,
                link.fp_master, link.fast_paths))


def fp_allocate_labels(labeller, log, conf, fpconf, islconf):
    """Allocates labels and paths from each port to the controller"""
    fplinks = fpconf.get_entries_all()
    # Are we doing fastpath ?
    if not fplinks:
        return

    next_set = []
    for fplink in fplinks:
        next_set.append((fplink.ct_id, -1, fplink))

    shortest_recursive(labeller, log, next_set, conf, fpconf, islconf)
    return

if __name__ == "__main__":
    """This loads all normal RouteFlow config and will setup dp0 with rules
    for fastpath
    """
    description = 'FastPath for RouteFlow'
    epilog = 'Report bugs to your vendor'

    config = os.path.dirname(os.path.realpath(__file__)) + "/config.csv"
    islconf = os.path.dirname(os.path.realpath(__file__)) + "/islconf.csv"

    parser = argparse.ArgumentParser(description=description, epilog=epilog)
    parser.add_argument('configfile', default=config,
                        help='VM-VS-DP mapping configuration file')
    parser.add_argument('-i', '--islconfig', default=islconf,
                        help='ISL mapping configuration file')
    parser.add_argument('-m', '--multitabledps', default='',
                        help='List of datapaths that support multiple tables')
    parser.add_argument('-s', '--satellitedps', default='',
                        help='List of datapaths that default forward to ISL peer')
    parser.add_argument('-f', '--fastpaths', default='',
                        help='List of "fastpath" link(s) to the controller')
    parser.add_argument('-v', '--ovsofctl', default='',
                        help='The ovs-ofctl command to run')
    parser.add_argument('-d', '--dp0', default='',
                        help='The controller to run')

    args = parser.parse_args()

    config = RFConfig(args.configfile)
    islconf = RFISLConf(args.islconfig)
    fpconf = RFFPConf(args.fastpaths)
    ovsctl = args.ovsofctl.split(" ")
    dp0 = args.dp0

    labeller = MetaVLAN()
    fp_allocate_labels(labeller, None, config, fpconf, islconf)

    using_fastpath = False

    for fplink in fpconf.get_entries_all():
        for label, vm_port in fplink.fast_paths:
            using_fastpath = True
            # Add match for LXC->dp0->network
            match = ["in_port=%d" % vm_port, "priority=32800"]
            action = labeller.ovsaction_push_meta(label)
            action += ["output=%d" % fplink.dp0_port]
            flow = ",".join(match) + ",actions="+",".join(action)
            subprocess.call(ovsctl + ["add-flow", dp0, flow])

            # Add match for network->dp0->LXC
            match = labeller.ovsmatch_meta(label)
            match += ["in_port=%d" % fplink.dp0_port, "priority=32800"]
            action = labeller.ovsaction_pop_meta()
            action += ["output=%d" % vm_port]
            flow = ",".join(match) + ",actions="+",".join(action)
            subprocess.call(ovsctl + ["add-flow", dp0, flow])

    if using_fastpath:
        # Mapping packets are sent to match the lxc ports to dp0 ports
        # These are passed directly to Ryu and sent to RFServer/RFClient
        subprocess.call(ovsctl + ["add-flow", dp0,
            "priority=32801,dl_dst=00:00:00:00:00:00,actions=CONTROLLER:65509"])
