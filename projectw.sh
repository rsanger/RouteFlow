#!/bin/bash

# Set to 0 to use external switch(es)
STARTBVMS=1

# If rfserverconfig.csv and rfserverinternal.csv exist in /home/projectw,
# use them and don't try to configure rfvm1. If they do not exist, use values
# below for default config.
DPPORTNET=172.31
DPPORTNETV6=fc00::
DPPORTS=2
SWITCH1DPID=0x99
MULTITABLEDPS="''"
SATELLITEDPS="''"

HOME=/home/projectw
RF_HOME=$HOME/RouteFlow
RFSERVERCONFIG=/tmp/rfserverconfig.csv
RFSERVERINTERNAL=/tmp/rfserverinternal.csv
HOME_RFSERVERCONFIG="$HOME/"`basename $RFSERVERCONFIG`
HOME_RFSERVERINTERNAL="$HOME/"`basename $RFSERVERINTERNAL`
CONTROLLER_PORT=6653
LXCDIR=/var/lib/lxc
RFVM1=$LXCDIR/rfvm1
RFBR=br0
RFDP=dp0
RFDPID=7266767372667673
OFP=OpenFlow13
RFVM1IP=192.168.10.100
HOSTVMIP=192.168.10.1
OVSSOCK=/tmp/openvswitch-db.sock
VSCTL="ovs-vsctl --db=unix:$OVSSOCK"
OFCTL="ovs-ofctl -O$OFP"
export PATH=$PATH:/usr/local/bin:/usr/local/sbin
export PYTHONPATH=$PYTHONPATH:$RF_HOME

#modprobe 8021q
ulimit -c 1000000000

if [ "$EUID" != "0" ]; then
  echo "You must be root to run this script."
  exit 1
fi

ACTION=""
case "$1" in
--ryu)
    ACTION="RYU"
    ;;
--reset)
    ACTION="RESET"
    ;;
*)
    echo "Invalid argument: $1"
    echo "Options: "
    echo "    --ryu: run using RYU"
    echo "    --reset: stop running and clear data from previous executions"
    exit
    ;;
esac

cd $RF_HOME

wait_port_listen() {
    port=$1
    while ! `nc -z localhost $port` ; do
        echo -n .
        sleep 1
    done
}

echo_bold() {
    echo -e "\033[1m${1}\033[0m"
}

kill_process_tree() {
    top=$1
    pid=$2

    children=`ps -o pid --no-headers --ppid ${pid}`

    for child in $children
    do
        kill_process_tree 0 $child
    done

    if [ $top -eq 0 ]; then
        kill -9 $pid &> /dev/null
    fi
}

add_local_br() {
    br=$1
    dpid=$2
    $VSCTL add-br $br
    $VSCTL set bridge $br protocols=$OFP
    if [ "$dpid" != "" ] ; then 
      $VSCTL set bridge $br other-config:datapath-id=$dpid
    fi
    ifconfig $br up
    check_local_br_up $br
}

check_local_br_up() {
    br=$1
    echo waiting for OVS sw/controller $br to come up
    while ! $OFCTL ping $br 64|grep -q "64 bytes from" ; do
      echo -n "."
      sleep 1
    done 
}

start_ovs() {
	if [ ! -f /usr/local/etc/openvswitch/conf.db ] ; then
		ovsdb-tool create /usr/local/etc/openvswitch/conf.db /usr/local/share/openvswitch/vswitch.ovsschema
	fi
        ovsdb-server --pidfile --detach --remote=punix:$OVSSOCK
        ovs-vswitchd --pidfile --detach unix:$OVSSOCK
}

start_sample_vms() {
    echo_bold "-> Starting the sample network..."
    formatted_dpid=`printf "%16.16x" $SWITCH1DPID`
    echo_bold "-> Using DPID $formatted_dpid for switch1"
    lxc-start -n b1 -d
    lxc-start -n b2 -d
    add_local_br switch1 $formatted_dpid
    $VSCTL add-port switch1 b1.0
    $VSCTL add-port switch1 b2.0
    $VSCTL set-controller switch1 tcp:127.0.0.1:$CONTROLLER_PORT

    echo_bold "---"
    echo_bold "This test is up and running."
    echo_bold "Try pinging host b2 from host b1:"
    echo_bold "  $ sudo lxc-console -n b1"
    echo_bold "Login and run:"
    echo_bold "  $ ping $DPPORTNET.2.2"
    echo_bold "  $ ping $DPPORTNETV6:2:2"
}

