//
// Created by shaw on 10/15/15.
//

#include <ev.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>

#include <map>

using namespace std;

static void external_sock_cb(struct ev_loop* event_loop, ev_io* io, int events);
static void internal_sock_cb(struct ev_loop* event_loop, ev_io* io, int events);

static int listener(int external_port, int internal_port);

int main(int argc, char** argv)
{
    if(argc != 3) {
        printf("usage: program external_port internal_port\r\n");
        return (-1);
    }
    int external_port = -1, internal_port = -1;
    sscanf(argv[1], "%d", &external_port);
    sscanf(argv[2], "%d", &internal_port);
    printf("external_port: %d, internal_port: %d\r\n", external_port, internal_port);
    if(external_port == -1 || internal_port == -1) {
        return (-1);
    }
    printf("ok,let's go\r\n");
    listener(external_port, internal_port);

    return (0);
}

static int listener(int external_port, int internal_port) {
    // get a event loop
    struct ev_loop* event_loop = ev_default_loop(0);
    if(event_loop == NULL) {
        printf("get default loop with 0 failed\r\n");
        return (-1);
    }
    // allocate socks
    int external_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(external_sock == -1) {
        printf("socket external failed,errno: %d\r\n", errno);
        return (-1);
    }
    // set reuseable
    int enable = 1;
    if(setsockopt(external_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
        printf("setsockopt external_sock SO_REUSEADDR failed, errno: %d\r\n", errno);
        return (-1);
    }
    int internal_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(internal_sock == -1) {
        printf("socket external failed,errno: %d\r\n", errno);
        return (-1);
    }
    if(setsockopt(internal_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
        printf("setsockopt internal_sock SO_REUSEADDR failed, errno: %d\r\n", errno);
        return (-1);
    }
    // set non-block
    int flags = fcntl(external_sock, F_GETFL, 0);
    if(flags == -1) {
        printf("fcntl external_sock F_GETFL failed, errno: %d\r\n", errno);
        return (-1);
    }
    if(fcntl(external_sock, F_SETFL, (flags | O_NONBLOCK)) != 0) {
        printf("fcntl external_sock F_SETFL failed, errno: %d\r\n", errno);
        return (-1);
    }
    flags = fcntl(internal_sock, F_GETFL, 0);
    if(flags == -1) {
        printf("fcntl internal_sock F_GETFL failed, errno: %d\r\n", errno);
        return (-1);
    }
    if(fcntl(internal_sock, F_SETFL, (flags | O_NONBLOCK)) != 0) {
        printf("fcntl internal_sock F_SETFL failed, errno: %d\r\n", errno);
        return (-1);
    }

    // bind socks
    struct sockaddr_in sockaddr_in1 = {0};
    sockaddr_in1.sin_len = sizeof(sockaddr_in1);
    sockaddr_in1.sin_family = AF_INET;
    sockaddr_in1.sin_port = htons(external_port);
    if(inet_pton(AF_INET, "0.0.0.0", &sockaddr_in1.sin_addr) != 1) {
        printf("inet_pton 0.0.0.0 failed, errno: %d\r\n", errno);
        return (-1);
    }
    if(bind(external_sock, (const sockaddr*)&sockaddr_in1, sizeof(sockaddr_in1)) != 0) {
        printf("bind external_sock failed, errno: %d\r\n", errno);
        return (-1);
    }
    sockaddr_in1.sin_port = htons(internal_port);
    if(bind(internal_sock, (const sockaddr*)&sockaddr_in1, sizeof(sockaddr_in1)) != 0) {
        printf("bind internal_sock failed, errno: %d\r\n", errno);
        return (-1);
    }
    // listen
    listen(external_sock, 1024);
    listen(internal_sock, 1024);

    // register to libev
    ev_io* external_sock_io = (ev_io*)malloc(sizeof(ev_io));
    ev_io_init(external_sock_io, external_sock_cb, external_sock, EV_READ);
    ev_io* internal_sock_io = (ev_io*)malloc(sizeof(ev_io));
    ev_io_init(internal_sock_io, internal_sock_cb, internal_sock, EV_READ);
    ev_io_start(event_loop, external_sock_io);
    ev_io_start(event_loop, internal_sock_io);

    printf("rolling...\r\n");
    ev_run(event_loop);

    return (0);
}

typedef struct SPeerCtx {
    int fd;
    struct sockaddr addr;
    socklen_t addr_len;
    ev_io io;
    char rbuf[2*40960];
    size_t rbuf_size; // const
    size_t rbuf_len;
    size_t rbuf_pos;
    char wbuf[2*40960];
    size_t wbuf_size; // const
    size_t wbuf_len;
    size_t wbuf_pos;
} SPeerCtx;

#define PKG_HEADER_SIZE 10 // 4-payload length,4-external peer's id,2-cmd

static SPeerCtx g_internal_peer_ctx = {0};
static map<int32_t, SPeerCtx> g_external_peer_ctxes; // fd: ctx

static void external_peer_cb(struct ev_loop* event_loop, ev_io* io, int events) {
    map<int32_t, SPeerCtx>::iterator peer_ctx_ite = g_external_peer_ctxes.find(io->fd);
    if(peer_ctx_ite == g_external_peer_ctxes.end()) { // everything is impossible
        return ;
    }
    SPeerCtx& peer_ctx = peer_ctx_ite->second;
    if(events & EV_WRITE) {
        if(peer_ctx.wbuf_len > 0) {
            ssize_t bytes_written = write(io->fd, peer_ctx.wbuf+peer_ctx.wbuf_pos, peer_ctx.wbuf_len);
            if(bytes_written == -1) { // errno
                if(errno == EWOULDBLOCK) { // blocked
                    return ;
                }
            } else {
                peer_ctx.wbuf_pos += bytes_written;
                peer_ctx.wbuf_len -= bytes_written;
            }
            // wbuf is used half, align
            if(peer_ctx.wbuf_pos > peer_ctx.wbuf_size/2) {
                memmove(peer_ctx.wbuf, peer_ctx.wbuf+peer_ctx.wbuf_pos, peer_ctx.wbuf_len);
                peer_ctx.wbuf_pos = 0;
            }
        }
    }
    if(events & EV_READ) {
        size_t bytes2read = peer_ctx.rbuf_size - (peer_ctx.rbuf_pos+peer_ctx.rbuf_len);
        ssize_t bytes_read = read(io->fd, peer_ctx.rbuf+peer_ctx.rbuf_pos+peer_ctx.rbuf_len, bytes2read);
        printf("external peer buf: %s\r\n", peer_ctx.rbuf);
        if (bytes_read == -1) { // errno
            if (errno == EAGAIN) { // non-block but data is not ready
                return ;
            }
        } else if (bytes_read == 0) { // eof of a socket???
            printf("external peer is eof?!!!\r\n");
            return ;
        } else {
            peer_ctx.rbuf_len += bytes_read;
        }
        // flush into internal peer's wbuf
        size_t internal_peer_wbuf_space = g_internal_peer_ctx.wbuf_size-(g_internal_peer_ctx.wbuf_pos+g_internal_peer_ctx.wbuf_len);
        // if internal peer's wbuf space is too small, can't flush now
        if(internal_peer_wbuf_space > PKG_HEADER_SIZE) {
            char peer_pkg_header[PKG_HEADER_SIZE] = {0}; // big-endian
            int32_t peer_pkg_payload_len = internal_peer_wbuf_space - PKG_HEADER_SIZE;
            {
                int8_t* pkg_payload_len_bytes = (int8_t*)&peer_pkg_payload_len; // assumed little-endian now
                peer_pkg_header[0] = pkg_payload_len_bytes[3]; peer_pkg_header[1] = pkg_payload_len_bytes[2];
                peer_pkg_header[2] = pkg_payload_len_bytes[1]; peer_pkg_header[3] = pkg_payload_len_bytes[0];
                int8_t* peer_id_bytes = (int8_t*)&peer_ctx.fd; // assumed little-endian
                peer_pkg_header[4] = peer_id_bytes[3]; peer_pkg_header[5] = peer_id_bytes[2];
                peer_pkg_header[6] = peer_id_bytes[1]; peer_pkg_header[7] = peer_id_bytes[0];
            }
            peer_pkg_header[8] = 'D'; peer_pkg_header[9] = 'P'; // Data Payload
            memcpy(g_internal_peer_ctx.wbuf+g_internal_peer_ctx.wbuf_pos, peer_pkg_header, PKG_HEADER_SIZE);
            memcpy(g_internal_peer_ctx.wbuf+g_internal_peer_ctx.wbuf_pos+PKG_HEADER_SIZE,
                   peer_ctx.rbuf+peer_ctx.rbuf_pos+PKG_HEADER_SIZE, internal_peer_wbuf_space - PKG_HEADER_SIZE);
            peer_ctx.rbuf_pos += (internal_peer_wbuf_space - PKG_HEADER_SIZE);
            peer_ctx.rbuf_len -= (internal_peer_wbuf_space - PKG_HEADER_SIZE);
        }
        // peer's rbuf used half, align
        if(peer_ctx.rbuf_pos > peer_ctx.rbuf_size/2) {
            memmove(peer_ctx.rbuf, peer_ctx.rbuf+peer_ctx.rbuf_pos, peer_ctx.rbuf_len);
            peer_ctx.rbuf_pos = 0;
        }
    }
}
static void external_sock_cb(struct ev_loop* event_loop, ev_io* io, int events) {
    printf("external sock events: %d\r\n", events);
    if(g_internal_peer_ctx.fd <= 0) {
        printf("internal peer is absent, please wait...\r\n");
        return ;
    }
    // peer come
    struct sockaddr_in peer_addr_in = {0};
    socklen_t peer_addr_in_len = 0;
    int peer_fd = accept(io->fd, (struct sockaddr*)&peer_addr_in, &peer_addr_in_len);
    if(peer_fd < 0) { // errno
        if(errno == EWOULDBLOCK) {
            return ;
        }
    }

    SPeerCtx& peer_ctx = g_external_peer_ctxes[peer_fd];
    peer_ctx.fd = peer_fd;
    peer_ctx.addr = (*(struct sockaddr*)(&peer_addr_in)); peer_ctx.addr_len = peer_addr_in_len;
    peer_ctx.rbuf_size = sizeof(peer_ctx.rbuf);
    peer_ctx.rbuf_pos = peer_ctx.rbuf_len = 0;
    peer_ctx.wbuf_size = sizeof(peer_ctx.wbuf);
    peer_ctx.wbuf_pos = peer_ctx.wbuf_len = 0;
    // register to libev
    ev_io_init(&peer_ctx.io, external_peer_cb, peer_fd, EV_READ|EV_WRITE);
    ev_io_start(event_loop, &peer_ctx.io);

    char peer_cidr[1024] = {0};
    if(inet_ntop(AF_INET, &peer_addr_in.sin_addr, peer_cidr, peer_addr_in_len) == NULL) {
        printf("convert external_sock's peer cidr failed, errno: %d\r\n", errno);
    }
    printf("got a external peer - fd(%d), addr(%s:%d)\r\n", peer_fd, peer_cidr, ntohs(peer_addr_in.sin_port));
}

// TODO: if a external peer is slow to send, then the internal peer read will be blocked

static void consume_internal_peer_pkg(struct ev_loop* event_loop) {
    // enough data for a pkg
    if(g_internal_peer_ctx.rbuf_len < PKG_HEADER_SIZE) return ; // nothing to do
    int8_t* pkg_buf = (int8_t*)g_internal_peer_ctx.rbuf+g_internal_peer_ctx.rbuf_pos;
    int32_t pkg_payload_len = -1;
    int32_t pkg_external_peer_id = -1;
    { // parse pkg payload len and external peer id
        // TODO: handle endian
        int8_t* pkg_payload_len_bytes = (int8_t*)&pkg_payload_len;
        int8_t* pkg_external_peer_id_bytes = (int8_t*)&pkg_external_peer_id;
        pkg_payload_len_bytes[0] = pkg_buf[3]; pkg_payload_len_bytes[1] = pkg_buf[2];
        pkg_payload_len_bytes[2] = pkg_buf[1]; pkg_payload_len_bytes[3] = pkg_buf[0];
        pkg_external_peer_id_bytes[0] = pkg_buf[7]; pkg_external_peer_id_bytes[1] = pkg_buf[6];
        pkg_external_peer_id_bytes[2] = pkg_buf[5]; pkg_external_peer_id_bytes[3] = pkg_buf[4];
    }
    if(g_internal_peer_ctx.rbuf_len < PKG_HEADER_SIZE+pkg_payload_len) return ; // data is still not enough
    // determine cmd
    int bytes_consumed = PKG_HEADER_SIZE+pkg_payload_len; // how many bytes consumed this time
    map<int32_t, SPeerCtx>::iterator pkg_external_peer_ctx_ite = g_external_peer_ctxes.find(pkg_external_peer_id);
    if(pkg_external_peer_ctx_ite != g_external_peer_ctxes.end()) { // dest peer is gone?!
        SPeerCtx& pkg_external_peer_ctx = pkg_external_peer_ctx_ite->second;
        if(memcmp(g_internal_peer_ctx.rbuf+8, "DP", 2) == 0) { // Data Payload
            // flush payload to external peer's wbuf
            size_t pkg_external_peer_wbuf_space = pkg_external_peer_ctx.wbuf_size-(pkg_external_peer_ctx.wbuf_pos+pkg_external_peer_ctx.wbuf_len);
            // external peer's wbuf space is too small, can't flush now
            if(pkg_external_peer_wbuf_space < pkg_payload_len) {
                bytes_consumed = 0;
                return ;
            } else {
                memcpy(pkg_external_peer_ctx.wbuf+pkg_external_peer_ctx.wbuf_pos+pkg_external_peer_ctx.wbuf_len,
                       g_internal_peer_ctx.rbuf+g_internal_peer_ctx.rbuf_pos+PKG_HEADER_SIZE, pkg_payload_len);
                pkg_external_peer_ctx.wbuf_len += pkg_payload_len;
            }
        } else if(memcmp(g_internal_peer_ctx.rbuf+8, "LC", 2) == 0) { // Lost Connection
            ev_io_stop(event_loop, &pkg_external_peer_ctx.io);
            g_external_peer_ctxes.erase(pkg_external_peer_ctx_ite);
        }
    }
    g_internal_peer_ctx.rbuf_pos += bytes_consumed;
    g_internal_peer_ctx.rbuf_len -= bytes_consumed;
    // rbuf is used half, align
    if(g_internal_peer_ctx.rbuf_pos > g_internal_peer_ctx.rbuf_size/2) {
         memmove(g_internal_peer_ctx.rbuf, g_internal_peer_ctx.rbuf+g_internal_peer_ctx.rbuf_pos, g_internal_peer_ctx.rbuf_len);
        g_internal_peer_ctx.rbuf_pos = 0;
    }
    consume_internal_peer_pkg(event_loop);
}
static void internal_peer_cb(struct ev_loop* event_loop, ev_io* io, int events) {
    if(events & EV_WRITE) {
        if(g_internal_peer_ctx.wbuf_len > 0) {
            ssize_t bytes_written = write(io->fd, g_internal_peer_ctx.wbuf+g_internal_peer_ctx.wbuf_pos, g_internal_peer_ctx.wbuf_len);
            if(bytes_written == -1) { // errno
                if(errno == EWOULDBLOCK) { // blocked
                    return ;
                }
            } else {
                g_internal_peer_ctx.wbuf_pos += bytes_written;
                g_internal_peer_ctx.wbuf_len -= bytes_written;
            }
            // wbuf is used half, align
            if(g_internal_peer_ctx.wbuf_pos > g_internal_peer_ctx.wbuf_size/2) {
                memmove(g_internal_peer_ctx.wbuf, g_internal_peer_ctx.wbuf+g_internal_peer_ctx.wbuf_pos, g_internal_peer_ctx.wbuf_len);
                g_internal_peer_ctx.wbuf_pos = 0;
            }
        }
    }
    if(events & EV_READ) {
        printf("internal peer events: %d\r\n", events);

        size_t bytes2read = g_internal_peer_ctx.rbuf_size - (g_internal_peer_ctx.rbuf_pos+g_internal_peer_ctx.rbuf_len);
        ssize_t bytes_read = read(io->fd, g_internal_peer_ctx.rbuf+g_internal_peer_ctx.rbuf_pos+g_internal_peer_ctx.rbuf_len, bytes2read);
        printf("internal peer buf: %s\r\n", g_internal_peer_ctx.rbuf);
        if (bytes_read == -1) { // errno
            if (errno == EAGAIN) { // non-block but data is not ready
                return ;
            }
        } else if (bytes_read == 0) { // eof of a socket???
            printf("internal peer is eof?!!!\r\n");
            return;
        } else {
            g_internal_peer_ctx.rbuf_len += bytes_read;
        }
    }
    consume_internal_peer_pkg(event_loop);
}
static void internal_sock_cb(struct ev_loop* event_loop, ev_io* io, int events) {
    printf("internal sock events: %d\r\n", events);
    if(g_internal_peer_ctx.fd != 0) {
        struct sockaddr_in* internal_peer_addr_in = (struct sockaddr_in*)&g_internal_peer_ctx.addr;
        char internal_peer_cidr[256] = {0};
        inet_ntop(AF_INET, &internal_peer_addr_in->sin_addr, internal_peer_cidr, g_internal_peer_ctx.addr_len);
        printf("internal sock has been filled - fd(%d), addr(%s:%d)\r\n",
               g_internal_peer_ctx.fd, internal_peer_cidr, ntohs(internal_peer_addr_in->sin_port));
        return ;
    }

    // peer come
    struct sockaddr_in peer_addr_in = {0};
    socklen_t peer_addr_in_len = 0;
    int peer_fd = accept(io->fd, (struct sockaddr*)&peer_addr_in, &peer_addr_in_len);
    if(peer_fd < 0) { // errno
        if(errno == EWOULDBLOCK) {
            return ;
        }
    }
    // TODO: i think we need some auth
    g_internal_peer_ctx.fd = peer_fd;
    g_internal_peer_ctx.addr = (*(struct sockaddr*)&peer_addr_in); g_internal_peer_ctx.addr_len = peer_addr_in_len;
    g_internal_peer_ctx.rbuf_size = sizeof(g_internal_peer_ctx.rbuf);
    g_internal_peer_ctx.rbuf_pos = g_internal_peer_ctx.rbuf_len = 0;
    g_internal_peer_ctx.wbuf_size = sizeof(g_internal_peer_ctx.wbuf);
    g_internal_peer_ctx.wbuf_pos = g_internal_peer_ctx.wbuf_len = 0;
    // register peer to libev
    ev_io_init(&g_internal_peer_ctx.io, internal_peer_cb, peer_fd, EV_READ|EV_WRITE);
    ev_io_start(event_loop, &g_internal_peer_ctx.io);

    char peer_cidr[1024] = {0};
    if(inet_ntop(AF_INET, &peer_addr_in.sin_addr, peer_cidr, peer_addr_in_len) == NULL) {
        printf("convert internal_sock's peer cidr failed, errno: %d\r\n", errno);
    }
    printf("got a internal peer - fd(%d), addr(%s:%d)\r\n", peer_fd, peer_cidr, ntohs(peer_addr_in.sin_port));
}