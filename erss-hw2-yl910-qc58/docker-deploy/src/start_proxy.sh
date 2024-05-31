#!/bin/bash
make clean
make
echo 'start running proxy server...'
./proxy_server &
while true; do :; done