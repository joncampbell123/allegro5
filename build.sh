#!/bin/bash

# I hate cmake
cmake -DCMAKE_INSTALL_PREFIX:string=/usr -G 'Unix Makefiles' || exit 1

# okay, build
make -j5

