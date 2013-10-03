#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <sys/socket.h>
#include <time.h>
#include <syslog.h>

#include <string>
#include <vector>
#include <cstring>
#include <iostream>

#include "converter.h"
#include "FlowTable.hh"
#include "FPMServer.hh"

using namespace std;

#define FULL_IPV4_PREFIX 32
#define FULL_IPV6_PREFIX 128

#define EMPTY_MAC_ADDRESS "00:00:00:00:00:00"
#define ROUTE_COOLDOWN 5000 /* milliseconds */

static const MACAddress MAC_ADDR_NONE(EMPTY_MAC_ADDRESS);

// TODO: implement a way to pause the flow table updates when the VM is not
//       associated with a valid datapath

static int HTPollingCb(const struct sockaddr_nl*, struct nlmsghdr *n,
                       void *arg) {
    FlowTable *ft = reinterpret_cast<FlowTable *>(arg);
    return ft->updateHostTable(n);
}

static int RTPollingCb(const struct sockaddr_nl*, struct nlmsghdr *n,
                       void *arg) {
    FlowTable *ft = reinterpret_cast<FlowTable *>(arg);
    return ft->updateRouteTable(n);
}

FlowTable::FlowTable(uint64_t id, InterfaceMap *ifm, IPCMessageService *ipc,
                     RouteSource source) {
    this->vm_id = id;
    this->ifMap = ifm;
    this->ipc = ipc;
    this->source = source;
}

FlowTable::FlowTable(const FlowTable& other) {
    this->vm_id = other.vm_id;
    this->ifMap = other.ifMap;
    this->ipc = other.ipc;
    this->source = other.source;
}

void FlowTable::operator()() {
    FPMServer *fpm;
    rtnl_open(&rthNeigh, RTMGRP_NEIGH);
    HTPolling = boost::thread(&rtnl_listen, &rthNeigh, &HTPollingCb, this);

    switch (this->source) {
    case RS_NETLINK: {
        syslog(LOG_NOTICE, "Netlink interface enabled");
        rtnl_open(&rth, RTMGRP_IPV4_MROUTE | RTMGRP_IPV4_ROUTE
                        | RTMGRP_IPV6_MROUTE | RTMGRP_IPV6_ROUTE);
        RTPolling = boost::thread(&rtnl_listen, &rth, &RTPollingCb, this);
        break;
    }
    case RS_FPM: {
        fpm = new FPMServer(this);
        RTPolling = boost::thread(*fpm);
        syslog(LOG_NOTICE, "FPM interface enabled");
        break;
    }
    default:
        syslog(LOG_CRIT, "Invalid route source specified. Disabling route "
               "updates.");
        break;
    }

    GWResolver = boost::thread(&FlowTable::GWResolverCb, this);
    GWResolver.join();
}

void FlowTable::clear() {
    this->routeTable.clear();
    boost::lock_guard<boost::mutex> lock(hostTableMutex);
    this->hostTable.clear();
}

void FlowTable::interrupt() {
    HTPolling.interrupt();
    GWResolver.interrupt();
    RTPolling.interrupt();
}

