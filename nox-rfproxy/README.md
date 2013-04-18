nox-rfproxy
===========

A port of the RFProxy application to NOX-Classic. This repository is referenced
by a core RouteFlow repository, and it is recommended to install via that
repository (see "Building" below).

NOX Network Control Platform
----------------------------

Copyright (C) 2008-2009 Nicira Networks  
Copyright (C) 2009-2010 Stanford University  
Copyright (C) 2009-2010 International Computer Science Institute, UC Berkeley

Welcome to the NOX network control platform.   This distribution
includes all the software you need to build, install, and deploy NOX in
your network, as well as source code and tools to allow you to develop
your own NOX applications.

This version of NOX is a developers' release.  It is intended to provide
a programmatic platform for controlling one or more OpenFlow switches.
NOX can be extended both in C++ or Python and provides an abstracted
interface to OpenFlow.  This distribution contains a set of example
applications and some built in libraries which provide useful network
functions such as host tracking and routing.

Dependencies
------------

NOX-Classic does not support Ubuntu 12.04 or later due to dependency on SWIG1.3.

To install all dependencies on an Ubuntu 11.10 or earlier release:

```
$ sudo apt-get install linux-headers-generic autoconf automake libtool \
libboost-test-dev libssl-dev libpcap-dev python-twisted \
python-simplejson python-dev swig1.3
```

Building
--------

Nox-rfproxy requires RouteFlow to run. The usual way to install RouteFlow and
all of its dependencies is as follows:

1) Clone RouteFlow

```$ git clone git@github.com:joestringer/RouteFlow.git```

2) Fetch dependencies and install VMs (This will build nox from this repository)

```$ RouteFlow/build.sh -i nox```

Running
-------

RouteFlow usually supplies a script to run all of the components in the
correct order. If you want to run nox-rfproxy, load the app by using
nox-core:

```
$ cd RouteFlow/nox/build/src
$ ./nox_core -i ptcp:$CONTROLLER_PORT rfproxy -v &
```

RFProxy License
---------------

RFProxy uses the Apache License version 2.0. See LICENSE in
`src/nox/netapps/rfproxy/` for more details.