default_config() {
    echo_bold "-> Default configuring rfvm1 virtual machine..."
    ROOTFS=$RFVM1/rootfs

    # Default rfserver config
    cp /dev/null $RFSERVERINTERNAL
    echo "vm_id,ct_id,dp_id,dp_port,eth_addr,rem_ct,rem_id,rem_port,rem_eth_addr" > $RFSERVERINTERNAL
    cp /dev/null $RFSERVERCONFIG
    echo "vm_id,vm_port,ct_id,dp_id,dp_port" > $RFSERVERCONFIG
    for i in `seq 1 $DPPORTS` ; do
      echo 0x12a0a0a0a0a0,$i,0,$SWITCH1DPID,$i >> $RFSERVERCONFIG
    done

    # Configure the VM
    cat > $RFVM1/config <<EOF
lxc.tty = 4
lxc.pts = 1024
lxc.rootfs = $ROOTFS 
lxc.mount  = $RFVM1/fstab

lxc.cgroup.devices.deny = a
# /dev/null and zero
lxc.cgroup.devices.allow = c 1:3 rwm
lxc.cgroup.devices.allow = c 1:5 rwm
# consoles
lxc.cgroup.devices.allow = c 5:1 rwm
lxc.cgroup.devices.allow = c 5:0 rwm
#lxc.cgroup.devices.allow = c 4:0 rwm
#lxc.cgroup.devices.allow = c 4:1 rwm
# /dev/{,u}random
lxc.cgroup.devices.allow = c 1:9 rwm
lxc.cgroup.devices.allow = c 1:8 rwm
lxc.cgroup.devices.allow = c 136:* rwm
lxc.cgroup.devices.allow = c 5:2 rwm
# rtc
lxc.cgroup.devices.allow = c 254:0 rwm

lxc.utsname = rfvm1

lxc.network.type = veth
lxc.network.flags = up
lxc.network.name = eth0
lxc.network.hwaddr = 12:a0:a0:a0:a0:a0
lxc.network.veth.pair = rfvm1.0

EOF

for i in `seq 1 $DPPORTS` ; do
	cat >> $RFVM1/config<<EOF
lxc.network.type = veth
lxc.network.flags = up
lxc.network.name = eth$i
lxc.network.hwaddr = 12:a1:a1:a1:a2:$i
lxc.network.veth.pair = rfvm1.$i

EOF
done

    cat > $ROOTFS/etc/network/interfaces <<EOF
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address $RFVM1IP
    netmask 255.255.255.0
EOF

for i in `seq 1 $DPPORTS` ; do
        cat >> $RFVM1/rootfs/etc/network/interfaces<<EOF

auto eth$i
iface eth$i inet static
   address $DPPORTNET.$i.1
   netmask 255.255.255.0
iface eth$i inet6 static
   address $DPPORTNETV6$i:1/112
EOF
done
}

start_rfvm1() {
    echo_bold "-> Starting the rfvm1 virtual machine..."
    ROOTFS=$RFVM1/rootfs

    cp /dev/null $ROOTFS/var/log/syslog

    cat > $ROOTFS/etc/rc.local <<EOF
/root/run_rfclient.sh &
exit 0
EOF

    cat > $ROOTFS/root/run_rfclient.sh <<EOF
#!/bin/sh
/opt/rfclient/rfclient > /var/log/rfclient.log 2> /var/log/rfclient.log.err
EOF
    chmod +x $RFVM1/rootfs/root/run_rfclient.sh

    # Create the rfclient dir
    RFCLIENTDIR=$ROOTFS/opt/rfclient
    mkdir -p $RFCLIENTDIR

    # Copy the rfclient executable
    cp rfclient/rfclient $RFCLIENTDIR/rfclient
    cp -p -P /usr/local/lib/libzmq* $ROOTFS/usr/local/lib
    chroot $ROOTFS ldconfig

    VMLOG=/tmp/rfvm1.log
    rm -f $VMLOG
    lxc-start -n rfvm1 -l DEBUG -o $VMLOG -d
}

