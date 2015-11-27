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

    // TODO: IPV6
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
static shared_ptr<CPeerCtx> g_internal_peer_ctx;

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
#if defined(__APPLE__)
    // ignore sigpipe
    static const int ignore = 1;
    if (setsockopt(internal_peer, SOL_SOCKET, SO_NOSIGPIPE, (void*)&ignore, sizeof(ignore)) != 0) {
      printf("setsockopt internal_peer SO_NOSIGPIPE failed, errno: %d\r\n", errno);
      return (-1);
    }
#endif    
    
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
	
	g_internal_peer_ctx = shared_ptr<CPeerCtx>(new CPeerCtx(internal_peer, internal_peer, (struct sockaddr*)internal_addr));
	// register libev, then start
	g_internal_peer_ctx->initCallback(internal_peer_cb, EV_READ|EV_WRITE);
	g_internal_peer_ctx->start(event_loop);

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

static map<int32_t, shared_ptr<CPeerCtx> > g_shadow_peer_ctxes; // id: ctx
static map<int32_t, int32_t> g_shadow_peer_fd2id;

/**
 *
 */
static void shadow_peer_cb(struct ev_loop* event_loop, ev_io* io, int events) {
  map<int, int32_t>::iterator shadow_peer_fd2id_ite = g_shadow_peer_fd2id.find(io->fd);
  if(shadow_peer_fd2id_ite == g_shadow_peer_fd2id.end()) {
    return ;
  }
  map<int32_t, shared_ptr<CPeerCtx> >::iterator peer_ctx_ite = g_shadow_peer_ctxes.find(shadow_peer_fd2id_ite->second);
  if(peer_ctx_ite == g_shadow_peer_ctxes.end()) { // everything is possible
    return ;
  }
  shared_ptr<CPeerCtx> shadow_peer_ctx = peer_ctx_ite->second;
  if(events & EV_WRITE) {
    shadow_peer_ctx->flush();
  }
  if(events & EV_READ) {
    int draw_result = shadow_peer_ctx->draw();
    if (draw_result == -1) { // read next time
      return ;
    } else if (draw_result == 0) { // eof of a socket???
      printf("shadow peer is eof?!!!\r\n");
      char peer_cidr[1024] = {0};
      inet_ntop(AF_INET, &((struct sockaddr_in*)shadow_peer_ctx->addr())->sin_addr, peer_cidr, sizeof(peer_cidr));
      printf("shawdow peer(fd: %d, addr(%s:%d)) is eof?!!!\r\n",
	     shadow_peer_ctx->fd(), peer_cidr, ((struct sockaddr_in*)shadow_peer_ctx->addr())->sin_port);
      // flush into internal peer's wbuf
      char lc_pkg_header[PKG_HEADER_SIZE] = {0};
      {
	int8_t* pkg_peer_id_bytes = (int8_t*)&shadow_peer_fd2id_ite->second; // assumed little-endian
	lc_pkg_header[4] = pkg_peer_id_bytes[3]; lc_pkg_header[5] = pkg_peer_id_bytes[2];
	lc_pkg_header[6] = pkg_peer_id_bytes[1]; lc_pkg_header[7] = pkg_peer_id_bytes[0];
      }
      lc_pkg_header[8] = 'L'; lc_pkg_header[9] = 'C'; // LC
      g_internal_peer_ctx->pushWbuf(lc_pkg_header, PKG_HEADER_SIZE);
      
      g_shadow_peer_ctxes.erase(shadow_peer_fd2id_ite->second);
      g_shadow_peer_fd2id.erase(shadow_peer_ctx->fd());
    } else {
      // flush into internal peer's wbuf
      char dp_peer_pkg_header[PKG_HEADER_SIZE] = {0}; // big-endian
      int32_t peer_pkg_payload_len = shadow_peer_ctx->rbufLen();
      {
	int8_t *pkg_payload_len_bytes = (int8_t *) &peer_pkg_payload_len; // assumed little-endian now
	dp_peer_pkg_header[0] = pkg_payload_len_bytes[3];
	dp_peer_pkg_header[1] = pkg_payload_len_bytes[2];
	dp_peer_pkg_header[2] = pkg_payload_len_bytes[1];
	dp_peer_pkg_header[3] = pkg_payload_len_bytes[0];
	int32_t shadow_peer_id = shadow_peer_ctx->id();
	int8_t *peer_id_bytes = (int8_t *) &shadow_peer_id; // assumed little-endian
	dp_peer_pkg_header[4] = peer_id_bytes[3];
	dp_peer_pkg_header[5] = peer_id_bytes[2];
	dp_peer_pkg_header[6] = peer_id_bytes[1];
	dp_peer_pkg_header[7] = peer_id_bytes[0];
      }
      dp_peer_pkg_header[8] = 'D'; dp_peer_pkg_header[9] = 'P'; // Data Payload
      g_internal_peer_ctx->pushWbuf(dp_peer_pkg_header, PKG_HEADER_SIZE);
      g_internal_peer_ctx->pushWbuf(shadow_peer_ctx->rbuf(), peer_pkg_payload_len);
    

      shadow_peer_ctx->purgeRbuf();
    } // read returns
  } // events & EV_READ
}