void FlowTable::GWResolverCb(FlowTable *ft) {
    while (true) {
        PendingRoute pr;
        ft->pendingRoutes.wait_and_pop(pr);

        /* If the head of the list is in no hurry to be resolved,
         * then let's just sleep for a while until it's ready. */
        if (boost::get_system_time() < pr.time) {
            syslog(LOG_DEBUG, "GWResolver is getting sleepy...");
            boost::this_thread::sleep(pr.time);
        }
        pr.advance(ROUTE_COOLDOWN);

        bool existingEntry = false;
        std::list<RouteEntry>::iterator iter = ft->routeTable.begin();
        for (; iter != ft->routeTable.end(); iter++) {
            if (pr.rentry == *iter) {
                existingEntry = true;
                break;
            }
        }

        if (ft->deletedRoutes.count(pr.rentry.toString()) > 0) {
            boost::lock_guard<boost::mutex> lock(ft->drMutex);
            ft->deletedRoutes.erase(pr.rentry.toString());
            if (pr.type != RMT_DELETE) {
                syslog(LOG_INFO, "Removing cached route before insertion.");
                continue;
            }
        }

        if (existingEntry && pr.type == RMT_ADD) {
            syslog(LOG_INFO, "Received duplicate route add for route %s\n",
                   pr.rentry.address.toString().c_str());
            continue;
        }

        if (!existingEntry && pr.type == RMT_DELETE) {
            syslog(LOG_INFO, "Received route removal for %s but route %s.\n",
                   pr.rentry.address.toString().c_str(), "cannot be found");
            continue;
        }

        const RouteEntry& re = pr.rentry;
        if (pr.type != RMT_DELETE
            && ft->findHost(re.gateway) == MAC_ADDR_NONE) {
            /* Host is unresolved. Attempt to resolve it. */
            if (ft->resolveGateway(re.gateway, re.interface) < 0) {
                /* If we can't resolve the gateway, put it to the end of the
                 * queue. Routes with unresolvable gateways will constantly
                 * loop through this code, popping and re-pushing. */
                syslog(LOG_WARNING, "An error occurred while attempting to "
                       "resolve %s/%s.\n", re.address.toString().c_str(),
                       re.netmask.toString().c_str());
            } else {
                /* A resolution is scheduled, so try again later. */
                ft->pendingRoutes.push(pr);
            }
            continue;
        }

        if (ft->sendToHw(pr.type, pr.rentry) < 0) {
            syslog(LOG_WARNING, "An error occurred while pushing %s/%s.\n",
                   re.address.toString().c_str(),
                   re.netmask.toString().c_str());
            ft->pendingRoutes.push(pr);
            continue;
        }

        if (pr.type == RMT_ADD) {
            ft->routeTable.push_back(pr.rentry);
        } else if (pr.type == RMT_DELETE) {
            ft->routeTable.remove(pr.rentry);
        } else {
            syslog(LOG_ERR, "Received unexpected RouteModType (%d)\n",
                   pr.type);
        }
    }
}

/**
 * Get the local interface corresponding to the given interface number.
 *
 * On success, overwrites given interface pointer with the active interface
 * and returns 0;
 * On error, logs it and returns -1.
 */
int FlowTable::getInterface(const char *intf, const char *type,
                            Interface *iface) {
    Interface temp;
    if (!ifMap->findInterface(intf, &temp)) {
        syslog(LOG_ERR, "Interface %s not found, dropping %s entry\n",
               intf, type);
        return -1;
    }

    *iface = temp;
    return 0;
}

int rta_to_ip(unsigned char family, const void *ip, IPAddress& result) {
    if (family == AF_INET) {
        result = IPAddress(reinterpret_cast<const struct in_addr *>(ip));
    } else if (family == AF_INET6) {
        result = IPAddress(reinterpret_cast<const struct in6_addr *>(ip));
    } else {
        syslog(LOG_ERR, "Unrecognised nlmsg family");
        return -1;
    }

    if (result.toString() == "") {
        syslog(LOG_WARNING, "Blank IP address. Dropping Route\n");
        return -1;
    }

    return 0;
}

