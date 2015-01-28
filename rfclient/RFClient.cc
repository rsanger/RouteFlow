#include <ifaddrs.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <cstdlib>
#include <boost/thread.hpp>

#include "defs.h"
#include "RFClient.hh"

using namespace std;

#define PORT_ERROR (0xffffffff)


int send_ioctl(const char * ifname, struct ifreq *ifr, int req) {
    if (NULL == ifname) {
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    strncpy(ifr->ifr_name, ifname, sizeof(ifr->ifr_name) - 1);
    ifr->ifr_name[sizeof(ifr->ifr_name) - 1] = '\0';

    int ret = ioctl(sock, req, ifr);
    if (ret < 0) {
        char error[BUFSIZ];
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_WARNING, "ioctl(%d): %s", req, error);
    }

    close(sock);
    return ret;
}

/* Is interface running? */
int is_interface_running(const char * ifname) {
    struct ifreq ifr;

    if (send_ioctl(ifname, &ifr, SIOCGIFFLAGS) < 0) {
        return -1;
    }    

    return ifr.ifr_flags & IFF_RUNNING;
}

/* Get the MAC address of the interface. */
int get_hwaddr_byname(const char * ifname, uint8_t hwaddr[]) {
    if ((NULL == ifname) || (NULL == hwaddr)) {
        return -1;
    }

    struct ifreq ifr; 
    if (send_ioctl(ifname, &ifr, SIOCGIFHWADDR)) {
        return -1;
    }

    std::memcpy(hwaddr, ifr.ifr_ifru.ifru_hwaddr.sa_data, IFHWADDRLEN);
    return 0;
}

/* Get the interface associated VM identification number. */
uint64_t get_interface_id(const char *ifname) {
    if (ifname == NULL)
        return 0;

    uint8_t mac[6];
    uint64_t id;
    stringstream hexmac;

    if (get_hwaddr_byname(ifname, mac) == -1) {
        return 0;
    }

    for (int i = 0; i < 6; i++) {
        hexmac << std::hex << setfill('0') << setw(2) << (int)mac[i];
    }
    hexmac >> id;
    return id;
}

bool RFClient::findInterface(const char *ifName, Interface *dst) {
    boost::lock_guard<boost::mutex> lock(this->ifMutex);

    map<string, Interface>::iterator it = this->ifacesMap.find(ifName);
    if (it == this->ifacesMap.end()) {
        return false;
    }

    *dst = it->second;
    return true;
}

RFClient::RFClient(uint64_t id, const string &address, RouteSource source) {
    this->rm_outstanding = 0;
    this->id = id;

    string id_str = to_string<uint64_t>(id);
    syslog(LOG_INFO, "Starting RFClient (vm_id=%s)", id_str.c_str());
    ipc = IPCMessageServiceFactory::forClient(address, id_str);
    this->ifacesMap = this->load_interfaces();
    syslog(LOG_INFO, "loaded %lu interfaces", this->ifacesMap.size());
    if (this->ifacesMap.size() == 0) {
        exit(-1);
    }

    {
        std::vector<IPAddress> ip_addresses;
        boost::lock_guard<boost::mutex> lock(this->ifMutex);
        map<string, Interface>::iterator it;
        for (it = this->ifacesMap.begin(); it != this->ifacesMap.end(); ++it) {
            const Interface &iface = it->second;
            if (iface.physical) {
                this->physicalInterfaces[iface.port] = &it->second;
                PortRegister msg(this->id, iface.port, iface.hwaddress);
                this->ipc->send(RFCLIENT_RFSERVER_CHANNEL, RFSERVER_ID, msg);
                syslog(LOG_INFO,
                       "Registering client port (vm_port=%d)",
                       iface.port);
            }
        }
    }

    this->startFlowTable(source);
    this->startPortMapper();

    ipc->listen(RFCLIENT_RFSERVER_CHANNEL, this, this, false);

    for (;;) {
       bool flow_control = false;
       {
          boost::lock_guard<boost::mutex> lock(this->rm_outstanding_mutex);
          if (this->rm_outstanding > max_rm_outstanding) {
            flow_control = true;
          }
       }
       if (flow_control) {
          usleep(100);
          continue;
       }
       RouteMod rm;
       this->rm_q.wait_and_pop(rm);
       this->ipc->send(RFCLIENT_RFSERVER_CHANNEL, RFSERVER_ID, rm);
       {
          boost::lock_guard<boost::mutex> lock(this->rm_outstanding_mutex);
          ++(this->rm_outstanding);
       }
    }
}

