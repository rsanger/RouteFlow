#include "Match.hh"

Match::Match(TLV *tlv) : TLV(tlv) { }

Match::Match(MatchType type, const uint8_t *value)
    : TLV(type, type_to_length(type), value) { }

Match::Match(MatchType type, const uint32_t value)
    : TLV(type, type_to_length(type), value) { }

Match::Match(MatchType type, const ip_match_t *ip)
    : TLV(type, type_to_length(type), (const uint8_t *)ip) { }

Match::Match(MatchType type, const ip6_match_t *ip)
    : TLV(type, type_to_length(type), (const uint8_t *)ip) { }

std::string Match::type_to_string(uint8_t type) const {
    switch(type) {
        case RFMT_IPV4:         return "RFMT_IPV4";
        case RFMT_IPV6:         return "RFMT_IPV6";
        case RFMT_ETHERNET:     return "RFMT_ETHERNET";
        case RFMT_MPLS:         return "RFMT_MPLS";
        default:                return "UNKNOWN_MATCH";
    }
}

size_t Match::type_to_length(uint8_t type) {
    switch (type) {
        case RFMT_IPV4:         return sizeof(struct ip_match);
        case RFMT_IPV6:         return sizeof(struct ip6_match);
        case RFMT_ETHERNET:     return IFHWADDRLEN;
        case RFMT_MPLS:         return sizeof(uint32_t);
        default:                return 0;
    }
}

namespace MatchList {
    /**
     * Builds a BSON array based on the given vector of matches.
     */
    mongo::BSONArray to_BSON(const std::vector<Match*> list) {
        std::vector<Match*>::const_iterator iter;
        mongo::BSONArrayBuilder builder;

        for (iter = list.begin(); iter != list.end(); ++iter) {
            builder.append((*iter)->to_BSON());
        }

        return builder.arr();
    }

    /**
     * Returns a vector of Matches extracted from 'bson'. 'bson' should be an
     * array of bson-encoded Match objects formatted as follows:
     * {
     *   "type": (int),
     *   "value": (binary)
     * }
     *
     * If the given 'bson' is not an array, the returned vector will be empty.
     * If any matches in the array are invalid, they will not be added to the
     * vector.
     */
    std::vector<Match*> to_vector(const mongo::BSONElement bson) {
        std::vector<Match*> list;

        /* If we are given invalid input, return empty list. */
        if (bson.type() != mongo::Array) {
            return list;
        }

        std::vector<mongo::BSONElement> arr = bson.Array();
        std::vector<mongo::BSONElement>::iterator iter;

        for (iter = arr.begin(); iter != arr.end(); ++iter) {
            TLV *tlv = TLV::from_BSON(iter->Obj());

            if (tlv != NULL) {
                list.push_back(new Match(tlv));
                delete tlv;
            }
        }

        return list;
    }
}
