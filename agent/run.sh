#!/bin/bash
set -e

# Compile all source files
g++ -std=c++17 -I/opt/homebrew/include \
    main.cpp \
    utils.cpp \
    cdp_client.cpp \
    chromium_process.cpp \
    agent.cpp \
    rl_agent.cpp \
    -o main

echo "Build successful: ./main"