void RFClient::startFlowTable(RouteSource source) {
    this->flowTable = new FlowTable(this->id, this, &(this->rm_q), source);
    boost::thread t(*this->flowTable);
    t.detach();
}

void RFClient::startPortMapper() {
    this->portMapper = new PortMapper(this->id, &(this->physicalInterfaces), &(this->ifMutex));
    boost::thread t(*this->portMapper);
    t.detach();
}

void RFClient::sendRm(RouteMod rm) {
    this->rm_q.push(rm);
}

RouteMod RFClient::controllerRouteMod(uint32_t port, uint32_t vlan,
                                      bool matchMac, MACAddress hwaddress,
                                      bool matchIP, const IPAddress &ip_address) {
    RouteMod rm;
    rm.set_mod(RMT_CONTROLLER);
    rm.set_id(this->flowTable->get_vm_id());
    rm.set_vm_port(port);
    if (matchMac) {
        rm.add_match(Match(RFMT_ETHERNET, hwaddress.toString()));
    }
    if (vlan) {
        rm.add_match(Match(RFMT_VLAN_ID, vlan));
    }
    if (matchIP){
        if (ip_address.getVersion() == IPV4) {
            rm.add_match(Match(RFMT_IPV4, ip_address, IPAddress(IPV4, FULL_IPV4_PREFIX)));
        } else {
            rm.add_match(Match(RFMT_IPV6, ip_address, IPAddress(IPV6, FULL_IPV6_PREFIX)));
        }
    }
    rm.add_option(Option(RFOT_PRIORITY, (uint16_t)PRIORITY_HIGH));
    return rm;
}

void RFClient::sendInterfaceToControllerRouteMods(const Interface &iface) {
    uint32_t port = iface.port;
    uint32_t vlan = iface.vlan;
    MACAddress hwaddress = iface.hwaddress;
    std::vector<IPAddress>::const_iterator it;
    RouteMod rm;
    for (it = iface.addresses.begin();
         it != iface.addresses.end();
         ++it) {
         if (it->getVersion() == IPV4) {
             /* ICMP traffic. */
             rm = controllerRouteMod(port, vlan, true, hwaddress, true, *it);
             rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_ICMP));
             sendRm(rm);
             /* ARP */
             rm = controllerRouteMod(port, vlan, true, hwaddress, false, *it);
             rm.add_match(Match(RFMT_ETHERTYPE, (uint16_t)ETHERTYPE_ARP));
             sendRm(rm);
         } else {
             /* TODO: handle neighbor solicitation et al specifically, 
                      like we do for IPv4 and ARP. Will need to implement
                      IPV6/ICMP6 type code checking. */
             rm = controllerRouteMod(port, vlan, false, hwaddress, false, *it);
             rm.add_match(Match(RFMT_ETHERTYPE, (uint16_t)ETHERTYPE_IPV6));
             rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_ICMPV6));
             rm.add_option(Option(RFOT_PRIORITY, (uint16_t)(PRIORITY_LOW + 1)));
             sendRm(rm);
             rm = controllerRouteMod(port, vlan, true, hwaddress, true , *it);
             rm.add_match(Match(RFMT_ETHERTYPE, (uint16_t)ETHERTYPE_IPV6));
             rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_ICMPV6));
             sendRm(rm);
         }
         /* BGP */
         rm = controllerRouteMod(port, vlan, true, hwaddress, true, *it);
         rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_TCP));
         rm.add_match(Match(RFMT_TP_SRC, (uint16_t)TPORT_BGP));
         sendRm(rm);
         rm = controllerRouteMod(port, vlan, true, hwaddress, true, *it);
         rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_TCP));
         rm.add_match(Match(RFMT_TP_DST, (uint16_t)TPORT_BGP));
         sendRm(rm);
         /* OSPF for IPv4 */
         rm = controllerRouteMod(port, vlan, false, hwaddress, false, *it);
         rm.add_match(Match(RFMT_ETHERTYPE, (uint16_t)ETHERTYPE_IP));
         rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_OSPF));
         sendRm(rm);
         /* OSPF for IPv6 */
         rm = controllerRouteMod(port, vlan, false, hwaddress, false, *it);
         rm.add_match(Match(RFMT_ETHERTYPE, (uint16_t)ETHERTYPE_IPV6));
         rm.add_match(Match(RFMT_NW_PROTO, (uint16_t)IPPROTO_OSPF));
         sendRm(rm);
         /* TODO: add other IGP traffic here - RIPv2 et al */
    }
}

