#ifndef __ACTION_HH__
#define __ACTION_HH__

#include "TLV.hh"

enum ActionType {
    RFAT_OUTPUT,         /* Output port */
    RFAT_SET_ETH_SRC,    /* Ethernet source address */
    RFAT_SET_ETH_DST,    /* Ethernet destination address */
    RFAT_PUSH_MPLS,      /* Push MPLS label */
    RFAT_POP_MPLS,       /* Pop MPLS label */
    RFAT_SWAP_MPLS       /* Swap MPLS label */
    /* Future implementation */
    //RFAT_DROP,           /* Drop packet (Unimplemented) */
    //RFAT_SFLOW           /* Generate SFlow messages (Unimplemented) */
};

class Action : public TLV {
    public:
        Action(TLV *other);
        Action(ActionType type, const uint8_t* value);
        Action(ActionType type, const uint32_t value);

        virtual std::string type_to_string(uint8_t type) const;
        virtual mongo::BSONObj to_BSON() const;

        static Action* from_BSON(mongo::BSONObj bson);
    private:
        static size_t type_to_length(uint8_t type);
};

namespace ActionList {
    mongo::BSONArray to_BSON(const std::vector<Action> list);
    std::vector<Action> to_vector(std::vector<mongo::BSONElement> array);
}

#endif /* __ACTION_HH__ */
