#! /bin/bash

CONFIGURATION=Debug
SOLUTION_DIR=../solutions/libmovie_xcode_macos_debug

pushd ..
mkdir -p $SOLUTION_DIR
pushd $SOLUTION_DIR
/Applications/CMake.app/Contents/bin/cmake -G "Xcode" "${PWD}/../.." -DCMAKE_CONFIGURATION_TYPES:STRING=$CONFIGURATION -DCMAKE_BUILD_TYPE:STRING=$CONFIGURATION -DLIBMOVIE_EXAMPLES_BUILD:BOOL=TRUE
popd
popd
