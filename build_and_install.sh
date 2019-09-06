#!/bin/bash
# builds
#-------------------------------------------------------
# build shared client
echo "gcc -g -c -Wall -Werror -fpic shared_client.c"
gcc -g -c -Wall -Werror -fpic shared_client.c
echo "gcc -shared -o libshared_client.so shared_client.o"
gcc -shared -o libshared_client.so shared_client.o

# test app
echo "g++ -g -Wall test.cpp -o test -lpthread"
g++ -g -Wall test.cpp -o test -lpthread

# build stat server
echo "g++ -g -Wall stat_server.cpp -o stat_server"
g++ -g -Wall stat_server.cpp -o stat_server

# start stat_server
echo "./stat_server&"
./stat_server&

# use LD_PRELOAD on our shared library
echo "export LD_PRELOAD=libshared_client.so"
export LD_PRELOAD=$PWD/libshared_client.so

# run test
echo "./test"
./test


