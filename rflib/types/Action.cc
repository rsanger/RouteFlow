#include <net/if.h>
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
    std::vector<Action*> to_vector(const mongo::BSONElement bson) {
        std::vector<Action*> list;

        /* If we are given invalid input, return empty list. */
        if (bson.type() != mongo::Array) {
            return list;
        }

        std::vector<mongo::BSONElement> arr = bson.Array();
        std::vector<mongo::BSONElement>::iterator iter;

        for (iter = arr.begin(); iter != arr.end(); ++iter) {
            TLV *tlv = TLV::from_BSON(iter->Obj());

            if (tlv != NULL) {
                list.push_back(new Action(tlv));
                delete tlv;
            }
        }

        return list;
    }
}
