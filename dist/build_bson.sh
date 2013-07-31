#!/bin/sh

MONGO_URL="http://downloads.mongodb.org/cxx-driver"
TARBALL_BASE="mongodb-linux-x86_64-"
EXPANDED_BASE="mongo-cxx-driver-nightly"
API="api-mongo"
BSON_VESION="2.5.0"

prune_non_apache() {
    find $1 -type f | xargs grep -L 'Apache License' | xargs rm -rf 
}

##
# Fetch BSON C++ sources from MongoDB's C++ Driver
#
# $1 - Version string, defaults to "latest"
##
get_bson_cxx() {
    VER=${1-$BSON_VESION}
    ORIG_SRC="$TARBALL_BASE$VER.tgz"
    PATCH="${RFDIR}/dist/mongo-bson-$VER.patch"

    if [ \! -e "$ORIG_SRC" ] || [ $UPDATE -eq 1 ]; then
        if [ -n "$(which wget)" ]; then
            $DO wget "$MONGO_URL/$ORIG_SRC" || fail "couldn't wget $ORIG_SRC"
        elif [ -n "$(which curl)" ]; then
            $DO curl -O "$MONGO_URL/$ORIG_SRC" || fail "couldn't curl $ORIG_SRC"
        else
            fail "need wget or curl installed to fetch $ORIG_SRC"
        fi

        $DO rm -rf $EXPANDED_BASE $API
    fi

    if [ \! -e "$API" ]; then
        $DO tar xzf $ORIG_SRC
        $DO prune_non_apache $EXPANDED_BASE
        $DO mv $EXPANDED_BASE/src/mongo api-mongo
        $DO rm -rf $EXPANDED_BASE
        if [ -e "$PATCH" ]; then
            print_status "Patching BSON"
            $DO patch -p1 -i ${PATCH} || fail "Couldn't apply BSON patch"
        else
            fail "missing required BSON patching file $PATCH"
        fi
    fi
}

get_bson() {
    get_bson_cxx $1
}
