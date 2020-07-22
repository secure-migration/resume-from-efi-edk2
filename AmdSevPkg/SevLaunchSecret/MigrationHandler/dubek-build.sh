#!/bin/bash
cd ~/edk2-internal
source edksetup.sh
build -n 20 -t GCC5 -a X64 -p AmdSevPkg/AmdSevPkg.dsc