reset() {
    echo_bold "-> Stopping and resetting LXC VMs...";
    lxc-stop -n rfvm1 &> /dev/null;
    lxc-stop -n b1 &> /dev/null;
    lxc-stop -n b2 &> /dev/null;

    init=$1;
    if [ $init -eq 1 ]; then
        echo_bold "-> Starting OVS daemons...";
	start_ovs
    else
        echo_bold "-> Stopping child processes...";
        kill_process_tree 1 $$
    fi

    sudo $VSCTL del-br $RFBR &> /dev/null;
    sudo $VSCTL del-br $RFDP &> /dev/null;
    sudo $VSCTL del-br switch1 &> /dev/null;
    sudo $VSCTL emer-reset &> /dev/null;

    rm -rf $RFVM1/rootfs/var/run/network/ifstate;
    rm -rf $LXCDIR/b1/rootfs/var/run/network/ifstate;
    rm -rf $LXCDIR/b2/rootfs/var/run/network/ifstate;

    echo_bold "-> Deleting data from previous runs...";
    rm -rf $HOME/db;
    rm -rf $RFVM1/rootfs/opt/rfclient;
    rm -rf $RFSERVERCONFIG
    rm -rf $RFSERVERINTERNAL
}
reset 1
trap "reset 0; exit 0" INT

if [ "$ACTION" != "RESET" ]; then
    if [ -f "$HOME_RFSERVERCONFIG" ] && [ -f "$HOME_RFSERVERINTERNAL" ] ; then
        echo_bold "-> Using existing external config..."
        cp $HOME_RFSERVERCONFIG $RFSERVERCONFIG
        cp $HOME_RFSERVERINTERNAL $RFSERVERINTERNAL
    else
        echo_bold "-> Using default config..."
        default_config
    fi

    echo_bold "-> Starting the management network ($RFBR)..."
    add_local_br $RFBR
    ifconfig $RFBR $HOSTVMIP

    echo_bold "-> Starting RFServer..."
    nice ./rfserver/rfserver.py $RFSERVERCONFIG -i $RFSERVERINTERNAL -m $MULTITABLEDPS -s $SATELLITEDPS &

    echo_bold "-> Starting the controller ($ACTION) and RFPRoxy..."
    case "$ACTION" in
    RYU)
        cd ryu-rfproxy
        ryu-manager --use-stderr --ofp-tcp-listen-port=$CONTROLLER_PORT ryu-rfproxy/rfproxy.py &
        ;;
    esac
    cd - &> /dev/null
    wait_port_listen $CONTROLLER_PORT
    check_local_br_up tcp:127.0.0.1:$CONTROLLER_PORT

    echo_bold "-> Starting the control plane network ($RFDP VS)..."
    $VSCTL add-br $RFDP
    $VSCTL set bridge $RFDP other-config:datapath-id=$RFDPID
    $VSCTL set bridge $RFDP protocols=$OFP
    $VSCTL set-controller $RFDP tcp:127.0.0.1:$CONTROLLER_PORT
    $OFCTL add-flow $RFDP actions=CONTROLLER:65509
    ifconfig $RFDP up
    check_local_br_up $RFDP

    echo_bold "-> Waiting for $RFDP to connect to controller..."
    while ! $VSCTL find Controller target=\"tcp:127.0.0.1:$CONTROLLER_PORT\" is_connected=true | grep -q connected ; do
      echo -n .
      sleep 1
    done

    echo_bold "-> Starting rfvm1..."
    start_rfvm1
    while ! ifconfig -s rfvm1.0 ; do
      echo -n .
      sleep 1
    done
    
    $VSCTL add-port $RFBR rfvm1.0
    for i in `netstat -i|grep rfvm1|cut -f 1 -d " "` ; do
      if [ "$i" != "rfvm1.0" ] ; then
        $VSCTL add-port $RFDP $i
      fi
    done

    echo_bold "-> Waiting for rfvm1 to come up..."
    while ! ping -W 1 -c 1 $RFVM1IP ; do
      echo -n .
      sleep 1
    done

    if [ $STARTBVMS -eq 1 ] ; then 
      start_sample_vms
    fi
    echo_bold "You can stop this test by pressing Ctrl+C."
    wait
fi
exit 0