void RFClient::sendAllInterfaceToControllerRouteMods(uint32_t vm_port) {
    std::map<string, Interface>::iterator it;
    for (it = this->ifacesMap.begin();
         it != this->ifacesMap.end();
         ++it) {
        Interface &iface = it->second;
        if (iface.port == vm_port) {
            iface.active = true;
            sendInterfaceToControllerRouteMods(iface);
        }
    }
}

void RFClient::deactivateInterfaces(uint32_t vm_port) {
    std::map<string, Interface>::iterator it;
    for (it = this->ifacesMap.begin();
         it != this->ifacesMap.end();
         ++it) {
        Interface &iface = it->second;
        if (iface.port == vm_port) {
            iface.active = false;
        }
    }
}

bool RFClient::process(const string &, const string &, const string &,
                       IPCMessage& msg) {
    int type = msg.get_type();
    if (type == PORT_CONFIG) {
        boost::lock_guard<boost::mutex> lock(this->ifMutex);

        PortConfig *config = static_cast<PortConfig*>(&msg);
        uint32_t vm_port = config->get_vm_port();
        uint32_t operation_id = config->get_operation_id();

        switch (operation_id) {
            case PCT_MAP_REQUEST:
                syslog(LOG_WARNING, "Received deprecated PortConfig (vm_port=%d) "
                                    "(type: %d)", vm_port, operation_id);
                break;
            case PCT_RESET:
                syslog(LOG_INFO, "Received port reset (vm_port=%d)", vm_port);
                deactivateInterfaces(vm_port);
                break;
            case PCT_MAP_SUCCESS:
                syslog(LOG_INFO, "Successfully mapped port (vm_port=%d)", vm_port);
                sendAllInterfaceToControllerRouteMods(vm_port);
                break;
            case PCT_ROUTEMOD_ACK:
                syslog(LOG_DEBUG, "Got RouteMod ack (vm_port=%d)", vm_port);
                {
                    boost::lock_guard<boost::mutex> lock(this->rm_outstanding_mutex);
                    --(this->rm_outstanding);
                }
                break;
            default:
                syslog(LOG_WARNING, "Received unrecognised PortConfig message");
                return false;
        }
    } else {
        syslog(LOG_WARNING, "Got unknown msg %d", type);
        return false;
    }

    return true;
}

/**
 * Converts the given interface string into a logical port number
 * (ignoring VLAN number).
 *
 * Returns PORT_ERROR on error.
 */
uint32_t RFClient::get_port_number(string ifName, bool *physical, uint32_t *vlan) {
    const char kDigits[] = "0123456789";
    size_t first_pos = ifName.find_first_of(kDigits);
    if (first_pos == string::npos) {
        return PORT_ERROR;
    }
    string port_num;
    size_t last_pos = ifName.find_first_not_of(kDigits, first_pos);
    if (last_pos == string::npos) {
        *physical = true;
        port_num = ifName.substr(first_pos, string::npos);
    } else {
        *physical = false;
        port_num = ifName.substr(first_pos, last_pos - first_pos);
        string vlan_num = ifName.substr(last_pos + 1, string::npos);
        *vlan = atoi(vlan_num.c_str());
    }
    return atoi(port_num.c_str());
}

/**
 * Gather all of the OF-mapped interfaces on the system.
 *
 * Returns an empty vector on failure.
 */
