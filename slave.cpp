//
// Created by shaw on 10/17/15.
//
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>

#include <arpa/inet.h>


#include <netdb.h>

#include <errno.h>

#include <unistd.h>

#include "common.h"

#include <map>

using namespace std;

/**
 * 连接internal peer,开始事件循环
 */
static int slave(struct sockaddr* internal_addrs[], struct sockaddr* worker_addrs[]);

int main(int argc, char* argv[])
{
    if(argc != 3) {
        printf("usage: program internal_host:internal_port worker_host:worker_port\r\n**host can by ip or domain**\r\n");
        return (-1);
    }
    const char* internal_addr = argv[1];
    const char* worker_addr = argv[2];
    const char* internal_addr_splitter = strstr(internal_addr, ":");
    const char* worker_addr_splitter = strstr(worker_addr, ":");
    if(internal_addr_splitter == NULL || worker_addr_splitter == NULL) {
        printf("usage: program internal_host:internal_port worker_host:worker_port\r\n**host can by ip or domain**\r\n");
        return (-1);
    }
    // this must be enough
    struct sockaddr* internal_addrs[1024] = {0};
    int internal_addr_cursor = 0;
    struct sockaddr* worker_addrs[1024] = {0};
    int worker_addr_cursor = 0;

    // TODO: support IPV6 and UDP
    printf("supported family: (AF_INET)%d & socktype: (SOCK_STREAM)%d\r\n", AF_INET, SOCK_STREAM);
    { // process hosts
        char internal_node[NI_MAXHOST] = {0}, worker_node[NI_MAXHOST] = {0};
        memcpy(internal_node, internal_addr, internal_addr_splitter-internal_addr);
        memcpy(worker_node, worker_addr, worker_addr_splitter-worker_addr);
        uint16_t internal_port = 0, worker_port = 0;
        struct addrinfo* addrinfos = NULL;
        int tryed = 0;

        sscanf(internal_addr_splitter+1, "%hu", &internal_port);
        sscanf(worker_addr_splitter+1, "%hu", &worker_port);
        int gai_errno = getaddrinfo(internal_node, NULL, NULL, &addrinfos);
        if(gai_errno != 0) {
            printf("getaddrinfo of internal node(%s) failed, error: %s\r\n", internal_node, gai_strerror(gai_errno));
            if(errno != EAI_AGAIN || tryed >= 3) {
                return (-1);
            } else {
                printf("we will try later within 1 second\r\n");
                sleep(1);
                tryed += 1;
            }
        }
        // pick up internal addr info
        for(struct addrinfo* addrinfo1 = addrinfos; addrinfo1 != NULL; addrinfo1 = addrinfo1->ai_next) {
            if(addrinfo1->ai_family == AF_INET && addrinfo1->ai_socktype == SOCK_STREAM) {
                struct sockaddr* sockaddr1 = (struct sockaddr*)calloc(1, sizeof(struct sockaddr));
                memcpy(sockaddr1, addrinfo1->ai_addr, sizeof(struct sockaddr));
                internal_addrs[internal_addr_cursor ++] = sockaddr1;

                struct sockaddr_in* sockaddr_in1 = (struct sockaddr_in*)sockaddr1;
                sockaddr_in1->sin_port = htons(internal_port);

                char addr_cidr[1024] = {0};
                inet_ntop(AF_INET, &sockaddr_in1->sin_addr, addr_cidr, sizeof(addr_cidr));
                printf("internal can be %s:%d\r\n", addr_cidr, ntohs(sockaddr_in1->sin_port));
            } else {
                printf("unsupported internal addrinfo family: %d with socktype: %d\r\n", addrinfo1->ai_family, addrinfo1->ai_socktype);
            }
        }
        freeaddrinfo(addrinfos);
        // re-get
        tryed = 0; addrinfos = NULL;
        gai_errno = getaddrinfo(worker_node, NULL, NULL, &addrinfos);
        if(gai_errno != 0) {
            printf("getaddrinfo of worker node(%s) failed, error: %s\r\n", worker_node, gai_strerror(gai_errno));
            if(errno != EAI_AGAIN || tryed >= 3) {
                return (-1);
            } else {
                printf("we will try later within 1 second\r\n");
                sleep(1);
                tryed += 1;
            }
        }
        // pick up worker addr info
        for(struct addrinfo* addrinfo1 = addrinfos; addrinfo1 != NULL; addrinfo1 = addrinfo1->ai_next) {
            if(addrinfo1->ai_family == AF_INET && addrinfo1->ai_socktype == SOCK_STREAM) {
                struct sockaddr* sockaddr1 = (struct sockaddr*)calloc(1, sizeof(struct sockaddr));
                memcpy(sockaddr1, addrinfo1->ai_addr, sizeof(struct sockaddr));
                worker_addrs[worker_addr_cursor ++] = sockaddr1;

                struct sockaddr_in* sockaddr_in1 = (struct sockaddr_in*)sockaddr1;
                sockaddr_in1->sin_port = htons(worker_port);

                char addr_cidr[1024] = {0};
                inet_ntop(AF_INET, &sockaddr_in1->sin_addr, addr_cidr, sizeof(addr_cidr));
                printf("worker can be %s:%d\r\n", addr_cidr, ntohs(sockaddr_in1->sin_port));
            } else {
                printf("unsupported worker addrinfo family: %d with socktype: %d\r\n", addrinfo1->ai_family, addrinfo1->ai_socktype);
            }
        }
        freeaddrinfo(addrinfos);
    }

    return (slave(internal_addrs, worker_addrs));
}

