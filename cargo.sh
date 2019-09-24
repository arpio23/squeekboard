#!/bin/bash

# This script manages Cargo operations
# while keeping the artifact directory within the build tree
# instead of the source tree

set -e

SCRIPT_PATH=`realpath $0`
SOURCE_DIR=`dirname $0`

export CARGO_TARGET_DIR=`pwd`
if [ ! -z ${1} ]; then
    OUT_PATH=`realpath "${1}"`
fi

cd $SOURCE_DIR
shift
cargo $BUILD_ARG $@

if [ ! -z ${OUT_PATH} ]; then
    cp "${CARGO_TARGET_DIR}"/debug/librs.a "${OUT_PATH}"
fi
