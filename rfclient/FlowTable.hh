#ifndef FLOWTABLE_HH_
#define FLOWTABLE_HH_

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>

#include <list>
#include <map>
#include <set>
#include <stdint.h>
#include <boost/thread.hpp>
#include "SyncQueue.hh"

#include "ipc/IPC.h"
#include "ipc/RFProtocol.h"
#include "types/IPAddress.h"
#include "types/MACAddress.h"
#include "defs.h"

#include "Interface.hh"
#include "RouteEntry.hh"
#include "HostEntry.hh"
#include "PendingRoute.hh"

using namespace std;

const int nl_buffersize = 128 * 1024 * 1024;

typedef enum route_source {
    RS_NETLINK,
    RS_FPM,
} RouteSource;

#ifndef NL_ACT_MAX
/* This part of the netlink api is not publically available until libnl-3.2.25.
 * 3.2.21 is what is currently available from apt, so I include this here.
 */
enum {
    NL_ACT_UNSPEC,
    NL_ACT_NEW,
    NL_ACT_DEL,
    NL_ACT_GET,
    NL_ACT_SET,
    NL_ACT_CHANGE,
    __NL_ACT_MAX,
};
#define NL_ACT_MAX (__NL_ACT_MAX -1)
#endif

#ifndef NUD_VALID
#define NUD_VALID (NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE|NUD_PROBE|NUD_STALE|NUD_DELAY)
#endif

class FlowTable {
    public:
        FlowTable(uint64_t vm_id,
                  InterfaceMap *ifMap,
                  SyncQueue<RouteMod> *rm_q,
                  RouteSource src);
        FlowTable(const FlowTable&);

        static void GWResolverCb(FlowTable *ft);
        static void NLListenCb(FlowTable *ft);
        void operator()();

        void clear();
        void interrupt();
        void print_test();

        void sendRm(RouteMod rm);
        void updateHostTable(struct rtnl_neigh *neigh, int action);
        void updateRouteTable(struct rtnl_route *route, int action);
        uint64_t get_vm_id();

    private:
        RouteSource source;
        InterfaceMap* ifMap;
        SyncQueue<RouteMod> *rm_q;
        uint64_t vm_id;

        boost::thread GWResolver;
        boost::thread NLListener;

        /* NetLink handlers */
        struct nl_sock *sock;
        struct nl_cache *rt_cache;
        struct nl_cache *ht_cache;
        struct nl_cache *link_cache;
        struct nl_cache_mngr *mngr;

        /* Known hosts */
        map<string, HostEntry> hostTable;
        boost::mutex hostTableMutex;

        /* Routing table change requests. */
        SyncQueue<PendingRoute> pendingRoutes;

        /* Routing table. */
        map<string, RouteEntry> routeTable;

        /* Routes with unresolved nexthops */
        set<string> unresolvedRoutes;

        /* Cached neighbour discovery sockets */
        map<string, int> pendingNeighbours;
        boost::mutex ndMutex;

        bool is_port_down(uint32_t port);
        int getInterface( const char *iface, const char *type, Interface*);

        void initNLListener();
        int initiateND(const char *hostAddr);
        void stopND(const string &hostAddr);
        int resolveGateway(const IPAddress&, const Interface&);
        const MACAddress& findHost(const IPAddress& host);

        int setEthernet(RouteMod& rm, const Interface& local_iface,
                        const MACAddress& gateway);
        int setIP(RouteMod& rm, const IPAddress& addr, const IPAddress& mask);
        int sendToHw(RouteModType, const RouteEntry&);
        int sendToHw(RouteModType, const HostEntry&);
        int sendToHw(RouteModType, const IPAddress& addr,
                     const IPAddress& mask, const Interface&,
                     const MACAddress& gateway);
};

#endif /* FLOWTABLE_HH_ */