static void consume_internal_peer_pkg(struct ev_loop* event_loop) {
  // enough data for a pkg
  size_t rbuf_len = g_internal_peer_ctx->rbufLen();
  const char* rbuf = g_internal_peer_ctx->rbuf();
  if (rbuf_len < PKG_HEADER_SIZE) return ; // nothing to do
  int32_t pkg_payload_len = -1;
  int32_t pkg_shadow_peer_id = -1;
  { // parse pkg payload len and shadow peer id
    // TODO: handle endian
    int8_t* pkg_payload_len_bytes = (int8_t*)&pkg_payload_len;
    int8_t*pkg_shadow_peer_id_bytes = (int8_t*)&pkg_shadow_peer_id;
    pkg_payload_len_bytes[0] = rbuf[3]; pkg_payload_len_bytes[1] = rbuf[2];
    pkg_payload_len_bytes[2] = rbuf[1]; pkg_payload_len_bytes[3] = rbuf[0];
    pkg_shadow_peer_id_bytes[0] = rbuf[7]; pkg_shadow_peer_id_bytes[1] = rbuf[6];
    pkg_shadow_peer_id_bytes[2] = rbuf[5]; pkg_shadow_peer_id_bytes[3] = rbuf[4];
  }
  if(rbuf_len < PKG_HEADER_SIZE+pkg_payload_len) return ; // data is still not enough
  // determine cmd
  int bytes_consumed = PKG_HEADER_SIZE+pkg_payload_len; // how many bytes consumed this time
  if(memcmp(rbuf+8, "NC", 2) == 0) { // New Connection
    // connect to worker
    int worker_peer_fd = socket(AF_INET, SOCK_STREAM, 0);
#if defined(__APPLE__)
    static const int ignore = 1;
    if (setsockopt(worker_peer_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&ignore, sizeof(ignore)) != 0) {
      printf("setsockopt worker_peer_fd SO_NOSIGPIPE failed, errno: %d\r\n", errno);
      return ;
    }
#endif
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
	shared_ptr<CPeerCtx> shadow_peer_ctx = shared_ptr<CPeerCtx>(new CPeerCtx(worker_peer_fd, pkg_shadow_peer_id, (struct sockaddr*)worker_addr));
	g_shadow_peer_ctxes[shadow_peer_ctx->id()] = shadow_peer_ctx;
	// register libev, then start
	shadow_peer_ctx->initCallback(shadow_peer_cb, EV_READ|EV_WRITE);
	shadow_peer_ctx->start(event_loop);
	
	connected = true;
	break;
      }
    }
    if(!connected) {
      printf("sorry, we have tried all addrs, but couldn't connect worker host\r\n");
    } else {
      printf("New Connection handled, shadow peer id: %d\r\n", pkg_shadow_peer_id);
    }
  } else {
    map<int32_t, shared_ptr<CPeerCtx> >::iterator pkg_shadow_peer_ctx_ite = g_shadow_peer_ctxes.find(pkg_shadow_peer_id);
    if (pkg_shadow_peer_ctx_ite != g_shadow_peer_ctxes.end()) { // dest peer is gone?!
      shared_ptr<CPeerCtx> pkg_shadow_peer_ctx = pkg_shadow_peer_ctx_ite->second;
      if (memcmp(rbuf + 8, "DP", 2) == 0) { // Data Payload
	// flush payload to shadow peer's wbuf
	pkg_shadow_peer_ctx->pushWbuf(rbuf+PKG_HEADER_SIZE, pkg_payload_len);
      } else if (memcmp(rbuf + 8, "LC", 2) == 0) { // Lost Connection
	g_shadow_peer_ctxes.erase(pkg_shadow_peer_ctx_ite);
	g_shadow_peer_fd2id.erase(pkg_shadow_peer_ctx->fd());
      }
    }
  }
  g_internal_peer_ctx->purgeRbuf(bytes_consumed);
    
  consume_internal_peer_pkg(event_loop);
}
static void internal_peer_cb(struct ev_loop* event_loop, ev_io* io, int events) {
  if(events & EV_WRITE) {
    g_internal_peer_ctx->flush();
  }
  if(events & EV_READ) {
    int draw_result = g_internal_peer_ctx->draw();
    if (draw_result == -1) { // errno
      return ;
    } else if (draw_result == 0) { // eof of a socket?!!
      char peer_cidr[1024] = {0};
      inet_ntop(AF_INET, &((struct sockaddr_in*)g_internal_peer_ctx->addr())->sin_addr, peer_cidr, sizeof(peer_cidr));
      printf("internal peer(fd: %d, addr(%s:%d)) is eof?!!!\r\n",
	     io->fd, peer_cidr, ((struct sockaddr_in*)g_internal_peer_ctx->addr())->sin_port);
      g_internal_peer_ctx.reset();

      ev_break(event_loop, EVBREAK_ALL);
    } else {
      consume_internal_peer_pkg(event_loop);
    }
  }
}