static struct sockaddr** g_worker_addrs = NULL;
static SPeerCtx g_internal_peer_ctx = {0};

static void internal_peer_cb(struct ev_loop* event_loop, ev_io* io, int events);

/**
 * 连接internal peer,开始事件循环
 */
static int slave(struct sockaddr** internal_addrs, struct sockaddr** worker_addrs) {
    g_worker_addrs = worker_addrs;

    struct ev_loop* event_loop = ev_default_loop(0);
    if(event_loop == NULL) {
        printf("get default loop with 0 failed\r\n");
        return (-1);
    }

    { // connect internal
         int internal_peer = socket(AF_INET, SOCK_STREAM, 0);

        bool connected = false;
        for (int internal_addr_cursor = 0; internal_addrs[internal_addr_cursor] != NULL; ++internal_addr_cursor) {
            struct sockaddr* internal_addr = internal_addrs[internal_addr_cursor];
            if(connect(internal_peer, internal_addr, sizeof(struct sockaddr)) == -1) {
                printf("connect internal failed, errno: %d\r\n", errno);
            } else {
                // set internal peer to non-block
                int flags = fcntl(internal_peer, F_GETFL, 0);
                if(flags == -1) {
                    printf("fcntl internal_peer F_GETFL failed, errno: %d\r\n", errno);
                    break;
                }
                if(fcntl(internal_peer, F_SETFL, (flags | O_NONBLOCK)) == -1) {
                    printf("fcntl internal_peer F_SETFL O_NONBLOCK failed, errno: %d\r\n", errno);
                    break;
                }

                g_internal_peer_ctx.fd = internal_peer;
                g_internal_peer_ctx.addr = (*internal_addr);
                g_internal_peer_ctx.rbuf_size = sizeof(g_internal_peer_ctx.rbuf);
                g_internal_peer_ctx.rbuf_pos = g_internal_peer_ctx.rbuf_len = 0;
                g_internal_peer_ctx.wbuf_size = sizeof(g_internal_peer_ctx.wbuf);
                g_internal_peer_ctx.wbuf_pos = g_internal_peer_ctx.wbuf_len = 0;
                // register libev
                ev_io_init(&g_internal_peer_ctx.io, internal_peer_cb, g_internal_peer_ctx.fd, EV_READ|EV_WRITE);
                ev_io_start(event_loop, &g_internal_peer_ctx.io);

                connected = true;
                break;
            }
        }
        if(!connected) {
            printf("sorry, we have tried all addrs, but couldn't connect internal host\r\n");
            return (-1);
        }
    }

    printf("rolling...\r\n");
    return (ev_run(event_loop, 0));
}

