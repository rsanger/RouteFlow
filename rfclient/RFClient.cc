#include <ifaddrs.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <sys/ioctl.h>

#include <cstdlib>
#include <boost/thread.hpp>

#include "defs.h"
#include "RFClient.hh"

using namespace std;

#define PORT_ERROR (0xffffffff)

/* Get the MAC address of the interface. */
int get_hwaddr_byname(const char * ifname, uint8_t hwaddr[]) {
    struct ifreq ifr;
    int sock;

    if ((NULL == ifname) || (NULL == hwaddr)) {
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';

    if (-1 == ioctl(sock, SIOCGIFHWADDR, &ifr)) {
        char error[BUFSIZ];
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_WARNING, "ioctl(SIOCGIFHWADDR): %s", error);
        return -1;
    }

    std::memcpy(hwaddr, ifr.ifr_ifru.ifru_hwaddr.sa_data, IFHWADDRLEN);

    close(sock);

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
    this->id = id;

    string id_str = to_string<uint64_t>(id);
    syslog(LOG_INFO, "Starting RFClient (vm_id=%s)", id_str.c_str());
    ipc = IPCMessageServiceFactory::forClient(address, id_str);

    vector<Interface> ifaces = this->load_interfaces();
    vector<Interface>::iterator it;
    for (it = ifaces.begin(); it != ifaces.end(); it++) {
        this->ifacesMap[it->name] = *it;
        this->interfaces[it->port] = &this->ifacesMap[it->name];

        PortRegister msg(this->id, it->port, it->hwaddress);
        this->ipc->send(RFCLIENT_RFSERVER_CHANNEL, RFSERVER_ID, msg);
        syslog(LOG_INFO, "Registering client port (vm_port=%d)", it->port);
    }

    this->startFlowTable(source);
    this->startPortMapper(ifaces);

    ipc->listen(RFCLIENT_RFSERVER_CHANNEL, this, this, true);
}

void RFClient::startFlowTable(RouteSource source) {
    this->flowTable = new FlowTable(this->id, this, this->ipc, source);
    boost::thread t(*this->flowTable);
    t.detach();
}

void RFClient::startPortMapper(vector<Interface> ifaces) {
    this->portMapper = new PortMapper(this->id, ifaces);
    boost::thread t(*this->portMapper);
    t.detach();
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
            this->interfaces[vm_port]->active = false;
            break;
        case PCT_MAP_SUCCESS:
            syslog(LOG_INFO, "Successfully mapped port(vm_port=%d)", vm_port);
            this->interfaces[vm_port]->active = true;
            break;
        default:
            syslog(LOG_WARNING, "Recieved unrecognised PortConfig message");
            return false;
        }
    } else {
        return false;
    }

    return true;
}

/* Set the MAC address of the interface. */
int RFClient::set_hwaddr_byname(const char * ifname, uint8_t hwaddr[],
                                int16_t flags) {
    char error[BUFSIZ];
    struct ifreq ifr;
    int sock;

    if ((NULL == ifname) || (NULL == hwaddr)) {
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';
    ifr.ifr_ifru.ifru_flags = flags & (~IFF_UP);

    if (-1 == ioctl(sock, SIOCSIFFLAGS, &ifr)) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_WARNING, "ioctl(SIOCSIFFLAGS): %s", error);
        return -1;
    }

    ifr.ifr_ifru.ifru_hwaddr.sa_family = ARPHRD_ETHER;
    std::memcpy(ifr.ifr_ifru.ifru_hwaddr.sa_data, hwaddr, IFHWADDRLEN);

    if (-1 == ioctl(sock, SIOCSIFHWADDR, &ifr)) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_WARNING, "ioctl(SIOCSIFHWADDR): %s", error);
        return -1;
    }

    ifr.ifr_ifru.ifru_flags = flags | IFF_UP;

    if (-1 == ioctl(sock, SIOCSIFFLAGS, &ifr)) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_WARNING, "ioctl(SIOCSIFFLAGS): %s", error);
        return -1;
    }

    close(sock);

    return 0;
}

/**
 * Converts the given interface string into a logical port number.
 *
 * Returns PORT_ERROR on error.
 */
uint32_t RFClient::get_port_number(string ifName) {
    size_t pos = ifName.find_first_of("123456789");
    if (pos >= ifName.length()) {
        return PORT_ERROR;
    }
    string port_num = ifName.substr(pos, ifName.length() - pos + 1);

    /* TODO: Do a better job of determining interface numbers */
    return atoi(port_num.c_str());
}

/**
 * Gather all of the interfaces on the system.
 *
 * Returns an empty vector on failure.
 */
vector<Interface> RFClient::load_interfaces() {
    struct ifaddrs *ifaddr, *ifa;
    vector<Interface> result;

    if (getifaddrs(&ifaddr) == -1) {
        char error[BUFSIZ];
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "getifaddrs: %s", error);
        return result;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        int family = ifa->ifa_addr->sa_family;

        if (family == AF_PACKET && strcmp(ifa->ifa_name, "eth0") != 0
            && strcmp(ifa->ifa_name, "lo") != 0) {
            string ifaceName = ifa->ifa_name;
            get_hwaddr_byname(ifa->ifa_name, hwaddress);

            Interface interface;
            interface.name = ifaceName;
            interface.port = get_port_number(ifaceName);
            if (interface.port == PORT_ERROR) {
                printf("Ignoring interface %s\n", ifaceName.c_str());
                continue;
            }
            interface.hwaddress = MACAddress(hwaddress);
            interface.active = false;

            printf("Loaded interface %s\n", interface.name.c_str());
            syslog(LOG_INFO, "Loaded interface %s\n", interface.name.c_str());

            result.push_back(interface);
        }
    }

    freeifaddrs(ifaddr);
    return result;
}

void usage(char *name) {
    printf("usage: %s [-f] [-a <address>] [-i <interface>] [-n <id>]\n\n"
           "RFClient subscribes to kernel updates and pushes these to \n"
           "RFServer for further processing.\n\n"
           "Arguments:\n"
           "  -a <address>      Specify the MongoDB address\n"
           "  -i <interface>    Specify which interface to use for client ID\n"
           "  -f                Use the FPM interface for route updates\n"
           "  -n <id>           Manually specify client ID in hex\n\n"
           "  -h                Print Help (this message) and exit\n"
           "\nReport bugs to: https://github.com/routeflow/RouteFlow/issues\n",
           name);
}

int main(int argc, char* argv[]) {
    string address = "";  /* Empty means use default. */
    RouteSource route_source = RS_NETLINK;
    uint64_t id = get_interface_id(DEFAULT_RFCLIENT_INTERFACE);

    char c;
    while ((c = getopt (argc, argv, "a:fi:n:h")) != -1) {
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
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    openlog("rfclient", LOG_NDELAY | LOG_NOWAIT | LOG_PID, SYSLOGFACILITY);
    RFClient s(id, address, route_source);

    return 0;
}
