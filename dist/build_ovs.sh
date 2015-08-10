#!/bin/sh

OVS_URL="http://openvswitch.org/releases"
OVS_GIT="git://openvswitch.org/openvswitch"
OVS_BRANCH="origin/master"

OVS_COMMON="linux-headers-generic"
OVS_BUILD_DEPS="dh-autoreconf pkg-config"
OVS_BINARY="openvswitch-switch"
OVS_KERNEL="openvswitch-datapath-source module-assistant"

verlte() {
    local result=`echo "$1 < $2" | bc`
    [ "$result" = "1" ]
}

verlt() {
    [ "$1" = "$2" ] && return 1 || verlte $1 $2
}

install_ovs() {
    print_status "Installing Open vSwitch"

    $SUPER make install || return 1

    $SUPER mkdir -p /lib/modules/`uname -r`/kernel/net/ovs
    $SUPER make modules_install || return 1
    $SUPER depmod -a || return 1
    $SUPER modprobe openvswitch || return 1
    grep -q openvswitch /etc/modules

    status=$?
    if [ $status -eq 1 ]; then
        $SUPER echo "openvswitch" >>/etc/modules
        if [ $? -ne 0 ]; then
            print_status "Can't add openvswitch_mod to /etc/modules" $YELLOW
        fi
    fi

    $SUPER mkdir -p /usr/local/etc/openvswitch
    $SUPER rm -f /usr/local/etc/openvswitch/conf.db
    $SUPER ovsdb-tool create /usr/local/etc/openvswitch/conf.db \
        vswitchd/vswitch.ovsschema || return 1

    return 0
}

##
# Fetch and build Open vSwitch from source.
#
# $1 - Version string or "git"
##
build_ovs() {
    if [ -e "openvswitch-$1" ] && [ $UPDATE -eq 0 ]; then
        return;
    fi

    fetch "openvswitch-" $1 $OVS_GIT $OVS_BRANCH $OVS_URL ||
        fail "Couldn't fetch OVS"

    if [ $FETCH_ONLY -ne 1 ]; then
        print_status "Building Open vSwitch"
        if [ ! -e "./configure" ]; then
            ./boot.sh || fail "Couldn't bootstrap Open vSwitch"
        fi
        if [ ! -e "Makefile" ]; then
            $DO ./configure --with-linux=/lib/modules/`uname -r`/build \
                --enable-silent-rules --silent ||
                fail "Couldn't configure Open vSwitch"
        fi
        $DO make || fail "Couldn't build Open vSwitch"

        install_ovs || fail "Couldn't install Open vSwitch"
        $DO cd -
    fi
}

get_ovs() {
    pkg_install "$OVS_COMMON"
    if [ "$1" = "deb" ]; then
        version=`lsb_release -a 2>/dev/null | grep "Release" | cut -f2`
        if (verlt $version "12.04"); then
            pkg_install "$OVS_BINARY $OVS_KERNEL"
            $SUPER module-assistant prepare
            $SUPER module-assistant auto-install openvswitch-datapath
        else
            # with newer ubuntu we can just use in-tree OVS kernel module
            pkg_install "$OVS_BINARY"
        fi
    else
        pkg_install "$OVS_BUILD_DEPS"
        build_ovs $@
    fi
}
