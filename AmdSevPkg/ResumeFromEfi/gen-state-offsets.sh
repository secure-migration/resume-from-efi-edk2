#!/bin/sh
set -e
cc GenStateOffsets.c -o tmp1
./tmp1 > AutoGeneratedStateOffsets.h
rm tmp1