static map<int32_t, SPeerCtx*> g_shadow_peer_ctxes; // id: ctx
static map<int, int32_t> g_shadow_peer_fd2id;

/**
 *
 */
static void shadow_peer_cb(struct ev_loop* event_loop, ev_io* io, int events) {
    map<int, int32_t>::iterator shadow_peer_fd2id_ite = g_shadow_peer_fd2id.find(io->fd);
    if(shadow_peer_fd2id_ite == g_shadow_peer_fd2id.end()) {
        return ;
    }
    map<int32_t, SPeerCtx*>::iterator peer_ctx_ite = g_shadow_peer_ctxes.find(shadow_peer_fd2id_ite->second);
    if(peer_ctx_ite == g_shadow_peer_ctxes.end()) { // everything is impossible
        return ;
    }
    SPeerCtx* shadow_peer_ctx = peer_ctx_ite->second;
    if(events & EV_WRITE) {
        if(shadow_peer_ctx->wbuf_len > 0) {
            ssize_t bytes_written = write(shadow_peer_ctx->fd, shadow_peer_ctx->wbuf+ shadow_peer_ctx->wbuf_pos, shadow_peer_ctx->wbuf_len);
            if(bytes_written == -1) { // errno
                if(errno == EWOULDBLOCK) { // blocked
                    return ;
                }
            } else {
                shadow_peer_ctx->wbuf_pos += bytes_written;
                shadow_peer_ctx->wbuf_len -= bytes_written;
            }
            // wbuf is used half, align
            if(shadow_peer_ctx->wbuf_pos > shadow_peer_ctx->wbuf_size/2) {
                memmove(shadow_peer_ctx->wbuf, shadow_peer_ctx->wbuf+ shadow_peer_ctx->wbuf_pos, shadow_peer_ctx->wbuf_len);
                shadow_peer_ctx->wbuf_pos = 0;
            }
        }
    }
    if(events & EV_READ) {
        size_t bytes2read = shadow_peer_ctx->rbuf_size - (shadow_peer_ctx->rbuf_pos+ shadow_peer_ctx->rbuf_len);
        ssize_t bytes_read = read(shadow_peer_ctx->fd, shadow_peer_ctx->rbuf+ shadow_peer_ctx->rbuf_pos+ shadow_peer_ctx->rbuf_len, bytes2read);
        printf("shadow peer buf: %s\r\n", shadow_peer_ctx->rbuf+shadow_peer_ctx->rbuf_pos);
        if (bytes_read == -1) { // errno
            if (errno == EAGAIN) { // non-block but data is not ready
                return ;
            }
        } else if (bytes_read == 0) { // eof of a socket???
            printf("shadow peer is eof?!!!\r\n");
            char peer_cidr[1024] = {0};
            inet_ntop(AF_INET, &((struct sockaddr_in*)&shadow_peer_ctx->addr)->sin_addr, peer_cidr, sizeof(peer_cidr));
            printf("shawdow peer(fd: %d, addr(%s:%d)) is eof?!!!\r\n",
                   shadow_peer_ctx->fd, peer_cidr, ((struct sockaddr_in*)&shadow_peer_ctx->addr)->sin_port);
            // flush into internal peer's wbuf
            size_t internal_peer_wbuf_space = g_internal_peer_ctx.wbuf_size-(g_internal_peer_ctx.wbuf_pos+g_internal_peer_ctx.wbuf_len);
            // if internal peer's wbuf space is too small, can't flush now
            if(internal_peer_wbuf_space >= PKG_HEADER_SIZE) {
                char pkg_header[PKG_HEADER_SIZE] = {0};
                {
                    int8_t* pkg_peer_id_bytes = (int8_t*)&shadow_peer_fd2id_ite->second; // assumed little-endian
                    pkg_header[4] = pkg_peer_id_bytes[3]; pkg_header[5] = pkg_peer_id_bytes[2];
                    pkg_header[6] = pkg_peer_id_bytes[1]; pkg_header[7] = pkg_peer_id_bytes[0];
                }
                pkg_header[8] = 'L'; pkg_header[9] = 'C'; // LC
                memcpy(g_internal_peer_ctx.wbuf+g_internal_peer_ctx.wbuf_pos+g_internal_peer_ctx.wbuf_len, pkg_header, PKG_HEADER_SIZE);
                g_internal_peer_ctx.wbuf_len += PKG_HEADER_SIZE;

                ev_io_stop(event_loop, io);
                close(shadow_peer_ctx->fd);
                shadow_peer_ctx->fd = 0;

                g_shadow_peer_fd2id.erase(shadow_peer_ctx->fd);
                g_shadow_peer_ctxes.erase(shadow_peer_fd2id_ite->second);
            }
        } else {
            shadow_peer_ctx->rbuf_len += bytes_read;

            // flush into internal peer's wbuf
            size_t internal_peer_wbuf_space =
                    g_internal_peer_ctx.wbuf_size - (g_internal_peer_ctx.wbuf_pos + g_internal_peer_ctx.wbuf_len);
            size_t shadow_peer_rbuf_sendable_bytes = min(internal_peer_wbuf_space-PKG_HEADER_SIZE, shadow_peer_ctx->rbuf_len);
            // if internal peer's wbuf space is too small, can't flush now
            if (internal_peer_wbuf_space > PKG_HEADER_SIZE) {
                char peer_pkg_header[PKG_HEADER_SIZE] = {0}; // big-endian
                int32_t peer_pkg_payload_len = shadow_peer_rbuf_sendable_bytes;
                {
                    int8_t *pkg_payload_len_bytes = (int8_t *) &peer_pkg_payload_len; // assumed little-endian now
                    peer_pkg_header[0] = pkg_payload_len_bytes[3];
                    peer_pkg_header[1] = pkg_payload_len_bytes[2];
                    peer_pkg_header[2] = pkg_payload_len_bytes[1];
                    peer_pkg_header[3] = pkg_payload_len_bytes[0];
                    int8_t *peer_id_bytes = (int8_t *) &shadow_peer_ctx->fd; // assumed little-endian
                    peer_pkg_header[4] = peer_id_bytes[3];
                    peer_pkg_header[5] = peer_id_bytes[2];
                    peer_pkg_header[6] = peer_id_bytes[1];
                    peer_pkg_header[7] = peer_id_bytes[0];
                }
                peer_pkg_header[8] = 'D'; peer_pkg_header[9] = 'P'; // Data Payload
                memcpy(g_internal_peer_ctx.wbuf + g_internal_peer_ctx.wbuf_pos+g_internal_peer_ctx.wbuf_len, peer_pkg_header, PKG_HEADER_SIZE);
                g_internal_peer_ctx.wbuf_len += PKG_HEADER_SIZE;
                memcpy(g_internal_peer_ctx.wbuf + g_internal_peer_ctx.wbuf_pos + g_internal_peer_ctx.wbuf_len,
                       shadow_peer_ctx->rbuf + shadow_peer_ctx->rbuf_pos, shadow_peer_rbuf_sendable_bytes);
                g_internal_peer_ctx.wbuf_len += shadow_peer_rbuf_sendable_bytes;

                shadow_peer_ctx->rbuf_pos += shadow_peer_rbuf_sendable_bytes;
                shadow_peer_ctx->rbuf_len -= shadow_peer_rbuf_sendable_bytes;
            }
            // peer's rbuf used half, align
            if (shadow_peer_ctx->rbuf_pos > shadow_peer_ctx->rbuf_size / 2) {
                memmove(shadow_peer_ctx->rbuf, shadow_peer_ctx->rbuf + shadow_peer_ctx->rbuf_pos, shadow_peer_ctx->rbuf_len);
                shadow_peer_ctx->rbuf_pos = 0;
            }
        } // read returns
    } // events & EV_READ
}