map<string, Interface> RFClient::load_interfaces() {
    struct ifaddrs *ifaddr, *ifa;
    map<string, Interface> interfaces;

    if (getifaddrs(&ifaddr) == -1) {
        char error[BUFSIZ];
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "getifaddrs: %s", error);
        return interfaces;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family != AF_PACKET) {
            continue;
        }

        if (strncmp(ifa->ifa_name, "eth", strlen("eth")) != 0) {
            continue;
        }

        if (strcmp(ifa->ifa_name, DEFAULT_RFCLIENT_INTERFACE) == 0) {
            continue;
        }

        string ifaceName = ifa->ifa_name;
        bool physical = false;
        uint32_t vlan = 0;
        uint32_t port = get_port_number(ifaceName, &physical, &vlan);

        if (port == PORT_ERROR) {
            syslog(LOG_INFO, 
                   "Cannot get port number for %s, ignoring\n",
                   ifaceName.c_str());
            continue;
        }

        while (is_interface_running(ifaceName.c_str()) <= 0) {
           syslog(LOG_INFO,
                  "Waiting for %s to come up\n",
                  ifaceName.c_str());
           sleep(1);
        }

        get_hwaddr_byname(ifaceName.c_str(), hwaddress);

        interfaces[ifaceName] = Interface();
        Interface &interface = interfaces[ifaceName];
        interface.name = ifaceName;
        interface.port = port;
        interface.hwaddress = MACAddress(hwaddress);
        interface.active = false;
        interface.physical = physical;
        interface.vlan = vlan;
    }

    map<string, Interface>::iterator it;
    char ip_addr[NI_MAXHOST];

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        it = interfaces.find(ifa->ifa_name);
        if (it == interfaces.end()) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) {
            getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                        ip_addr, sizeof(ip_addr), NULL, 0, NI_NUMERICHOST);
            it->second.addresses.push_back(IPAddress(IPV4, ip_addr));
        }
        if (ifa->ifa_addr->sa_family == AF_INET6) {
            getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
                        ip_addr, sizeof(ip_addr), NULL, 0, NI_NUMERICHOST);
            // Drop interface scope if present.
            char *scope_delim = strchr(ip_addr, '%');
            if (scope_delim) {
                *scope_delim = 0;
            }
            it->second.addresses.push_back(IPAddress(IPV6, ip_addr));           
        }
    }

    freeifaddrs(ifaddr);

    for (it = interfaces.begin(); it != interfaces.end(); ++it) {
        syslog(LOG_INFO, "loaded interface: %s", it->second.toString().c_str());
        std::vector<IPAddress>::iterator ip_it;
        for (ip_it = it->second.addresses.begin(); ip_it != it->second.addresses.end(); ++ip_it) {
            syslog(LOG_INFO, "interface %s has IP address %s", it->first.c_str(), ip_it->toString().c_str());
        }
    }

    return interfaces;
}

void usage(char *name) {
    printf("usage: %s [-f] [-a <address>] [-i <interface>] [-n <id>]\n\n"
           "RFClient subscribes to kernel updates and pushes these to \n"
           "RFServer for further processing.\n\n"
           "Arguments:\n"
           "  -a <address>      Specify the address for RFServer\n"
           "  -i <interface>    Specify which interface to use for client ID\n"
           "  -f                Use the FPM interface for route updates\n"
           "  -n <id>           Manually specify client ID in hex\n\n"
           "  -h                Print Help (this message) and exit\n"
           "  -v                Print the version number and exit\n"
           "\nReport bugs to: https://github.com/routeflow/RouteFlow/issues\n",
           name);
}

int main(int argc, char* argv[]) {
    string address = "";  /* Empty means use default. */
    RouteSource route_source = RS_NETLINK;
    uint64_t id = get_interface_id(DEFAULT_RFCLIENT_INTERFACE);

    char c;
    while ((c = getopt (argc, argv, "a:fi:n:hv")) != -1) {
        switch(c) {
        case 'a':
            address = optarg;
            break;
        case 'f':
            route_source = RS_FPM;
            break;
        case 'i':
            id = get_interface_id(optarg);
            break;
        case 'n':
            id = strtol(optarg, NULL, 16);
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        case 'v':
            printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
            return 0;
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    openlog("rfclient", LOG_NDELAY | LOG_NOWAIT | LOG_PID, SYSLOGFACILITY);
    RFClient s(id, address, route_source);

    return 0;
}
