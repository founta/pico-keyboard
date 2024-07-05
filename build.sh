#!/bin/bash

export PICO_SDK_PATH="~/win_home/Documents/battery_fw/pico-sdk"
mkdir -p build && cd build
cmake ../firmware && make -j 8

cd ..

rm -rf dist
mkdir dist && cp build/*.uf2 ./dist
