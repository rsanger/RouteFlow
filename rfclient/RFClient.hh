#ifndef RFCLIENT_HH
#define RFCLIENT_HH

#include <map>
#include <vector>
#include <string>

#include <netlink/route/link.h>

#include "ipc/IPC.h"
#include "ipc/RFProtocolFactory.h"
#include "FlowTable.hh"
#include "PortMapper.hh"

const int max_rm_outstanding = 1;

class RFClient : private RFProtocolFactory, private IPCMessageProcessor,
                 public InterfaceMap {
    public:
        RFClient(uint64_t id, const string &address, RouteSource);
        bool findInterface(const char *ifName, Interface *dst);

    private:
        FlowTable* flowTable;
        PortMapper* portMapper;
        IPCMessageService* ipc;
        SyncQueue<RouteMod> rm_q;
        boost::mutex rm_outstanding_mutex;
        uint64_t rm_outstanding;
        uint64_t id;

        boost::mutex ifMutex; /* This guards both of the maps below. */
        map<string, Interface> ifacesMap;
        map<int, Interface*> physicalInterfaces;

        uint8_t hwaddress[IFHWADDRLEN];

        void startFlowTable(RouteSource source);
        void startPortMapper();
        bool process(const string &from, const string &to,
                     const string &channel, IPCMessage& msg);
        void sendRm(RouteMod rm);
        RouteMod controllerRouteMod(uint32_t port, uint32_t vlan,
                                    bool matchMac, MACAddress hwaddress,
                                    bool matchIP, const IPAddress &ip_address);
        void sendInterfaceToControllerRouteMods(const Interface &iface);
        void sendAllInterfaceToControllerRouteMods(uint32_t vm_port);
        void deactivateInterfaces(uint32_t vm_port);
        uint32_t get_port_number(string ifName, bool *physical, uint32_t *vlan);
        map<string, Interface> load_interfaces();
};
#endif /* RFCLIENT_HH */
