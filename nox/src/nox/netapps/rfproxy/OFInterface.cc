#include <net/ethernet.h>

#include "OFInterface.hh"
#include "openflow/rfofmsg.h"

namespace rfproxy {

static vigil::Vlog_module lg("rfproxy");

/**
 * Convert the RouteFlow Match to an OpenFlow match
 *
 * Returns -1 on failure.
 */
int add_match(ofp_flow_mod *ofm, Match *match) {
    int error = 0;

    switch(match->getType()) {
        case RFMT_IPV4: {
            ip_match *im = (ip_match *)match->getValue();
            uint32_t mask = ofp_get_mask(im->mask, OFPFW_NW_DST_SHIFT);

            ofm_match_dl(ofm, OFPFW_DL_TYPE, ETHERTYPE_IP, 0, 0);
            ofm_match_nw(ofm, mask, 0, 0, 0, im->addr.s_addr);
            break;
        }
        case RFMT_ETHERNET:
            ofm_match_dl(ofm, OFPFW_DL_DST, 0, 0, (uint8_t *)match->getValue());
            break;
        case RFMT_IPV6:
        case RFMT_MPLS:
        default: {
            /* Not implemented in OpenFlow 1.0. */
            error = -1;
            break;
        }
    }

    return error;
}

/**
 * Convert the RouteFlow Action to an OpenFlow Action
 *
 * Returns -1 on failure.
 */
int add_action(ofp_action_header *oah, Action *action) {
    int error = 0;

    switch (action->getType()) {
        case RFAT_OUTPUT:
            ofm_set_action(oah, OFPAT_OUTPUT, *(uint16_t*)action->getValue(), 0);
            break;
        case RFAT_SET_ETH_SRC:
            ofm_set_action(oah, OFPAT_SET_DL_SRC, 0, (uint8_t*)action->getValue());
            break;
        case RFAT_SET_ETH_DST:
            ofm_set_action(oah, OFPAT_SET_DL_DST, 0, (uint8_t*)action->getValue());
            break;
        case RFAT_PUSH_MPLS:
        case RFAT_POP_MPLS:
        case RFAT_SWAP_MPLS:
            /* Not implemented in OpenFlow 1.0. */
            error = -1;
            break;
    }

    return error;
}

/**
 * Return the size of the OpenFlow struct to hold the given matches.
 */
size_t ofp_match_len(std::vector<Match*> matches) {
    /* In OpenFlow 1.0, it's a fixed structure */
    return sizeof(struct ofp_flow_mod);
}

/**
 * Return the size of the OpenFlow struct to hold this particular action.
 */
size_t ofp_len(Action *action) {
    size_t len = 0;

    switch (action->getType()) {
        case RFAT_OUTPUT:
            len = sizeof(struct ofp_action_output);
            break;
        case RFAT_SET_ETH_SRC:
        case RFAT_SET_ETH_DST:
            len = sizeof(struct ofp_action_dl_addr);
            break;
        case RFAT_PUSH_MPLS:
        case RFAT_POP_MPLS:
        case RFAT_SWAP_MPLS:
            /* Not implemented in OpenFlow 1.0. */
            break;
    }

    if (len > 0) {
        len += sizeof(struct ofp_action_header);
    }

    return len;
}

/**
 * Return the size of the OpenFlow struct to hold the given actions.
 */
size_t ofp_action_len(std::vector<Action*> actions) {
    std::vector<Action*>::iterator iter;
    size_t len = 0;

    for (iter = actions.begin(); iter != actions.end(); ++iter) {
        len += ofp_len(*iter);
    }

    return len;
}

/**
 * Creates an OpenFlow FlowMod based on the given matches and actions
 *
 * mod: Specify whether to add or remove a flow (RMT_*)
 * matches: Vector of Match structures
 * actions: Vector of Action structures
 *
 * Returns a shared_array pointing to NULL on failure (Unsupported feature)
 */
boost::shared_array<uint8_t> create_flow_mod(uint8_t mod,
            std::vector<Match*> matches, std::vector<Action*> actions) {
    int error = 0;
    ofp_flow_mod *ofm;
    size_t size = sizeof *ofm + ofp_match_len(matches) + ofp_action_len(actions);

    boost::shared_array<uint8_t> raw_of(new uint8_t[size]);
    ofm = (ofp_flow_mod*) raw_of.get();
    ofm_init(ofm, size);

    ofp_action_header *oah = ofm->actions;

    std::vector<Match*>::iterator iter_mat;
    for (iter_mat = matches.begin(); iter_mat != matches.end(); ++iter_mat) {
        if (add_match(ofm, *iter_mat) != 0) {
            Match *match = *iter_mat;
            VLOG_DBG(lg, "Could not serialise Match (type: %s)",
                     match->type_to_string(match->getType()).c_str());
            error = -1;
            break;
        }
    }

    std::vector<Action*>::iterator iter_act;
    for (iter_act = actions.begin(); iter_act != actions.end(); ++iter_act) {
        if (add_action(oah, *iter_act) != 0) {
            Action *action = *iter_act;
            VLOG_DBG(lg, "Could not serialise Action (type: %s)",
                     action->type_to_string(action->getType()).c_str());
            error = -1;
            break;
        }
        oah += ofp_len(*iter_act);
    }

    switch (mod) {
        case RMT_ADD:
            ofm_set_command(ofm, OFPFC_ADD);
            break;
        case RMT_DELETE:
            ofm_set_command(ofm, OFPFC_DELETE_STRICT);
            break;
        default:
            VLOG_DBG(lg, "Unrecognised RouteModType (type: %d)", mod);
            error = -1;
            break;
    }

    if (error != 0) {
        raw_of.reset(NULL);
    }

    return raw_of;
}

} // namespace rfproxy