int FlowTable::updateHostTable(struct nlmsghdr *n) {
    char error[BUFSIZ];
    struct ndmsg *ndmsg_ptr = (struct ndmsg *) NLMSG_DATA(n);
    struct rtattr *rtattr_ptr;

    char intf[IF_NAMESIZE + 1];
    memset(intf, 0, IF_NAMESIZE + 1);

    boost::this_thread::interruption_point();

    if (if_indextoname((unsigned int) ndmsg_ptr->ndm_ifindex, (char *) intf) == NULL) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "HostTable: %s", error);
        return 0;
    }

    /*
    if (ndmsg_ptr->ndm_state != NUD_REACHABLE) {
        cout << "ndm_state: " << (uint16_t) ndmsg_ptr->ndm_state << endl;
        return 0;
    }
    */

    boost::scoped_ptr<HostEntry> hentry(new HostEntry());

    char mac[2 * IFHWADDRLEN + 5 + 1];
    memset(mac, 0, 2 * IFHWADDRLEN + 5 + 1);

    rtattr_ptr = (struct rtattr *) RTM_RTA(ndmsg_ptr);
    int rtmsg_len = RTM_PAYLOAD(n);

    for (; RTA_OK(rtattr_ptr, rtmsg_len); rtattr_ptr = RTA_NEXT(rtattr_ptr, rtmsg_len)) {
        switch (rtattr_ptr->rta_type) {
        case RTA_DST: {
            if (rta_to_ip(ndmsg_ptr->ndm_family, RTA_DATA(rtattr_ptr),
                          hentry->address) < 0) {
                return 0;
            }
            break;
        }
        case NDA_LLADDR:
            if (strncpy(mac, ether_ntoa(((ether_addr *) RTA_DATA(rtattr_ptr))), sizeof(mac)) == NULL) {
                strerror_r(errno, error, BUFSIZ);
                syslog(LOG_ERR, "HostTable: %s", error);
                return 0;
            }
            break;
        default:
            break;
        }
    }

    hentry->hwaddress = MACAddress(mac);
    if (getInterface(intf, "host", &hentry->interface) != 0) {
        return 0;
    }

    if (strlen(mac) == 0) {
        syslog(LOG_INFO, "Received host entry with blank mac. Ignoring\n");
        return 0;
    }

    switch (n->nlmsg_type) {
        case RTM_NEWNEIGH: {
            this->sendToHw(RMT_ADD, *hentry);

            string host = hentry->address.toString();
            {
                // Add to host table
                boost::lock_guard<boost::mutex> lock(hostTableMutex);
                this->hostTable[host] = *hentry;
            }
            {
                // If we have been attempting neighbour discovery for this
                // host, then we can close the associated socket.
                boost::lock_guard<boost::mutex> lock(ndMutex);
                map<string, int>::iterator iter = pendingNeighbours.find(host);
                if (iter != pendingNeighbours.end()) {
                    if (close(iter->second) == -1) {
                        strerror_r(errno, error, BUFSIZ);
                        syslog(LOG_ERR, "pendingNeighbours: %s", error);
                    }
                    pendingNeighbours.erase(host);
                }
            }

            syslog(LOG_INFO, "netlink->RTM_NEWNEIGH: ip=%s, mac=%s",
                   host.c_str(), mac);
            break;
        }
        /* TODO: enable this? It is causing serious problems. Why?
        case RTM_DELNEIGH: {
            this->sendToHw(RMT_DELETE, *hentry);
            // TODO: delete from hostTable
            boost::lock_guard<boost::mutex> lock(hostTableMutex);
            break;
        }
        */
    }

    return 0;
}

