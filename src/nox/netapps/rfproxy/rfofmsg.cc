#include <string.h>
#include <net/ethernet.h>

#include "defs.h"
#include "rfofmsg.hh"

/**
 * Initialise the FlowMod with default values
 *
 * ofm: FlowMod to initialise
 * size: size of the FlowMod structure
 */
void ofm_init(ofp_flow_mod* ofm, size_t size) {
	memset(ofm, 0, size);

	/* Open Flow Header. */
	ofm->header.version = OFP_VERSION;
	ofm->header.type = OFPT_FLOW_MOD;
	ofm->header.length = htons(size);
	ofm->header.xid = 0;

    /* Match nothing by default. */
	ofm->match.wildcards = htonl(OFPFW_ALL);

	ofm->cookie = htonl(0);
	ofm->priority = htons(OFP_DEFAULT_PRIORITY);
	ofm->flags = htons(0);
}

/**
 * Match on physical input port
 *
 * ofm: FlowMod to add match to
 * in: Physical in_port to match
 */
void ofm_match_in(ofp_flow_mod* ofm, uint16_t in) {
	ofm->match.wildcards &= htonl(~OFPFW_IN_PORT);
	ofm->match.in_port = htons(in);
}

/**
 * Match on ethernet attributes
 *
 * Applies a single OFPFW_DL_* match to the FlowMod.
 *
 * ofm: FlowMod to add match to
 * match: Match type (OFPFW_DL_TYPE,OFPFW_DL_SRC,OFPFW_DL_DST)
 * type: Ethertype to match
 * src: 48-bit Ethernet source to match
 * dst: 48-bit Ethernet destination to match
 */
void ofm_match_dl(ofp_flow_mod* ofm, uint32_t match, uint16_t type,
                  const uint8_t src[], const uint8_t dst[]) {
    ofm->match.wildcards &= htonl(~match);

    if (match & OFPFW_DL_TYPE) { /* Ethernet frame type. */
        ofm->match.dl_type = htons(type);
    }
    if (match & OFPFW_DL_SRC) { /* Ethernet source address. */
        memcpy(ofm->match.dl_src, src, OFP_ETH_ALEN);
    }
    if (match & OFPFW_DL_DST) { /* Ethernet destination address. */
        memcpy(ofm->match.dl_dst, dst, OFP_ETH_ALEN);
    }
}

/**
 * Match on VLAN attributes
 *
 * Applies multiple OFPFW_DL_VLAN* matches to the FlowMod.
 *
 * ofm: FlowMod to add match to
 * match: Match type (OFPFW_DL_VLAN, OFPFW_DL_VLAN_PCP)
 * id: VLAN id to match
 * priority: VLAN PCP to match
 */
void ofm_match_vlan(ofp_flow_mod* ofm, uint32_t match, uint16_t id,
                    uint8_t priority) {
    ofm->match.wildcards &= htonl(~match);

    if (match & OFPFW_DL_VLAN) { /* VLAN id. */
        ofm->match.dl_vlan = htons(id);
    }
    if (match & OFPFW_DL_VLAN_PCP) { /* VLAN priority. */
        ofm->match.dl_vlan_pcp = priority;
    }
}

/**
 * Match on IP attributes
 *
 * Applies multiple OFPFW_NW_* matches to the FlowMod.
 *
 * ofm: FlowMod to add match to
 * match: Match type (OFPFW_NW_*)
 * proto: IP protocol to match
 * tos: DSCP bits to match
 * src: IPv4 source address in network byte-order
 * dst: IPv4 destination address in network byte-order
 */
void ofm_match_nw(ofp_flow_mod* ofm, uint32_t match, uint8_t proto,
                  uint8_t tos, uint32_t src, uint32_t dst) {
    ofm->match.wildcards &= htonl(~match);

    if (match & OFPFW_NW_PROTO) { /* IP protocol. */
        ofm->match.nw_proto = proto;
    }
    if (match & OFPFW_NW_TOS) { /* IP ToS (DSCP field, 6 bits). */
        ofm->match.nw_tos = tos;
    }
    if ((match & OFPFW_NW_SRC_MASK) > 0) { /* IP source address. */
        ofm->match.nw_src = src;
    }
    if ((match & OFPFW_NW_DST_MASK) > 0) { /* IP destination address. */
        ofm->match.nw_dst = dst;
    }
}

