#include <net/if.h>
#include <arpa/inet.h>
#include <boost/scoped_array.hpp>

#include "Action.hh"

Action::Action(TLV *tlv) : TLV(tlv) { }

Action::Action(ActionType type, const uint8_t* value)
    : TLV(type, type_to_length(type), value) { }

Action::Action(ActionType type, const uint32_t value)
    : TLV(type, type_to_length(type), value) { }

std::string Action::type_to_string(uint8_t type) const {
    switch (type) {
        case RFAT_OUTPUT:           return "RFAT_OUTPUT";
        case RFAT_PUSH_MPLS:        return "RFAT_PUSH_MPLS";
        case RFAT_SWAP_MPLS:        return "RFAT_SWAP_MPLS";
        case RFAT_SET_ETH_SRC:      return "RFAT_SET_ETH_SRC";
        case RFAT_SET_ETH_DST:      return "RFAT_SET_ETH_DST";
        case RFAT_POP_MPLS:         return "RFAT_POP_MPLS";
        default:                    return "UNKNOWN_ACTION";
    }
}

size_t Action::type_to_length(uint8_t type) {
    switch (type) {
        case RFAT_OUTPUT:
        case RFAT_PUSH_MPLS:
        case RFAT_SWAP_MPLS:
            return sizeof(uint32_t);
        case RFAT_SET_ETH_SRC:
        case RFAT_SET_ETH_DST:
            return IFHWADDRLEN;
        case RFAT_POP_MPLS: /* len = 0 */
        default:
            return 0;
    }
}

/**
 * Serialises the Action to BSON in the following format:
 * {
 *   "type": (int),
 *   "value": (binary)
 * }
 */
mongo::BSONObj Action::to_BSON() const {
    mongo::BSONObjBuilder builder;

    boost::scoped_array<uint8_t> value(this->value);
    switch(type) {
        case RFAT_OUTPUT:
        case RFAT_PUSH_MPLS:
        case RFAT_SWAP_MPLS: {
            uint32_t label = htonl(*(this->value));
            value.reset((uint8_t *)&label);
            break;
        }
    }

    builder.append("type", this->type);
    builder.appendBinData("value", this->length, mongo::BinDataGeneral,
                          value.get());

    return builder.obj();
}


/**
 * Constructs a new TLV object based on the given BSONObj.
 *
 * It is the caller's responsibility to free the returned object. If the given
 * BSONObj is not a valid TLV, this method returns NULL.
 */
Action* Action::from_BSON(const mongo::BSONObj bson) {
    const mongo::BSONElement &btype = bson["type"];
    const mongo::BSONElement &bvalue = bson["value"];

    if (btype.type() != mongo::NumberInt)
        return NULL;

    if (bvalue.type() != mongo::BinData)
        return NULL;

    ActionType type = (ActionType)btype.Int();
    int len = bvalue.valuesize();
    const char *value = bvalue.binData(len);

    boost::scoped_array<uint8_t> local_value((uint8_t *)value);
    switch(type) {
        case RFAT_OUTPUT:
        case RFAT_PUSH_MPLS:
        case RFAT_SWAP_MPLS: {
            uint32_t label = ntohl((*value));
            local_value.reset((uint8_t *)&label);
            break;
        }
        default:
            break;
    }

    return new Action(type, local_value.get());
}

namespace ActionList {
    /**
     * Builds a BSON array based on the given vector of actions.
     */
    mongo::BSONArray to_BSON(const std::vector<Action*> list) {
        std::vector<Action*>::const_iterator iter;
        mongo::BSONArrayBuilder builder;

        for (iter = list.begin(); iter != list.end(); ++iter) {
            builder.append((*iter)->to_BSON());
        }

        return builder.arr();
    }

    /**
     * Returns a vector of Actions extracted from 'bson'. 'bson' should be an
     * array of bson-encoded Action objects formatted as follows:
     * {
     *   "type": (int),
     *   "value": (binary)
     * }
     *
     * If the given 'bson' is not an array, the returned vector will be empty.
     * If any actions in the array are invalid, they will not be added to the
     * vector.
     */
    std::vector<Action*> to_vector(std::vector<mongo::BSONElement> array) {
        std::vector<mongo::BSONElement>::iterator iter;
        std::vector<Action*> list;

        for (iter = array.begin(); iter != array.end(); ++iter) {
            boost::scoped_ptr<Action> action(Action::from_BSON(iter->Obj()));

            if (action.get() != NULL) {
                list.push_back(action.get());
            }
        }

        return list;
    }
}
