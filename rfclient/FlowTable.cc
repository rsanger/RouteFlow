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

#include <netlink/cache.h>
#include <netlink/route/rtnl.h>
#include <netlink/route/route.h>
#include <netlink/route/neighbour.h>
#include <netlink/types.h>

using namespace std;

#define EMPTY_MAC_ADDRESS "00:00:00:00:00:00"
#define ROUTE_COOLDOWN 5000 /* milliseconds */

#ifndef IF_NAMESIZE
#define IF_NAMESIZE 32
#endif

static const MACAddress MAC_ADDR_NONE(EMPTY_MAC_ADDRESS);

// TODO: implement a way to pause the flow table updates when the VM is not
//       associated with a valid datapath

/**
 * Change callbacks are called when a new netlink message is received.
 */
static void HTChangeCb( struct nl_cache *cache,
                        struct nl_object *obj,
                        int action,
                        void *arg) {
    struct rtnl_neigh *neigh;
    neigh = (struct rtnl_neigh *) obj;
    FlowTable *ft = reinterpret_cast<FlowTable *>(arg);
    ft->updateHostTable(neigh, action);
}

static void RTChangeCb( struct nl_cache *cache,
                        struct nl_object *obj,
                        int action,
                        void *arg) {
    struct rtnl_route *route;
    route = (struct rtnl_route *) obj;
    FlowTable *ft = reinterpret_cast<FlowTable *>(arg);
    ft->updateRouteTable(route, action);
}

/**
 * Iter callbacks are called for each entry in the host and route tables when
 * the caches are initialised.
 */
static void HTIterCb(struct nl_object *obj, void *arg) {
    struct rtnl_neigh *neigh;
    neigh = (struct rtnl_neigh *) obj;
    FlowTable *ft = reinterpret_cast<FlowTable *>(arg);
    ft->updateHostTable(neigh, NL_ACT_NEW);
}

static void RTIterCb(struct nl_object *obj, void *arg) {
    struct rtnl_route *route;
    route = (struct rtnl_route *) obj;
    FlowTable *ft = reinterpret_cast<FlowTable *>(arg);
    ft->updateRouteTable(route, NL_ACT_NEW);
}

FlowTable::FlowTable(uint64_t id, InterfaceMap *ifMap, SyncQueue<RouteMod> *rm_q, RouteSource source) {
    this->vm_id = id;
    this->ifMap = ifMap;
    this->rm_q = rm_q;
    this->source = source;
}

FlowTable::FlowTable(const FlowTable& other) {
    this->vm_id = other.vm_id;
    this->ifMap = other.ifMap;
    this->rm_q = other.rm_q;
    this->source = other.source;
}

void FlowTable::operator()() {
    switch (this->source) {
        case RS_NETLINK: {
            initNLListener();
            break;
        }
        default: {
            syslog(LOG_CRIT, "Invalid route source specified. Disabling route updates.");
            break;
        }
    }

    GWResolver = boost::thread(&FlowTable::GWResolverCb, this);
    GWResolver.join();
}

/**
 * Initialises the libnl rtnetlink caches.
 *
 * Initialises the socket for the cache manager, the cache manager itself, and
 * a link cache, a neighbour cache and a route cache.
 *
 * It then iterates over the existing entries in the neighbour and route caches
 * and applies HTIterCb and RTIterCb to them respectively.
 *
 * It then starts the NLListener thread to poll the socket for updates.
 *
 * One socket is used to receive updates for all three caches.
 */
void FlowTable::initNLListener() {
    int err;
    syslog(LOG_NOTICE, "Netlink interface enabled");
    sock = nl_socket_alloc();

    err = nl_cache_mngr_alloc(sock, NETLINK_ROUTE, 0, &mngr);
    if (err < 0) {
        throw "some kind of problem with the route cache manager!";
    }

    err = rtnl_route_alloc_cache(sock, AF_UNSPEC, 0, &rt_cache);
    if (err < 0) {
        throw "some kind of problem with the route cache!";
    }
    err = nl_cache_mngr_add_cache(mngr, rt_cache, &RTChangeCb, this);
    if (err < 0) {
        throw "some kind of problem with adding route cache!";
    }

    err = rtnl_neigh_alloc_cache(sock, &ht_cache);
    if (err < 0) {
        throw "some kind of problem with the neigh cache!";
    }
    err = nl_cache_mngr_add_cache(mngr, ht_cache, &HTChangeCb, this);
    if (err < 0) {
        throw "some kind of problem with adding neigh cache!";
    }

    err = rtnl_link_alloc_cache(sock, AF_UNSPEC, &link_cache);
    if (err < 0) {
        throw "some kind of problem with the route cache!";
    }
    err = nl_cache_mngr_add_cache(mngr, link_cache, NULL, NULL);
    if (err < 0) {
        throw "some kind of problem with adding the link cache!";
    }

    nl_cache_foreach(ht_cache, &HTIterCb, this);
    nl_cache_foreach(rt_cache, &RTIterCb, this);

    NLListener = boost::thread(&FlowTable::NLListenCb, this);
}

