AC_INIT([RFClient],  m4_esyscmd_s([echo "0.1 ($(git describe --long --tags --dirty --always))"]),
        [routeflow-discuss@googlegroups.com], [],
        [http://routeflow.github.io/RouteFlow/])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_SRCDIR(rfclient/RFClient.cc)

AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_CXXCPP
AC_PROG_CXX
AC_PROG_RANLIB
AC_C_BIGENDIAN
AC_TYPE_SIZE_T
AC_LANG([C++])
AC_HEADER_STDC

AM_INIT_AUTOMAKE

AC_CHECK_HEADERS([boost/scoped_array.hpp], [], [AC_MSG_ERROR([Cannot find Boost system headers.])])
AC_CHECK_HEADERS([boost/thread.hpp], [], [AC_MSG_ERROR([Cannot find Boost threading headers.])])

LIBS="-lboost_thread -lboost_system -lboost_filesystem -lboost_program_options"

PKG_CHECK_MODULES(LIBNL3, libnl-route-3.0 >= 3.1, [have_libnl3=yes], [have_libnl3=no])

if (test "${have_libnl3}" = "yes"); then
        CPPFLAGS+=" $LIBNL3_CFLAGS"
        LIBS+=" $LIBNL3_LIBS"
fi

AC_ARG_WITH([ipc],
    [AS_HELP_STRING([--with-ipc=prefix],
        [Set IPC backend, valid options are "mongo" or "zeromq".])],
    [case "${withval}" in
     mongo) ipc=mongo ;;
     zeromq)  ipc=zeromq ;;
     *) AC_MSG_ERROR([bad value ${withval} for --with-ipc]) ;;
     esac], [ipc=zeromq])
AM_CONDITIONAL([USE_ZEROMQ], [test "x$ipc" = "xzeromq"])
AM_CONDITIONAL([USE_MONGO], [test "x$ipc" = "xmongo"])

AC_ARG_WITH([bson],
    [AS_HELP_STRING([--with-bson=prefix],
        [Specify a non-standard path to the mongo C++ driver (bson) library])],
    [AC_CONFIG_LINKS([rflib/bson/mongo:$with_bson])],
    [with_bson=""])
AM_CONDITIONAL([LOCAL_BSON], [test -e "$with_bson"/bson/bson.h])
AC_CHECK_HEADERS([mongo/bson/bson.h], [],
                 [AM_COND_IF([LOCAL_BSON],
                             [AC_MSG_NOTICE([Building local BSON from $with_bson])],
                             [AC_MSG_ERROR([Cannot find MongoDB BSON headers.])])])

AM_COND_IF([USE_MONGO],
           [AC_CHECK_HEADERS([mongo/client/dbclient.h], [],
                             [AC_MSG_NOTICE([Cannot find MongoDB client headers.])])])
AM_COND_IF([USE_ZEROMQ],
           [AC_CHECK_HEADERS([zmq.h], [],
                             [AC_MSG_ERROR([Cannot find ZeroMQ headers.])])])
AM_COND_IF([LOCAL_BSON], [],
           [AC_MSG_NOTICE([Linking against libmongoclient. Code may be AGPL-tainted.])])

# distribute additional compiler and linker flags
# --> set these variables instead of CXXFLAGS or LDFLAGS
AC_SUBST([LIBS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

# files to generate via autotools (.am or .in source files)
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([rfclient/Makefile])
AC_CONFIG_FILES([rflib/Makefile])
AC_CONFIG_FILES([rflib/ipc/Makefile])
AC_CONFIG_FILES([rflib/types/Makefile])
AM_COND_IF([LOCAL_BSON], [AC_CONFIG_FILES([rflib/bson/Makefile])])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

# generate the final Makefile etc.
AC_OUTPUT