static void consume_internal_peer_pkg(struct ev_loop* event_loop) {
    // enough data for a pkg
    if(g_internal_peer_ctx.rbuf_len < PKG_HEADER_SIZE) return ; // nothing to do
    int8_t* pkg_buf = (int8_t*)g_internal_peer_ctx.rbuf+g_internal_peer_ctx.rbuf_pos;
    int32_t pkg_payload_len = -1;
    int32_t pkg_shadow_peer_id = -1;
    { // parse pkg payload len and shadow peer id
        // TODO: handle endian
        int8_t* pkg_payload_len_bytes = (int8_t*)&pkg_payload_len;
        int8_t*pkg_shadow_peer_id_bytes = (int8_t*)&pkg_shadow_peer_id;
        pkg_payload_len_bytes[0] = pkg_buf[3]; pkg_payload_len_bytes[1] = pkg_buf[2];
        pkg_payload_len_bytes[2] = pkg_buf[1]; pkg_payload_len_bytes[3] = pkg_buf[0];
        pkg_shadow_peer_id_bytes[0] = pkg_buf[7]; pkg_shadow_peer_id_bytes[1] = pkg_buf[6];
        pkg_shadow_peer_id_bytes[2] = pkg_buf[5]; pkg_shadow_peer_id_bytes[3] = pkg_buf[4];
    }
    if(g_internal_peer_ctx.rbuf_len < PKG_HEADER_SIZE+pkg_payload_len) return ; // data is still not enough
    // determine cmd
    int bytes_consumed = PKG_HEADER_SIZE+pkg_payload_len; // how many bytes consumed this time
    if(memcmp(pkg_buf+8, "NC", 2) == 0) { // New Connection
        // connect to worker
        int worker_peer_fd = socket(AF_INET, SOCK_STREAM, 0);
        bool connected = false;
        for(int worker_addr_curosr = 0; g_worker_addrs[worker_addr_curosr] != NULL; ++ worker_addr_curosr) {
            struct sockaddr* worker_addr = g_worker_addrs[worker_addr_curosr];
            if(connect(worker_peer_fd, worker_addr, sizeof(struct sockaddr)) == -1) {
                printf("connect worker failed, errno: %d\r\n", errno);
            } else {
                // set worker peer to non-block
                int flags = fcntl(worker_peer_fd, F_GETFL, 0);
                if(flags == -1) {
                    printf("fcntl worker_peer F_GETFL failed, errno: %d\r\n", errno);
                    break;
                }
                if(fcntl(worker_peer_fd, F_SETFL, (flags | O_NONBLOCK)) == -1) {
                    printf("fcntl worker_peer F_SETFL O_NONBLOCK failed, errno: %d\r\n", errno);
                    break;
                }

                g_shadow_peer_fd2id[worker_peer_fd] = pkg_shadow_peer_id;
                if(g_shadow_peer_ctxes.find(pkg_shadow_peer_id) == g_shadow_peer_ctxes.end()) {
                    g_shadow_peer_ctxes[pkg_shadow_peer_id] = (SPeerCtx*)calloc(1, sizeof(SPeerCtx));
                }
                SPeerCtx* shadow_peer_ctx = g_shadow_peer_ctxes[pkg_shadow_peer_id];
                shadow_peer_ctx->fd = worker_peer_fd;
                shadow_peer_ctx->addr = (*worker_addr);
                shadow_peer_ctx->rbuf_size = sizeof(shadow_peer_ctx->rbuf);
                shadow_peer_ctx->rbuf_pos = shadow_peer_ctx->rbuf_len = 0;
                shadow_peer_ctx->wbuf_size = sizeof(shadow_peer_ctx->wbuf);
                shadow_peer_ctx->wbuf_pos = shadow_peer_ctx->wbuf_len = 0;
                // register libev
                ev_io_init(&shadow_peer_ctx->io, shadow_peer_cb, shadow_peer_ctx->fd, EV_READ|EV_WRITE);
                ev_io_start(event_loop, &shadow_peer_ctx->io);

                connected = true;
                break;
            }
        }
        if(!connected) {
            printf("sorry, we have tried all addrs, but couldn't connect worker host\r\n");
            return ;
        } else {
            printf("New Connection handled\r\n");
        }
    } else {
        map<int32_t, SPeerCtx*>::iterator pkg_shadow_peer_ctx_ite = g_shadow_peer_ctxes.find(pkg_shadow_peer_id);
        if (pkg_shadow_peer_ctx_ite != g_shadow_peer_ctxes.end()) { // dest peer is gone?!
            SPeerCtx* pkg_shadow_peer_ctx = pkg_shadow_peer_ctx_ite->second;
            if (memcmp(pkg_buf + 8, "DP", 2) == 0) { // Data Payload
                // flush payload to external peer's wbuf
                size_t pkg_shadow_peer_wbuf_space = pkg_shadow_peer_ctx->wbuf_size -
                                                      (pkg_shadow_peer_ctx->wbuf_pos + pkg_shadow_peer_ctx->wbuf_len);
                // external peer's wbuf space is too small, can't flush now
                if (pkg_shadow_peer_wbuf_space < pkg_payload_len) {
                    bytes_consumed = 0;
                    return ;
                } else {
                    memcpy(pkg_shadow_peer_ctx->wbuf + pkg_shadow_peer_ctx->wbuf_pos + pkg_shadow_peer_ctx->wbuf_len,
                           pkg_buf + PKG_HEADER_SIZE, pkg_payload_len);
                    pkg_shadow_peer_ctx->wbuf_len += pkg_payload_len;
                }
            } else if (memcmp(pkg_buf + 8, "LC", 2) == 0) { // Lost Connection
                g_shadow_peer_fd2id.erase(pkg_shadow_peer_ctx->fd);
                ev_io_stop(event_loop, &pkg_shadow_peer_ctx->io);
                close(pkg_shadow_peer_ctx->fd);
                g_shadow_peer_ctxes.erase(pkg_shadow_peer_ctx_ite);
            }
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
        if (g_internal_peer_ctx.wbuf_len > 0) {
            ssize_t bytes_written = write(io->fd, g_internal_peer_ctx.wbuf + g_internal_peer_ctx.wbuf_pos,
                                          g_internal_peer_ctx.wbuf_len);
            if (bytes_written == -1) { // errno
                if (errno == EWOULDBLOCK) { // blocked
                    return;
                }
            } else {
                g_internal_peer_ctx.wbuf_pos += bytes_written;
                g_internal_peer_ctx.wbuf_len -= bytes_written;
            }
            // wbuf is used half, align
            if (g_internal_peer_ctx.wbuf_pos > g_internal_peer_ctx.wbuf_size / 2) {
                memmove(g_internal_peer_ctx.wbuf, g_internal_peer_ctx.wbuf + g_internal_peer_ctx.wbuf_pos,
                        g_internal_peer_ctx.wbuf_len);
                g_internal_peer_ctx.wbuf_pos = 0;
            }
        }
    }
    if(events & EV_READ) {
        size_t bytes2read = g_internal_peer_ctx.rbuf_size - (g_internal_peer_ctx.rbuf_pos+g_internal_peer_ctx.rbuf_len);
        ssize_t bytes_read = read(g_internal_peer_ctx.fd, g_internal_peer_ctx.rbuf+g_internal_peer_ctx.rbuf_pos+g_internal_peer_ctx.rbuf_len, bytes2read);
        if (bytes_read == -1) { // errno
            if (errno == EAGAIN) { // non-block but data is not ready
                return ;
            }
        } else if (bytes_read == 0) { // eof of a socket?!!
            char peer_cidr[1024] = {0};
            inet_ntop(AF_INET, &((struct sockaddr_in*)&g_internal_peer_ctx.addr)->sin_addr, peer_cidr, sizeof(peer_cidr));
            printf("internal peer(fd: %d, addr(%s:%d)) is eof?!!!\r\n",
                   io->fd, peer_cidr, ((struct sockaddr_in*)&g_internal_peer_ctx.addr)->sin_port);
            ev_io_stop(event_loop, io);
            g_internal_peer_ctx.fd = 0;

            ev_break(event_loop, EVBREAK_ALL);
        } else {
            g_internal_peer_ctx.rbuf_len += bytes_read;

            consume_internal_peer_pkg(event_loop);
        }
    }
}