int FlowTable::updateRouteTable(struct nlmsghdr *n) {
    struct rtmsg *rtmsg_ptr = (struct rtmsg *) NLMSG_DATA(n);

    boost::this_thread::interruption_point();

    if (!((n->nlmsg_type == RTM_NEWROUTE || n->nlmsg_type == RTM_DELROUTE) &&
          rtmsg_ptr->rtm_table == RT_TABLE_MAIN)) {
        return 0;
    }

    boost::scoped_ptr<RouteEntry> rentry(new RouteEntry());

    char intf[IF_NAMESIZE + 1];
    memset(intf, 0, IF_NAMESIZE + 1);

    struct rtattr *rtattr_ptr;
    rtattr_ptr = (struct rtattr *) RTM_RTA(rtmsg_ptr);
    int rtmsg_len = RTM_PAYLOAD(n);

    for (; RTA_OK(rtattr_ptr, rtmsg_len); rtattr_ptr = RTA_NEXT(rtattr_ptr, rtmsg_len)) {
        switch (rtattr_ptr->rta_type) {
        case RTA_DST:
            if (rta_to_ip(rtmsg_ptr->rtm_family, RTA_DATA(rtattr_ptr),
                          rentry->address) < 0) {
                return 0;
            }
            break;
        case RTA_GATEWAY:
            if (rta_to_ip(rtmsg_ptr->rtm_family, RTA_DATA(rtattr_ptr),
                          rentry->gateway) < 0) {
                return 0;
            }
            break;
        case RTA_OIF:
            if_indextoname(*((int *) RTA_DATA(rtattr_ptr)), (char *) intf);
            break;
        case RTA_MULTIPATH: {
            struct rtnexthop *rtnhp_ptr = (struct rtnexthop *) RTA_DATA(
                    rtattr_ptr);
            int rtnhp_len = RTA_PAYLOAD(rtattr_ptr);

            if (rtnhp_len < (int) sizeof(*rtnhp_ptr)) {
                break;
            }

            if (rtnhp_ptr->rtnh_len > rtnhp_len) {
                break;
            }

            if_indextoname(rtnhp_ptr->rtnh_ifindex, (char *) intf);

            int attrlen = rtnhp_len - sizeof(struct rtnexthop);

            if (attrlen) {
                struct rtattr *attr = RTNH_DATA(rtnhp_ptr);

                for (; RTA_OK(attr, attrlen); attr = RTA_NEXT(attr, attrlen))
                    if (attr->rta_type == RTA_GATEWAY) {
                        if (rta_to_ip(rtmsg_ptr->rtm_family, RTA_DATA(attr),
                                      rentry->gateway) < 0) {
                            return 0;
                        }
                        break;
                    }
            }
        }
            break;
        default:
            break;
        }
    }

    rentry->netmask = IPAddress(IPV4, rtmsg_ptr->rtm_dst_len);
    if (rtmsg_ptr->rtm_dst_len == 0) {
        /* Default route. Zero the address. */
        rentry->address = rentry->netmask;
    }

    if (getInterface(intf, "route", &rentry->interface) != 0) {
        return 0;
    }

    string net = rentry->address.toString();
    string mask = rentry->netmask.toString();
    string gw = rentry->gateway.toString();

    switch (n->nlmsg_type) {
        case RTM_NEWROUTE:
            syslog(LOG_INFO, "netlink->RTM_NEWROUTE: net=%s, mask=%s, gw=%s",
                   net.c_str(), mask.c_str(), gw.c_str());
            this->pendingRoutes.push(PendingRoute(RMT_ADD, *rentry));
            break;
        case RTM_DELROUTE: {
            boost::lock_guard<boost::mutex> lock(drMutex);
            std::pair<string, RouteEntry> pair(rentry->toString(), *rentry);

            syslog(LOG_INFO, "netlink->RTM_DELROUTE: net=%s, mask=%s, gw=%s",
                   net.c_str(), mask.c_str(), gw.c_str());
            this->pendingRoutes.push(PendingRoute(RMT_DELETE, *rentry));
            this->deletedRoutes.insert(pair);
            break;
        }
    }

    return 0;
}

/**
 * Begins the neighbour discovery process to the specified host.
 *
 * Returns an open socket on success, or -1 on error.
 */
int FlowTable::initiateND(const char *hostAddr) {
    char error[BUFSIZ];
    int s, flags;
    struct sockaddr_storage store;
    struct sockaddr_in *sin = (struct sockaddr_in*)&store;
    struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)&store;

    memset(&store, 0, sizeof(store));

    if (inet_pton(AF_INET, hostAddr, &sin->sin_addr) == 1) {
        store.ss_family = AF_INET;
    } else if (inet_pton(AF_INET6, hostAddr, &sin6->sin6_addr) == 1) {
        store.ss_family = AF_INET6;
        syslog(LOG_ERR, "Refusing to initiateND() for IPv6: %s\n", hostAddr);
        return -1;
    } else {
        syslog(LOG_ERR, "Invalid address family for IP \"%s\". Refusing to "
               "initiateND().\n", hostAddr);
        return -1;
    }

    if ((s = socket(store.ss_family, SOCK_STREAM, 0)) < 0) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "socket(): %s", error);
        return -1;
    }

    // Prevent the connect() call from blocking
    flags = fcntl(s, F_GETFL, 0);
    if (fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "fcntl(): %s", error);
        close(s);
        return -1;
    }

    connect(s, (struct sockaddr *)&store, sizeof(store));
    return s;
}

