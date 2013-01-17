#ifndef __RFOFMSG_H__
#define __RFOFMSG_H__

#include <arpa/inet.h>

#include "openflow/openflow.h"

#define OFP_BUFFER_NONE (0xffffffff)

uint32_t ofp_get_mask(struct in_addr, int shift);
uint32_t ofp_get_mask(uint8_t, int shift);

void ofm_init(ofp_flow_mod* ofm, size_t size);
void ofm_match_in(ofp_flow_mod* ofm, uint16_t in);
void ofm_match_dl(ofp_flow_mod* ofm, uint32_t match, uint16_t type, const uint8_t src[], const uint8_t dst[]);
void ofm_match_vlan(ofp_flow_mod* ofm, uint32_t match, uint16_t id, uint8_t priority);
void ofm_match_nw(ofp_flow_mod* ofm, uint32_t match, uint8_t proto, uint8_t tos, uint32_t src, uint32_t dst);
void ofm_match_tp(ofp_flow_mod* ofm, uint32_t match, uint16_t src, uint16_t dst);
void ofm_set_action(ofp_action_header* hdr, uint16_t type, uint16_t port, const uint8_t addr[]);

void ofm_set_command(ofp_flow_mod* ofm, enum ofp_flow_mod_command cmd);
void ofm_set_command(ofp_flow_mod* ofm, uint16_t cmd, uint32_t id, uint16_t idle_to, uint16_t hard_to, uint16_t port);

#endif /* __RFOFMSG_H__ */
