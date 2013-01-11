#include <stdint.h>
#include <map>
#include <linux/if_ether.h>
#include <boost/bind.hpp>

#include "rfproxy.hh"
#include "assert.hh"
#include "component.hh"
#include "vlog.hh"
#include "packet-in.hh"
#include "datapath-join.hh"
#include "datapath-leave.hh"
#include "buffer.hh"
#include "flow.hh"
#include "netinet++/ethernet.hh"
#include "packets.h"

#include "ipc/MongoIPC.h"
#include "ipc/RFProtocol.h"
#include "ipc/RFProtocolFactory.h"
#include "OFInterface.hh"
#include "converter.h"
#include "defs.h"

#define FAILURE 0
#define SUCCESS 1

// TODO: proper support for ID
#define ID 0

namespace vigil {

static Vlog_module lg("rfproxy");


// Base functions
bool rfproxy::send_of_msg(uint64_t dp_id, uint8_t* msg) {
    if (send_openflow_command(datapathid::from_host(dp_id), (ofp_header*) msg, true))
        return FAILURE;
    else
        return SUCCESS;
}

bool rfproxy::send_packet_out(uint64_t dp_id, uint32_t port, Buffer& data) {

    if (send_openflow_packet(datapathid::from_host(dp_id), data, port,
                             OFPP_NONE, true))
        return FAILURE;
    else
        return SUCCESS;
}

// Event handlers
Disposition rfproxy::on_datapath_up(const Event& e) {
    const Datapath_join_event& dj = assert_cast<const Datapath_join_event&> (e);

    for (int i = 0; i < dj.ports.size(); i++) {
        if (dj.ports[i].port_no <= OFPP_MAX) {
            DatapathPortRegister msg(ID, dj.datapath_id.as_host(), dj.ports[i].port_no);
            ipc->send(RFSERVER_RFPROXY_CHANNEL, RFSERVER_ID, msg);

            VLOG_INFO(lg,
                      "Registering datapath port (dp_id=%0#"PRIx64", dp_port=%d)",
                      dj.datapath_id.as_host(),
                      dj.ports[i].port_no);
        }
    }

    return CONTINUE;
}

Disposition rfproxy::on_datapath_down(const Event& e) {
    const Datapath_leave_event& dl = assert_cast<const Datapath_leave_event&> (e);
    uint64_t dp_id = dl.datapath_id.as_host();

    VLOG_INFO(lg,
        "Datapath is down (dp_id=%0#"PRIx64")",
        dp_id);

    // Delete internal entry
    table.delete_dp(dp_id);

    // Notify RFServer
    DatapathDown dd(ID, dp_id);
    ipc->send(RFSERVER_RFPROXY_CHANNEL, RFSERVER_ID, dd);
    return CONTINUE;
}

Disposition rfproxy::on_packet_in(const Event& e) {
    const Packet_in_event& pi = assert_cast<const Packet_in_event&> (e);
    uint64_t dp_id = pi.datapath_id.as_host();
    uint32_t in_port = pi.in_port;
    uint64_t vs_id = pi.datapath_id.as_host();
    uint32_t vs_port = pi.in_port;

    Nonowning_buffer orig_buf(*pi.get_buffer());
    Buffer *buf = &orig_buf;
    Flow orig_flow(in_port, *buf);
    Flow *flow = &orig_flow;

    // Drop all LLDP packets
    if (flow->dl_type == ethernet::LLDP) {
        return CONTINUE;
    }

    // If we have a mapping packet, inform RFServer through a Map message
    if (flow->dl_type == htons(RF_ETH_PROTO)) {
        const eth_data* data = buf->try_pull<eth_data> ();
        VLOG_INFO(lg,
            "Received mapping packet (vm_id=%0#"PRIx64", vm_port=%d, vs_id=%0#"PRIx64", vs_port=%d)",
            data->vm_id,
            data->vm_port,
            vs_id,
            vs_port);

        // Create a map message and send it
        VirtualPlaneMap mapmsg;
        mapmsg.set_vm_id(data->vm_id);
        mapmsg.set_vm_port(data->vm_port);
        mapmsg.set_vs_id(vs_id);
        mapmsg.set_vs_port(vs_port);
        ipc->send(RFSERVER_RFPROXY_CHANNEL, RFSERVER_ID, mapmsg);

		return CONTINUE;
	}

    // If the packet came from RFVS, redirect it to the right switch port
    if (IS_RFVS(dp_id)) {
        PORT dp_port = table.vs_port_to_dp_port(dp_id, in_port);
        if (dp_port != NONE)
            send_packet_out(dp_port.first, dp_port.second, *buf);
        else
            VLOG_DBG(lg, "Unmapped RFVS port (vs_id=%0#"PRIx64", vs_port=%d)",
                     dp_id, in_port);
    }
    // If the packet came from a switch, redirect it to the right RFVS port
    else {
        PORT vs_port = table.dp_port_to_vs_port(dp_id, in_port);
        if (vs_port != NONE)
            send_packet_out(vs_port.first, vs_port.second, *buf);
        else
            VLOG_DBG(lg, "Unmapped datapath port (vs_id=%0#"PRIx64", vs_port=%d)",
                     dp_id, in_port);
    }

    return CONTINUE;
}

// IPC message processing
bool rfproxy::process(const string &from, const string &to,
                      const string &channel, IPCMessage& msg) {
    int type = msg.get_type();
    if (type == ROUTE_MOD) {
        RouteMod* rmmsg = static_cast<RouteMod*>(&msg);
        boost::shared_array<uint8_t> ofmsg = create_flow_mod(rmmsg->get_mod(),
                                    rmmsg->get_matches(),
                                    rmmsg->get_actions(),
                                    rmmsg->get_options());
        if (ofmsg.get() == NULL) {
            VLOG_DBG(lg, "Failed to create OpenFlow FlowMod");
        } else {
            send_of_msg(rmmsg->get_id(), ofmsg.get());
        }
    }
    else if (type == DATA_PLANE_MAP) {
        DataPlaneMap* dpmmsg = dynamic_cast<DataPlaneMap*>(&msg);
        table.update_dp_port(dpmmsg->get_dp_id(), dpmmsg->get_dp_port(),
                             dpmmsg->get_vs_id(), dpmmsg->get_vs_port());
    }
    return true;
}

// Initialization
void rfproxy::configure(const Configuration* c) {
    lg.dbg("Configure called");
}

void rfproxy::install() {
    ipc = new MongoIPCMessageService(MONGO_ADDRESS, MONGO_DB_NAME, to_string<uint64_t>(ID));
    factory = new RFProtocolFactory();
    ipc->listen(RFSERVER_RFPROXY_CHANNEL, factory, this, false);

    register_handler<Packet_in_event>
        (boost::bind(&rfproxy::on_packet_in, this, _1));
    register_handler<Datapath_join_event>
        (boost::bind(&rfproxy::on_datapath_up, this, _1));
    register_handler<Datapath_leave_event>
        (boost::bind(&rfproxy::on_datapath_down, this, _1));
}

void rfproxy::getInstance(const Context* c, rfproxy*& component) {
    component = dynamic_cast<rfproxy*>
        (c->get_by_interface(container::Interface_description(
            typeid(rfproxy).name())));
}

REGISTER_COMPONENT(Simple_component_factory<rfproxy>, rfproxy);
}