/**
 * Initiates the gateway resolution process for the given host.
 *
 * Returns:
 *  0 if address resolution is currently being performed
 * -1 on error (usually an issue with the socket)
 */
int FlowTable::resolveGateway(const IPAddress& gateway,
                              const Interface& iface) {
    if (!iface.active) {
        return -1;
    }

    string gateway_str = gateway.toString();

    // If we already initiated neighbour discovery for this gateway, return.
    boost::lock_guard<boost::mutex> lock(ndMutex);
    if (pendingNeighbours.find(gateway_str) != pendingNeighbours.end()) {
        return 0;
    }

    // Otherwise, we should go ahead and begin the process.
    int sock = initiateND(gateway_str.c_str());
    if (sock == -1) {
        return -1;
    }
    this->pendingNeighbours[gateway_str] = sock;

    return 0;
}

/**
 * Find the MAC Address for the given host in a thread-safe manner.
 *
 * This searches the internal hostTable structure for the given host, and
 * returns its MAC Address. If the host is unresolved, this will return
 * MAC_ADDR_NONE. Neighbour Discovery is not performed by this function.
 */
const MACAddress& FlowTable::findHost(const IPAddress& host) {
    boost::lock_guard<boost::mutex> lock(hostTableMutex);
    map<string, HostEntry>::iterator iter;
    iter = FlowTable::hostTable.find(host.toString());
    if (iter != FlowTable::hostTable.end()) {
        return iter->second.hwaddress;
    }

    return MAC_ADDR_NONE;
}

int FlowTable::setEthernet(RouteMod& rm, const Interface& local_iface,
                           const MACAddress& gateway) {
    /* RFServer adds the Ethernet match to the flow, so we don't need to. */
    // rm.add_match(Match(RFMT_ETHERNET, local_iface.hwaddress));

    if (rm.get_mod() != RMT_DELETE) {
        rm.add_action(Action(RFAT_SET_ETH_SRC, local_iface.hwaddress));
        rm.add_action(Action(RFAT_SET_ETH_DST, gateway));
    }

    return 0;
}

int FlowTable::setIP(RouteMod& rm, const IPAddress& addr,
                     const IPAddress& mask) {
     if (addr.getVersion() == IPV4) {
        rm.add_match(Match(RFMT_IPV4, addr, mask));
    } else if (addr.getVersion() == IPV6) {
        rm.add_match(Match(RFMT_IPV6, addr, mask));
    } else {
        syslog(LOG_ERR, "Invalid address family for IP %s\n",
               addr.toString().c_str());
        return -1;
    }

    uint16_t priority = PRIORITY_LOW;
    priority += (mask.toPrefixLen() * PRIORITY_BAND);
    rm.add_option(Option(RFOT_PRIORITY, priority));

    return 0;
}

int FlowTable::sendToHw(RouteModType mod, const RouteEntry& re) {
    const string gateway_str = re.gateway.toString();
    if (mod == RMT_DELETE) {
        return sendToHw(mod, re.address, re.netmask, re.interface,
                        MAC_ADDR_NONE);
    } else if (mod == RMT_ADD) {
        const MACAddress& remoteMac = findHost(re.gateway);
        if (remoteMac == MAC_ADDR_NONE) {
            syslog(LOG_INFO, "Cannot Resolve %s\n", gateway_str.c_str());
            return -1;
        }

        return sendToHw(mod, re.address, re.netmask, re.interface, remoteMac);
    }

    syslog(LOG_ERR, "Unhandled RouteModType (%d)\n", mod);
    return -1;
}

int FlowTable::sendToHw(RouteModType mod, const HostEntry& he) {
    boost::scoped_ptr<IPAddress> mask;

    if (he.address.getVersion() == IPV6) {
        mask.reset(new IPAddress(IPV6, FULL_IPV6_PREFIX));
    } else if (he.address.getVersion() == IPV4) {
        mask.reset(new IPAddress(IPV4, FULL_IPV4_PREFIX));
    } else {
        syslog(LOG_ERR, "Received HostEntry with invalid address family\n");
        return -1;
    }

    return sendToHw(mod, he.address, *mask.get(), he.interface, he.hwaddress);
}

