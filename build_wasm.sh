#!/bin/bash
set -e

# Use relative paths since this is run in WSL now
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Build Pation for WASM (no heavy dependencies anymore!)
cd "$DIR"
mkdir -p build_wasm_pation
cd build_wasm_pation
emcmake cmake -G "Unix Makefiles" ..
emmake make -j4

# Copy outputs
cp pation.js "$DIR/wrapper-wasm/"
cp pation.wasm "$DIR/wrapper-wasm/"


#wsl bash -c "cd /mnt/c/Users/ACER/Downloads/Pation && source emsdk/emsdk_env.sh && cd build_wasm_pation && make clean && cd .. && ./build_wasm.sh"