/**
 * Match on transport protocol attributes
 *
 * Applies multiple OFPFW_TP_* matches to the FlowMod.
 *
 * ofm: FlowMod to add match to
 * match: Match type (OFPFW_TP_*)
 * src: Transport source port in host byte-order
 * dst: Transport destination port in host byte-order
 */
void ofm_match_tp(ofp_flow_mod* ofm, uint32_t match, uint16_t src,
                  uint16_t dst) {
    ofm->match.wildcards &= htonl(~match);

    if (match & OFPFW_TP_SRC) { /* TCP/UDP source port. */
        ofm->match.tp_src = htons(src);
    }
    if (match & OFPFW_TP_DST) { /* TCP/UDP destination port. */
        ofm->match.tp_dst = htons(dst);
    }
}

/**
 * Initialises the given OpenFlow Action header with the given type and len
 */
void ofm_action_init(ofp_action_header* hdr, uint16_t type, uint16_t len) {
    memset((uint8_t *)hdr, 0, len);
    hdr->type = htons(type);
    hdr->len = htons(len);
}

/**
 * Set action values for header
 *
 * hdr: Action header to initialise
 * type: Action type (OFPAT_*)
 * len: length of action
 * max_len: Max packet size to send when output port is OFPP_CONTROLLER
 * addr: Value to use if writing DL_SRC, DL_DST
 */
void ofm_set_action(ofp_action_header* hdr, uint16_t type, uint16_t port,
                    const uint8_t addr[]) {
    if (type == OFPAT_OUTPUT) {
        ofp_action_output* action = (ofp_action_output*)hdr;
        ofm_action_init(hdr, type, sizeof(*action));

        action->port = htons(port);
        if (port == OFPP_CONTROLLER) {
            action->max_len = htons(RF_MAX_PACKET_SIZE);
        }
    } else if (type == OFPAT_SET_DL_SRC || type == OFPAT_SET_DL_DST) {
        ofp_action_dl_addr* action = (ofp_action_dl_addr*)hdr;
        ofm_action_init(hdr, type, sizeof(*action));

        memcpy(&action->dl_addr, addr, OFP_ETH_ALEN);
    }
}

/**
 * Set FlowMod attributes
 *
 * ofm: FlowMod to modify
 * cmd: Command - OFPFC_*
 * id: buffer id of packet to apply (if buffered)
 * idle_to: idle time before discarding
 * hard_to: maximum time before discarding
 * port: output port for OFPFC_DELETE* commands
 */
void ofm_set_command(ofp_flow_mod* ofm, enum ofp_flow_mod_command cmd,
                     uint32_t id, uint16_t idle_to, uint16_t hard_to,
                     uint16_t port) {
    ofm->command = htons(cmd);
    ofm->buffer_id = htonl(id);
    ofm->idle_timeout = htons(idle_to);
    ofm->hard_timeout = htons(hard_to);
    ofm->out_port = htons(port);
}

/**
 * Set FlowMod attributes
 *
 * ofm: FlowMod to modify
 * cmd: Command - OFPFC_*
 */
void ofm_set_command(ofp_flow_mod* ofm, enum ofp_flow_mod_command cmd) {
    ofm_set_command(ofm, cmd, OFP_BUFFER_NONE, OFP_FLOW_PERMANENT,
                    OFP_FLOW_PERMANENT, OFPP_NONE);
}

/**
 * Convert the given CIDR mask to an OpenFlow 1.0 match bitmask
 *
 * mask: CIDR mask, eg 24
 * shift: OpenFlow shift value (OFPFW_NW_*_SHIFT)
 */
uint32_t ofp_get_mask(uint8_t mask, int shift) {
    return ((uint32_t) 31 + mask) << shift;
}

/**
 * Convert the given full IPv4 mask to an OpenFlow 1.0 match bitmask
 *
 * ip_mask: Full IPv4 bitmask in network byte-order, eg 255.255.255.0
 * shift: OpenFlow shift value (OFPFW_NW_*_SHIFT)
 */
uint32_t ofp_get_mask(struct in_addr ip, int shift) {
    uint8_t cidr_mask = 0;
    uint32_t ip_mask = ntohl(ip.s_addr);

    /* Convert to CIDR mask */
    while (ip_mask & (1<<31)) {
        cidr_mask++;
        ip_mask <<= 1;
    }

    return ofp_get_mask(cidr_mask, shift);
}