int FlowTable::sendToHw(RouteModType mod, const IPAddress& addr,
                         const IPAddress& mask, const Interface& local_iface,
                         const MACAddress& gateway) {
    if (!local_iface.active) {
        syslog(LOG_INFO, "Cannot send RouteMod for down port\n");
        return -1;
    }

    RouteMod rm;

    rm.set_mod(mod);
    rm.set_id(FlowTable::vm_id);

    if (setEthernet(rm, local_iface, gateway) != 0) {
        return -1;
    }
    if (setIP(rm, addr, mask) != 0) {
        return -1;
    }

    /* Add the output port. Even if we're removing the route, RFServer requires
     * the port to determine which datapath to send to. */
    rm.add_action(Action(RFAT_OUTPUT, local_iface.port));

    this->ipc->send(RFCLIENT_RFSERVER_CHANNEL, RFSERVER_ID, rm);
    return 0;
}

/*
 * Add or remove a Push, Pop or Swap operation matching on a label only
 * For matching on IP, update FTN (not yet implemented) is needed
 *
 * TODO: If an error occurs here, the NHLFE is silently dropped. Fix this.
 */
void FlowTable::updateNHLFE(nhlfe_msg_t *nhlfe_msg) {
    RouteMod msg;

    if (nhlfe_msg->table_operation == ADD_LSP) {
        msg.set_mod(RMT_ADD);
    } else if (nhlfe_msg->table_operation == REMOVE_LSP) {
        msg.set_mod(RMT_DELETE);
    } else {
        syslog(LOG_WARNING, "Unrecognised NHLFE table operation %d",
               nhlfe_msg->table_operation);
        return;
    }
    msg.set_id(FlowTable::vm_id);

    // We need the next-hop IP to determine which interface to use.
    int version = nhlfe_msg->ip_version;
    uint8_t* ip_data = reinterpret_cast<uint8_t*>(&nhlfe_msg->next_hop_ip);
    IPAddress gwIP(version, ip_data);

    // Get our interface for packet egress.
    Interface iface;
    map<string, HostEntry>::iterator iter;
    iter = FlowTable::hostTable.find(gwIP.toString());
    if (iter == FlowTable::hostTable.end()) {
        syslog(LOG_WARNING, "Failed to locate interface for LSP");
        return;
    } else {
        iface = iter->second.interface;
    }

    if (!iface.active) {
        syslog(LOG_WARNING, "Cannot send route via inactive interface");
        return;
    }

    // Get the MAC address corresponding to our gateway.
    const MACAddress& gwMAC = findHost(gwIP);
    if (gwMAC == MAC_ADDR_NONE) {
        syslog(LOG_ERR, "Failed to resolve gateway MAC for NHLFE");
        return;
    }

    if (setEthernet(msg, iface, gwMAC) != 0) {
        return;
    }

    // Match on in_label only - matching on IP is the domain of FTN not NHLFE
    msg.add_match(Match(RFMT_MPLS, nhlfe_msg->in_label));

    if (nhlfe_msg->nhlfe_operation == PUSH) {
        msg.add_action(Action(RFAT_PUSH_MPLS, ntohl(nhlfe_msg->out_label)));
    } else if (nhlfe_msg->nhlfe_operation == POP) {
        msg.add_action(Action(RFAT_POP_MPLS, (uint32_t)0));
    } else if (nhlfe_msg->nhlfe_operation == SWAP) {
        msg.add_action(Action(RFAT_SWAP_MPLS, ntohl(nhlfe_msg->out_label)));
    } else {
        syslog(LOG_ERR, "Unknown lsp_operation");
        return;
    }

    msg.add_action(Action(RFAT_OUTPUT, iface.port));

    this->ipc->send(RFCLIENT_RFSERVER_CHANNEL, RFSERVER_ID, msg);

    return;
}
