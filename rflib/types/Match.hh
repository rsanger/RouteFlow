#ifndef __MATCH_HH__
#define __MATCH_HH__

#include <net/if.h>
#include <arpa/inet.h>

#include "TLV.hh"

enum MatchType {
    RFMT_IPV4 = 1,       /* Match IPv4 Destination */
    RFMT_IPV6 = 2,       /* Match IPv6 Destination */
    RFMT_ETHERNET = 3,   /* Match Ethernet Destination */
    RFMT_MPLS = 4,       /* Match MPLS label_in */
    /* Optional */
    RFMT_IN_PORT = 254,  /* Match incoming port (Unimplemented) */
    RFMT_VLAN = 255      /* Match incoming VLAN (Unimplemented) */
};

typedef struct ip_match {
    struct in_addr addr;
    struct in_addr mask;
} ip_match_t;

typedef struct ip6_match {
    struct in6_addr addr;
    struct in6_addr mask;
} ip6_match_t;

class Match : public TLV {
    public:
        Match(const Match& other);
        Match(MatchType, boost::shared_array<uint8_t> value);
        Match(MatchType, const uint8_t* value);
        Match(MatchType, const uint32_t value);
        Match(MatchType, const ip_match_t*);
        Match(MatchType, const ip6_match_t*);

        Match& operator=(const Match& other);
        bool operator==(const Match& other);
        const ip_match* getIPv4() const;
        const ip6_match* getIPv6() const;
        virtual std::string type_to_string() const;
        virtual bool optional();
        virtual mongo::BSONObj to_BSON() const;

        static Match* from_BSON(mongo::BSONObj);
    private:
        static size_t type_to_length(uint8_t);
        static byte_order type_to_byte_order(uint8_t);
};

namespace MatchList {
    mongo::BSONArray to_BSON(const std::vector<Match> list);
    std::vector<Match> to_vector(std::vector<mongo::BSONElement> array);
}

#endif /* __MATCH_HH__ */
