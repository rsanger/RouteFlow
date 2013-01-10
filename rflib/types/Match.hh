#ifndef __MATCH_HH__
#define __MATCH_HH__

#include <net/if.h>
#include <arpa/inet.h>

#include "TLV.hh"

enum MatchType {
    RFMT_IPV4,           /* Match IPv4 Destination */
    RFMT_IPV6,           /* Match IPv6 Destination */
    RFMT_ETHERNET,       /* Match Ethernet Destination */
    RFMT_MPLS            /* Match MPLS label_in */
    /* Future implementation */
    //RFMT_IN_PORT,        /* Match incoming port (Unimplemented) */
    //RFMT_VLAN            /* Match incoming VLAN (Unimplemented) */
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
        Match(TLV *other);
        Match(MatchType type, const uint8_t *value);
        Match(MatchType type, const uint32_t value);
        Match(MatchType type, const ip_match_t *ip);
        Match(MatchType type, const ip6_match_t *ip);

        virtual std::string type_to_string(uint8_t type) const;
        virtual mongo::BSONObj to_BSON() const;

        static Match* from_BSON(mongo::BSONObj bson);
    private:
        static size_t type_to_length(uint8_t type);
};

namespace MatchList {
    mongo::BSONArray to_BSON(const std::vector<Match> list);
    std::vector<Match> to_vector(std::vector<mongo::BSONElement> array);
}

#endif /* __MATCH_HH__ */