/**
 * callback for the NL Listener thread. It checks for new netlink messages.
 * When new messages are found the cache change callbacks will handle updates.
 */
void FlowTable::NLListenCb(FlowTable *ft) {
    int status;
    while (1) {
        status = nl_cache_mngr_poll(ft->mngr, 500);
        if (status < 0) {
            throw "Error polling for new netlink messages";
        }
    }
}

uint64_t FlowTable::get_vm_id() {
    return this->vm_id;
}

void FlowTable::clear() {
    this->routeTable.clear();
    boost::lock_guard<boost::mutex> lock(hostTableMutex);
    this->hostTable.clear();
}

void FlowTable::interrupt() {
    GWResolver.interrupt();
    NLListener.interrupt();
}

void FlowTable::sendRm(RouteMod rm) {
    this->rm_q->push(rm);
}

void FlowTable::GWResolverCb(FlowTable *ft) {
    while (true) {
        PendingRoute pr;
        while (ft->pendingRoutes.front(pr)) {
            ft->pendingRoutes.pop();

            const RouteEntry& re = pr.rentry;
            const string re_key = re.toString();
            const string addr_str = re.address.toString();
            const string mask_str = re.netmask.toString();
            const string gw_str = re.gateway.toString();
            bool existingEntry = ft->routeTable.count(re_key) > 0;

            switch (pr.type) {
                case RMT_ADD:
                    if (existingEntry) {
                        syslog(LOG_ERR,
                              "Received duplicate route add for %s",
                              addr_str.c_str());
                    } else {
                        ft->routeTable.insert(make_pair(re_key, re));
                        if (ft->findHost(re.gateway) == MAC_ADDR_NONE) {
                            syslog(LOG_ERR,
                                   "Cannot resolve gateway %s, will retry route %s/%s",
                                    gw_str.c_str(), addr_str.c_str(), mask_str.c_str());
                            ft->unresolvedRoutes.insert(re_key);
                        } else {
                            syslog(LOG_DEBUG,
                                  "Adding route %s/%s via %s",
                                  addr_str.c_str(), mask_str.c_str(), gw_str.c_str());
                            ft->sendToHw(pr.type, pr.rentry);
                        }
                    }
                    break;

                case RMT_DELETE:
                    if (existingEntry) {
                        ft->routeTable.erase(re_key);
                        ft->unresolvedRoutes.erase(re_key);
                        syslog(LOG_DEBUG,
                               "Deleting route %s/%s via %s",
                               addr_str.c_str(), mask_str.c_str(), gw_str.c_str());
                        ft->sendToHw(pr.type, pr.rentry);
                          
                    } else {
                        syslog(LOG_ERR,
                               "Received route removal for %s but not in routing table",
                               addr_str.c_str());
                    }
                    break;

                default:
                    syslog(LOG_ERR,
                           "Received unexpected RouteModType (%d)",
                           pr.type);
                    break;
            }
        }
        if (ft->unresolvedRoutes.size() > 0) {
            set<string> resolvedRoutes;
            set<string>::iterator it;
            uint64_t gatewaysResolved = 0;
            for (it = ft->unresolvedRoutes.begin(); it != ft->unresolvedRoutes.end(); ++it) {
                const RouteEntry& re = ft->routeTable[*it];
                const string addr_str = re.address.toString();
                const string mask_str = re.netmask.toString();
                const string gw_str = re.gateway.toString();
                if (ft->findHost(re.gateway) == MAC_ADDR_NONE) {
                    syslog(LOG_DEBUG,
                          "Still cannot resolve gateway %s, will retry route %s/%s",
                          gw_str.c_str(), addr_str.c_str(), mask_str.c_str());
                    ft->resolveGateway(re.gateway, re.interface);
                } else {
                    syslog(LOG_DEBUG,
                           "Adding previously unresolved route %s/%s via %s",
                           addr_str.c_str(), mask_str.c_str(), gw_str.c_str());
                    ft->sendToHw(RMT_ADD, re);
                    resolvedRoutes.insert(*it);
                    ++gatewaysResolved;
                }
            }
            if (gatewaysResolved) {
                for (it = resolvedRoutes.begin(); it != resolvedRoutes.end(); ++it) {
                    ft->unresolvedRoutes.erase(*it);    
                }
            }
        } else {
            usleep(1000);
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

/**
 * When the netlink neighbour cache recieves an update, this function
 * generates a HostEntry object, adds it to the hostTable, sends it to the
 * hardware and stops neighbour discovery for that host
 *
 * args:
 *  neigh - the libnl neighbour object received by the cache
 *  action - the netlink message action
 */
void FlowTable::updateHostTable(struct rtnl_neigh *neigh,
                                int action) {
    struct nl_addr *nw_addr, *hw_addr;
    int ifindex, state;

    boost::this_thread::interruption_point();

    if (action != NL_ACT_NEW) {
        /**
         * TODO: We should definitely include support for hosts being deleted.
         * It is also possible that hosts will get lost as they are
         * added if they exist in the cache and change from a non NUD_VALID
         * state to a NUD_VALID state.
         */
        return;
    }

    state = rtnl_neigh_get_state(neigh);
    if (!(state & NUD_VALID)) {
        /**
         * TODO: NUD_VALID includes stale entries, we may wish to handle these
         * differently to NUD_REACHABLE entries.
         */
        return;
    }

    ifindex = rtnl_neigh_get_ifindex(neigh);
    char intf[IF_NAMESIZE + 1];
    memset(intf, 0, IF_NAMESIZE + 1);
    rtnl_link_i2name(link_cache, ifindex, intf, sizeof(intf));
    if (strcmp(intf, DEFAULT_RFCLIENT_INTERFACE) == 0) {
        return;
    }

    boost::scoped_ptr<HostEntry> hentry(new HostEntry());

    hentry->address = IPAddress(rtnl_neigh_get_dst(neigh));
    const string host = hentry->address.toString();

    if (getInterface(intf, "host", &hentry->interface) != 0) {
        return;
    }

    int MACBUFSIZ = 2 * IFHWADDRLEN + 5 + 1;
    char mac[MACBUFSIZ];
    memset(mac, 0, MACBUFSIZ);
    hw_addr = rtnl_neigh_get_lladdr(neigh);
    nl_addr2str(hw_addr, mac, MACBUFSIZ);
    if (strcmp((char *) mac, "none") == 0) {
        syslog(LOG_DEBUG, "Received host entry ip=%s with blank mac. Ignoring", host.c_str());
        return;
    }
    hentry->hwaddress = MACAddress(mac);

    switch (action) {
        case NL_ACT_NEW: {
            syslog(LOG_DEBUG, "netlink->RTM_NEWNEIGH: ip=%s, mac=%s",
                   host.c_str(), mac);
            this->sendToHw(RMT_ADD, *hentry);
            {
                // Add to host table
                boost::lock_guard<boost::mutex> lock(hostTableMutex);
                this->hostTable[host] = *hentry;
            }
            // If we have been attempting neighbour discovery for this
            // host, then we can close the associated socket.
            stopND(host);
            break;
        }
    }

    return;
}

/**
 * When the netlink route table cache recieves an update, this function
 *
 * args:
 *  neigh - the libnl neighbour object received by the cache
 *  action - the netlink message action
 */
void FlowTable::updateRouteTable(  struct rtnl_route *route,
                                   int action) {
    struct nl_addr *address, *gateway;
    struct rtnl_nexthop *nh;
    int family, nhcount, i, prefix_len, ifindex;

    boost::this_thread::interruption_point();

    if (!(action == NL_ACT_NEW || action == NL_ACT_DEL)) {
        return;
    }

    boost::scoped_ptr<RouteEntry> rentry(new RouteEntry());

    char intf[IF_NAMESIZE + 1];
    memset(intf, 0, IF_NAMESIZE + 1);

    if (rtnl_route_get_table(route) != RT_TABLE_MAIN) {
        syslog(LOG_DEBUG, "received route with invalid table, ignoring");
        return;
    }

    switch (rtnl_route_get_family(route)) {
    case AF_INET:
        family = IPV4;
        break;
    case AF_INET6:
        family = IPV6;
        break;
    default:
        syslog(LOG_DEBUG, "received route with invalid family, ignoring");
        return;
    }

    /**
     * the route gateway is given as a list of nexthop objects, which may or
     * may not actually have a legitimate gateway.
     * When there is more than one legitimate gateway, we select one
     * arbitrarily.
     */
    nhcount = rtnl_route_get_nnexthops(route);
    for (i = 0; i < nhcount; i++) {
        nh = rtnl_route_nexthop_n(route, i);
        gateway = rtnl_route_nh_get_gateway(nh);
        if (gateway) {
            break;
        }
    }
    if (!gateway) {
            syslog(LOG_DEBUG, "received route with no gateway, ignoring");
            return;
    }

    rentry->gateway = IPAddress(gateway);

    ifindex = rtnl_route_nh_get_ifindex(nh);
    rtnl_link_i2name(link_cache, ifindex, intf, sizeof(intf));

    if (strcmp(intf, DEFAULT_RFCLIENT_INTERFACE) == 0) {
        syslog(LOG_DEBUG, "received route for rfclient interface, ignoring");
        return;
    }

    if (getInterface(intf, "route", &rentry->interface) != 0) {
        syslog(LOG_DEBUG, "unable to retrieve interface for route, ignoring");
        return;
    }

    address = rtnl_route_get_dst(route);
    rentry->address = IPAddress(address);

    prefix_len = nl_addr_get_prefixlen(address);
    rentry->netmask = IPAddress(family, prefix_len);
    if (prefix_len == 0) {
        /* Default route. Zero the address. */
        rentry->address = rentry->netmask;
    }

    string net = rentry->address.toString();
    string mask = rentry->netmask.toString();
    string gw = rentry->gateway.toString();

    switch (action) {
        case NL_ACT_NEW:
            syslog(LOG_DEBUG, "netlink->RTM_NEWROUTE: net=%s, mask=%s, gw=%s",
                   net.c_str(), mask.c_str(), gw.c_str());
            this->pendingRoutes.push(PendingRoute(RMT_ADD, *rentry));
            break;
        case NL_ACT_DEL:
            syslog(LOG_DEBUG, "netlink->RTM_DELROUTE: net=%s, mask=%s, gw=%s",
                   net.c_str(), mask.c_str(), gw.c_str());
            this->pendingRoutes.push(PendingRoute(RMT_DELETE, *rentry));
            break;
        default:
            syslog(LOG_DEBUG, "route with invalid action, ignoring");
    }

    return;
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

void FlowTable::stopND(const string &host) {
    char error[BUFSIZ];
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

/**
 * Initiates the gateway resolution process for the given host.
 *
 * Returns 0 on success, -1 on error (usually an issue with the socket)
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
        syslog(LOG_DEBUG, "already doing neighbour discovery for %s", gateway_str.c_str());
        return 0;
    }

    // Otherwise, we should go ahead and begin the process.
    syslog(LOG_DEBUG, "starting neighbour discovery for %s", gateway_str.c_str());
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
    rm.add_action(Action(RFAT_SET_ETH_SRC, local_iface.hwaddress));
    rm.add_action(Action(RFAT_SET_ETH_DST, gateway));

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
    const MACAddress& remoteMac = findHost(re.gateway);
    if (remoteMac == MAC_ADDR_NONE) {
        syslog(LOG_ERR, "Cannot resolve %s", gateway_str.c_str());
        return -1;
    }

    return sendToHw(mod, re.address, re.netmask, re.interface, remoteMac);
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
        syslog(LOG_ERR, "Cannot send RouteMod for down port %s\n", local_iface.name.c_str());
        return -1;
    }

    RouteMod rm;

    rm.set_mod(mod);
    rm.set_id(FlowTable::vm_id);

    if (local_iface.vlan) {
        // rm.add_match(Match(RFMT_VLAN, local_iface.vlan));
        rm.add_action(Action(RFAT_SWAP_VLAN_ID, local_iface.vlan));
    }

    const string gw_str = gateway.toString();

    if (setEthernet(rm, local_iface, gateway) != 0) {
        syslog(LOG_ERR, "cannot setEthernet for %s", gw_str.c_str());
        return -1;
    }
    if (setIP(rm, addr, mask) != 0) {
        syslog(LOG_ERR, "cannot setIP for %s", gw_str.c_str());
        return -1;
    }

    /* Add the output port. Even if we're removing the route, RFServer requires
     * the port to determine which datapath to send to. */
    // rm.add_action(Action(RFAT_OUTPUT, local_iface.port));
    rm.set_vm_port(local_iface.port);

    syslog(LOG_DEBUG, "sending rfserver IPC for %s/%s via %s on port %u",
                      addr.toString().c_str(), mask.toString().c_str(),
                      gw_str.c_str(), local_iface.port);
    this->sendRm(rm);
    return 0;
}
