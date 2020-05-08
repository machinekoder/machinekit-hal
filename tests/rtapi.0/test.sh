#!/bin/sh
rm -f rtapi_test
set -e
gcc -g -DULAPI \
    -I../../include \
    rtapi_test.c \
    ../../lib/libmtalk.so \
    ../../lib/libhalulapi.so \
    ../../lib/libhal.so \
    -o rtapi_test

realtime stop
set +e
./rtapi_test
if [ $? -ne 1 ]; then
    echo "rtapi_test: expected 1, got " $?
    exit 1;
fi
set -e
realtime start
./rtapi_test
if [ $? -ne 0 ]; then
    echo "rtapi_test: expected 0, got " $?
    exit 1;
fi

realtime stop
exit 0
