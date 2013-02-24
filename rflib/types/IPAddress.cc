#include "IPAddress.h"

IPAddress::IPAddress() {
    this->init(IPV4);
}

IPAddress::IPAddress(const int version) {
    this->init(version);
}

IPAddress::IPAddress(const int version, const char* address) {
    string saddress(address);
    this->init(version);
    this->data_from_string(address);
}

IPAddress::IPAddress(const int version, const string &address) {
    this->init(version);
    this->data_from_string(address);
}

/**
 * Create IPAddress based on an IPv4 address, converting to network
 * byte-order for storing internally.
 *
 * data: IPv4 address in host byte-order
 */
IPAddress::IPAddress(const uint32_t data) {
    this->init(IPV4);
    uint32_t moddata = htonl(data);
    memcpy(this->data, &moddata, this->length);
}

IPAddress::IPAddress(const IPAddress &other) {
    this->init(other.getVersion());
    other.toArray(this->data);
}

IPAddress::IPAddress(const int version, const uint8_t* data) {
    this->init(version);
    memcpy(this->data, data, this->length);
}

IPAddress::IPAddress(in_addr data) {
    this->init(IPV4);
    memcpy(this->data, &data, this->length);
}

IPAddress::IPAddress(in6_addr data) {
    this->init(IPV6);
    memcpy(this->data, &data, this->length);
}

IPAddress::IPAddress(const int version, uint8_t prefix_len){
    this -> init(version);
    if (version == IPV4){
        if (prefix_len == 32){
            uint32_t fullmask = (uint32_t) -1;
            memcpy(this->data, &fullmask, this->length);
        }
        else {
            uint32_t mask = htonl(((1 << prefix_len) -1 ) << (32 - prefix_len));
            memcpy(this->data, &mask, this->length);
        }
    }
    else if (version == IPV6){
        for (int i = 0; i < this->length; i++){
            if (prefix_len >= 8){
                this->data[i] = 0xff;
                prefix_len -= 8;
            }
            else if (prefix_len == 0){
                this->data[i] = 0;
            }
            else {
                this->data[i] = ((1 << prefix_len) - 1 ) << (8 - prefix_len);
                prefix_len = 0;
            }
        }
    }
}

IPAddress::~IPAddress() {
    delete this->data;
}

IPAddress& IPAddress::operator=(const IPAddress &other) {
    if (this != &other) {
        delete data;
        this->init(other.getVersion());
        other.toArray(this->data);
    }
    return *this;
}

bool IPAddress::operator==(const IPAddress &other) const {
    return (this->getVersion() == other.getVersion() and
        (memcmp(other.data, this->data, this->length) == 0));
}

/**
 * Returns the in_addr (or in6_addr) structure for this IPAddress
 *
 * The caller is responsible for deleting the returned struct.
 */
void* IPAddress::toInAddr() const {
    void* n;
    if (this->version == IPV4) {
        n = new in_addr;
        memcpy(&((in_addr*) n)->s_addr, this->data, this->length);
    }
    else if (this->version == IPV6) {
        n = new in6_addr;
        memcpy(((in6_addr*) n)->s6_addr, this->data, this->length);
    }
    return n;
}

void IPAddress::toArray(uint8_t* array) const {
    memcpy(array, this->data, this->length);
}

/**
 * Return the IPv4 address in host byte-order
 *
 * Returns 0 on failure (not IPv4 address).
 */
uint32_t IPAddress::toUint32() const {
    if (this->version == IPV4) {
        return ntohl(((in_addr*) this->toInAddr())->s_addr);
    }
    else {
        return 0;
    }
}

string IPAddress::toString() const {
    char* dst = NULL;
    void* n = this->toInAddr();
    if (this->version == IPV4) {
        dst = new char[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, n, dst, INET_ADDRSTRLEN);
        delete (in_addr*) n;
    }
    else if (this->version == IPV6) {
        dst = new char[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, n, dst, INET6_ADDRSTRLEN);
        delete (in6_addr*) n;
    }
    string result = string(dst != NULL? dst : "");
    delete dst;
    return result;
}

uint32_t IPAddress::toPrefixLen() const {
	uint8_t n = 0;
  if (this->version == IPV4){
    uint32_t mask = this->toUint32();

    for (uint8_t i = 0; i < 32; i++) {
      if (((mask >> i) & 0x1) == 0x1) {
        n++;
      }
    }
  }
  else if (this->version == IPV6){
    for (uint8_t i = 0; i < this->length; i++){
      if (data[i] == 0xff){
        n += 8;
      }
      else {
        uint8_t shft;
        for (shft = 1; !((data[i] >> shft) & 1); shft++);
        n += (8 - shft);
        break;
      }
    }
  }
  else {
    n = 0xff;
  }
	return n;
}

uint32_t IPAddress::toCIDRMask() const {
   uint32_t mask = this->toUint32();
        
   uint8_t n = 0;
   for (uint8_t i = 0; i < 32; i++) {
     if (((mask >> i) & 0x1) == 0x1) {
       n++;
     }
  }

  return n;
}
int IPAddress::getVersion() const {
    return this->version;
}

void IPAddress::init(const int version) {
    this->version = version;
    if (this->version == IPV4)
        this->length = 4;
    else if (this->version == IPV6)
        this->length = 16;
    this->data = new uint8_t[this->length];
}

void IPAddress::data_from_string(const string &address) {
    if (this->version == IPV4) {
        struct in_addr n;
        inet_pton(AF_INET, address.c_str(), &n);
        memcpy(this->data, &n.s_addr, 4);
    }
    else if (this->version == IPV6) {
        struct in6_addr n6;
        inet_pton(AF_INET6, address.c_str(), &n6);
        memcpy(this->data, &n6.s6_addr, 16);
    }
}
