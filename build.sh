#!/bin/sh

set -x

SOURCE_DIR=`pwd`
BUILD_DIR=${BUILD_DIR:-../eva-bin}
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_NO_EXAMPLES=${BUILD_NO_EXAMPLES:-1}

mkdir -p $BUILD_DIR \
    && cd $BUILD_DIR \
    && cmake \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DCMAKE_BUILD_NO_EXAMPLES=$BUILD_NO_EXAMPLES \
        $SOURCE_DIR \
    && make $*