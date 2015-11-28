# ev-lcx
a lcx implementation based on libev.

# Compile
so far, the project compiled on mac&linux, in theory, the code works on windows.

1. install gcc/clang(the project needs c++0x, so at least, gcc should be 4.4.7)
2. install libev, download the tarbar: [libev](http://dist.schmorp.de/libev/), configure&make&make install
3. install cmake, if you are using mac, brew install cmake should be enough; if you are using linux,the official binary is here: [cmake](https://cmake.org/download/)
4. checkout the source code, cd in
5. make a build directory, such as: `mkdir build`, then cd in
6. generate cmake files: `cmake ..`
7. build: `make`

# usage
## listener
this is the public visible middle server, which means that it should own a public ip.

it must listen on two port, one is called *internal port*, one is called *external port*.

the *internal port* is communicate with slave, the *external port* is communicate with clients.

## slave
this is a middleware which must connect **listener** and **worker**.

## worker
in our scenario, the worker is a service behind a NAT, such as docker or NAT router. then we use this tool "open" the service port of the worker.

# final
finally, we got a port(*external port*) of listener is equal to a port of worker.

# why
everything is inside the code, :-)
