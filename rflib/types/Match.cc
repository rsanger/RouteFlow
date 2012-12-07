#include <boost/scoped_array.hpp>
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

/**
 * Serialises the Match to BSON in the following format:
 * {
 *   "type": (int),
 *   "value": (binary)
 * }
 */
mongo::BSONObj Match::to_BSON() const {
    uint8_t arr[this->length];
    const uint8_t* value = getValue();
    mongo::BSONObjBuilder builder;

    switch (this->type) {
        case RFMT_MPLS: {
            uint32_t label = htonl(*(uint32_t*)value);
            memcpy(arr, &label, this->length);
            value = arr;
            break;
        }
    }

    builder.append("type", this->type);
    builder.appendBinData("value", this->length, mongo::BinDataGeneral, value);
    return builder.obj();
}

/**
 * Constructs a new TLV object based on the given BSONObj.
 *
 * It is the caller's responsibility to free the returned object. If the given
 * BSONObj is not a valid TLV, this method returns NULL.
 */
Match* Match::from_BSON(const mongo::BSONObj bson) {
    const uint8_t *value = NULL;
    const mongo::BSONElement &btype = bson["type"];
    const mongo::BSONElement &bvalue = bson["value"];

    if (btype.type() != mongo::NumberInt)
        return NULL;

    if (bvalue.type() != mongo::BinData)
        return NULL;

    MatchType type = (MatchType)btype.Int();
    int len = bvalue.valuesize();
    boost::scoped_array<uint8_t> arr;

    switch (type) {
        case RFMT_MPLS: {
            uint32_t label = ntohl(*(uint32_t*)value);
            arr.reset(new uint8_t[type_to_length(type)]);
            memcpy(arr.get(), &label, type_to_length(type));
            value = arr.get();
            break;
        }
        default:
            value = (uint8_t*)bvalue.binData(len);
            break;
    }

    return new Match(type, value);
}

namespace MatchList {
    /**
     * Builds a BSON array based on the given vector of matches.
     */
    mongo::BSONArray to_BSON(const std::vector<Match> list) {
        std::vector<Match>::const_iterator iter;
        mongo::BSONArrayBuilder builder;

        for (iter = list.begin(); iter != list.end(); ++iter) {
            builder.append(iter->to_BSON());
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
    std::vector<Match> to_vector(std::vector<mongo::BSONElement> array) {
        std::vector<mongo::BSONElement>::iterator iter;
        std::vector<Match> list;

        for (iter = array.begin(); iter != array.end(); ++iter) {
            Match *match = Match::from_BSON(iter->Obj());

            if (match != NULL) {
                list.push_back(*match);
            }
        }

        return list;
    }
}
