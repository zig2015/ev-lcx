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
#include <assert.h>

#include <map>
#include <memory>

#include "common.h"

using namespace std;

static int listener(int external_port, int internal_port);

int main(int argc, char** argv)
{
  printf("Build on %s-%s. \r\nPassez un bon moment. \r\nshaw(shawhen2012@hotmail.com)\r\n", __DATE__, __TIME__);
  printf("---------------------------------------\r\n");
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
    return (listener(external_port, internal_port));
}

static void external_sock_cb(struct ev_loop* event_loop, ev_io* io, int events);
static void internal_sock_cb(struct ev_loop* event_loop, ev_io* io, int events);

static ev_timer g_kp_timer;
static void keep_live_cb(struct ev_loop* event_loop, ev_timer* timer, int events);

static int listener(const int external_port, const int internal_port) {
    // get a event loop
    struct ev_loop* event_loop = ev_default_loop(0);
    if(event_loop == NULL) {
        printf("get default loop with 0 failed\r\n");
        return (-1);
    }
    // init keep-live timer
    ev_init(&g_kp_timer, keep_live_cb);
    // allocate socks
    int external_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(external_sock == -1) {
        printf("socket external failed,errno: %d\r\n", errno);
        return (-1);
    }
#if defined(__APPLE__)
    // ignore sigpipe
    static const int ignore = 1;
    if (setsockopt(external_sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&ignore, sizeof(ignore)) != 0) {
      printf("setsockopt external_sock SO_NOSIGPIPE failed, errno: %d\r\n", errno);
      return (-1);
    }
#endif    
    // set reuseable
    static const int enable = 1;
    if(setsockopt(external_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
        printf("setsockopt external_sock SO_REUSEADDR failed, errno: %d\r\n", errno);
        return (-1);
    }
    int internal_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(internal_sock == -1) {
        printf("socket external failed,errno: %d\r\n", errno);
        return (-1);
    }
#if defined(__APPLE__)
    // ignore sigpipe
    if (setsockopt(internal_sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&ignore, sizeof(ignore)) != 0) {
      printf("setsockopt internal_sock SO_NOSIGPIPE failed, errno: %d\r\n", errno);
      return (-1);
    }
#endif    
    // set reuseable
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
    #if defined(__APPLE__)
        sockaddr_in1.sin_len = sizeof(sockaddr_in1);
    #endif
    sockaddr_in1.sin_family = AF_INET;
    sockaddr_in1.sin_port = htons(external_port);
    if(inet_pton(AF_INET, "0.0.0.0", &sockaddr_in1.sin_addr) != 1) {
        printf("inet_pton 0.0.0.0 failed, errno: %d\r\n", errno);
        return (-1);
    }
    if(::bind(external_sock, (const sockaddr*)&sockaddr_in1, sizeof(sockaddr_in1)) != 0) {
        printf("bind external_sock failed, errno: %d\r\n", errno);
        return (-1);
    }
    sockaddr_in1.sin_port = htons(internal_port);
    if(::bind(internal_sock, (const sockaddr*)&sockaddr_in1, sizeof(sockaddr_in1)) != 0) {
        printf("bind internal_sock failed, errno: %d\r\n", errno);
        return (-1);
    }
    // listen
    listen(external_sock, 1024);
    listen(internal_sock, 1024);

    // register to libev
    ev_io* external_sock_io = (ev_io*)calloc(1, sizeof(ev_io));
    ev_io* internal_sock_io = (ev_io*)calloc(1, sizeof(ev_io));
    ev_io_init(external_sock_io, external_sock_cb, external_sock, EV_READ);
    ev_io_init(internal_sock_io, internal_sock_cb, internal_sock, EV_READ);
    ev_io_start(event_loop, external_sock_io);
    ev_io_start(event_loop, internal_sock_io);

    printf("rolling...\r\n");
    return (ev_run(event_loop, 0));
}

static shared_ptr<CPeerCtx> g_internal_peer_ctx;
static map<int32_t, shared_ptr<CPeerCtx> > g_external_peer_ctxes; // id(fd): ctx

/**
 * external peer事件回调
 * 1. 可写：将external peer上的“写入缓冲数据”发送
 * 2. 可读：
 *      a. 正常读取数据，放到“读取缓冲数据”，根据情况打包DP(DataPayload)放入internal peer的“写入缓冲”
 *      b. eof，报告LC(Lost Connection)到internal peer
 */
static void external_peer_cb(struct ev_loop* event_loop, ev_io* io, int events) {
  map<int32_t, shared_ptr<CPeerCtx> >::iterator peer_ctx_ite = g_external_peer_ctxes.find(io->fd);
  if(peer_ctx_ite == g_external_peer_ctxes.end()) { // everything is possible
    return ;
  }
  shared_ptr<CPeerCtx> external_peer_ctx = peer_ctx_ite->second;
  if(events & EV_WRITE) {
    external_peer_ctx->flush();
  }
  if(events & EV_READ) {
    int draw_result = external_peer_ctx->draw();
    if (draw_result == -1) { // read next time
      return ;
    } else if (draw_result == 0) { // eof of a socket???
      printf("external peer is eof?!!!\r\n");
      char peer_cidr[1024] = {0};
      inet_ntop(AF_INET, &((struct sockaddr_in*)external_peer_ctx->addr())->sin_addr, peer_cidr, sizeof(peer_cidr));
      printf("external peer(fd: %d, addr(%s:%d)) is eof?!!!\r\n", io->fd, peer_cidr, ((struct sockaddr_in*)external_peer_ctx->addr())->sin_port);

      char lc_pkg_header[PKG_HEADER_SIZE] = {0};
      {
	int32_t peer_id = external_peer_ctx->id();
	int8_t* pkg_peer_id_bytes = (int8_t*)&peer_id; // assumed little-endian
	lc_pkg_header[4] = pkg_peer_id_bytes[3]; lc_pkg_header[5] = pkg_peer_id_bytes[2];
	lc_pkg_header[6] = pkg_peer_id_bytes[1]; lc_pkg_header[7] = pkg_peer_id_bytes[0];
      }
      lc_pkg_header[8] = 'L'; lc_pkg_header[9] = 'C'; // LC
      g_internal_peer_ctx->pushWbuf(lc_pkg_header, PKG_HEADER_SIZE);
	    
      g_external_peer_ctxes.erase(external_peer_ctx->id());
    } else {
      // 将external peer的读入缓冲区写入到internal peer的写出缓冲区
      int dp_pkg_payload_len = external_peer_ctx->rbufLen();
      char dp_external_peer_pkg_header[PKG_HEADER_SIZE] = {0}; // big-endian
      {
	int8_t *pkg_payload_len_bytes = (int8_t *) &dp_pkg_payload_len; // assumed little-endian now
	int32_t peer_id = external_peer_ctx->id();
	dp_external_peer_pkg_header[0] = pkg_payload_len_bytes[3];
	dp_external_peer_pkg_header[1] = pkg_payload_len_bytes[2];
	dp_external_peer_pkg_header[2] = pkg_payload_len_bytes[1];
	dp_external_peer_pkg_header[3] = pkg_payload_len_bytes[0];
	int8_t *peer_id_bytes = (int8_t *) &peer_id; // assumed little-endian
	dp_external_peer_pkg_header[4] = peer_id_bytes[3];
	dp_external_peer_pkg_header[5] = peer_id_bytes[2];
	dp_external_peer_pkg_header[6] = peer_id_bytes[1];
	dp_external_peer_pkg_header[7] = peer_id_bytes[0];
      }
      dp_external_peer_pkg_header[8] = 'D'; dp_external_peer_pkg_header[9] = 'P'; // Data Payload
      g_internal_peer_ctx->pushWbuf(dp_external_peer_pkg_header, PKG_HEADER_SIZE);
      const char* external_peer_rbuf = external_peer_ctx->rbuf();
      g_internal_peer_ctx->pushWbuf(external_peer_rbuf, dp_pkg_payload_len);

      external_peer_ctx->purgeRbuf();
    } // draw returns
  } // events & EV_READ
}
static void external_sock_cb(struct ev_loop* event_loop, ev_io* io, int events) {
    if(!g_internal_peer_ctx) {
        printf("internal peer is absent, please wait...\r\n");
        return ;
    }
    // peer come
    struct sockaddr_in new_external_peer_addr_in = {0};
    socklen_t peer_addr_in_len = 0;
    int new_external_peer_fd = accept(io->fd, (struct sockaddr*)&new_external_peer_addr_in, &peer_addr_in_len);
    if(new_external_peer_fd < 0) { // errno
        if(errno == EWOULDBLOCK) {
            return ;
        }
    }
#if defined(__APPLE__)
    // ignore sigpipe
    static const int ignore = 1;
    if (setsockopt(new_external_peer_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&ignore, sizeof(ignore)) != 0) {
      printf("setsockopt external_sock SO_NOSIGPIPE failed, errno: %d\r\n", errno);
      return ;
    }
#endif
    // set non-block
    int flags = fcntl(new_external_peer_fd, F_GETFL, 0);
    if(flags == -1) {
        printf("fcntl new_external_peer_fd F_GETFL failed, errno: %d\r\n", errno);
        return ;
    }
    if(fcntl(new_external_peer_fd, F_SETFL, (flags | O_NONBLOCK)) != 0) {
        printf("fcntl new_external_peer_fd F_SETFL failed, errno: %d\r\n", errno);
	return ;
    }
    
    shared_ptr<CPeerCtx> new_external_peer_ctx = shared_ptr<CPeerCtx>(new CPeerCtx(new_external_peer_fd, new_external_peer_fd, (struct sockaddr*)&new_external_peer_addr_in));
    g_external_peer_ctxes[new_external_peer_ctx->id()] = new_external_peer_ctx;
    // register to libev, then start loop
    new_external_peer_ctx->initCallback(external_peer_cb, EV_READ|EV_WRITE);
    new_external_peer_ctx->start(event_loop);

    char peer_cidr[1024] = {0};
    if(inet_ntop(AF_INET, &new_external_peer_addr_in.sin_addr, peer_cidr, sizeof(peer_cidr)) == NULL) {
        printf("****convert external_sock's peer cidr failed, errno: %d****\r\n", errno);
    }
    printf("got a external peer - fd(%d), addr(%s:%d)\r\n", new_external_peer_fd, peer_cidr, ntohs(new_external_peer_addr_in.sin_port));
    // push New Connection to internal peer
    char nc_pkg_header[PKG_HEADER_SIZE] = {0};
    {
      int32_t peer_id = new_external_peer_ctx->id();
      int8_t* pkg_peer_id_bytes = (int8_t*)&peer_id; // assumed little-endian
      printf("\tmake it a id: %d\r\n", new_external_peer_ctx->id());
      nc_pkg_header[4] = pkg_peer_id_bytes[3]; nc_pkg_header[5] = pkg_peer_id_bytes[2];
      nc_pkg_header[6] = pkg_peer_id_bytes[1]; nc_pkg_header[7] = pkg_peer_id_bytes[0];
    }
    nc_pkg_header[8] = 'N'; nc_pkg_header[9] = 'C'; // NC
    g_internal_peer_ctx->pushWbuf(nc_pkg_header, PKG_HEADER_SIZE);
}

static const ev_tstamp g_kp_timeout = 60;
static ev_tstamp g_last_activity = 0;
static void keep_live_cb(struct ev_loop* event_loop, ev_timer* timer, int events) {
  ev_tstamp after = (g_last_activity-ev_now(event_loop)) + g_kp_timeout;
  if (after < 0) { // died
    printf("\r\ninternal peer has died\r\n");
    g_internal_peer_ctx.reset();
  } else {
    ev_timer_set(timer, g_kp_timeout, 0);
    ev_timer_start(event_loop, timer);

    char kp_pkg_header[PKG_HEADER_SIZE] = {0};
    {
      int32_t internal_peer_id = g_internal_peer_ctx->id();
      int8_t* internal_peer_id_bytes = (int8_t*)&internal_peer_id;
      // todo: assumed host is little-endian
      kp_pkg_header[4] = internal_peer_id_bytes[3]; kp_pkg_header[5] = internal_peer_id_bytes[2];
      kp_pkg_header[6] = internal_peer_id_bytes[1]; kp_pkg_header[7] = internal_peer_id_bytes[0];
    }
    kp_pkg_header[8] = 'K'; kp_pkg_header[9] = 'L'; // Keep-Live
    g_internal_peer_ctx->pushWbuf(kp_pkg_header, PKG_HEADER_SIZE);
  }
}

// TODO: if a external peer is slow to send, then the internal peer read will be blocked

static void consume_internal_peer_pkg(struct ev_loop* event_loop) {
  // enough data for a pkg
  const  int rbuf_len = g_internal_peer_ctx->rbufLen();
  const char* rbuf = g_internal_peer_ctx->rbuf();
  if(rbuf_len < PKG_HEADER_SIZE) return ; // nothing to do
  int32_t pkg_payload_len = -1;
  int32_t pkg_external_peer_id = -1;
  { // parse pkg payload len and external peer id
    // TODO: handle endian
    int8_t* pkg_payload_len_bytes = (int8_t*)&pkg_payload_len;
    int8_t* pkg_external_peer_id_bytes = (int8_t*)&pkg_external_peer_id;
    pkg_payload_len_bytes[0] = rbuf[3]; pkg_payload_len_bytes[1] = rbuf[2];
    pkg_payload_len_bytes[2] = rbuf[1]; pkg_payload_len_bytes[3] = rbuf[0];
    pkg_external_peer_id_bytes[0] = rbuf[7]; pkg_external_peer_id_bytes[1] = rbuf[6];
    pkg_external_peer_id_bytes[2] = rbuf[5]; pkg_external_peer_id_bytes[3] = rbuf[4];
  }
  if(rbuf_len < PKG_HEADER_SIZE+pkg_payload_len) return ; // data is still not enough
  // determine cmd
  int bytes_consumed = PKG_HEADER_SIZE+pkg_payload_len; // how many bytes consumed this time
  map<int32_t, shared_ptr<CPeerCtx> >::iterator pkg_external_peer_ctx_ite = g_external_peer_ctxes.find(pkg_external_peer_id);
  if(pkg_external_peer_ctx_ite != g_external_peer_ctxes.end()) { // dest peer is gone?!
    shared_ptr<CPeerCtx> pkg_external_peer_ctx = pkg_external_peer_ctx_ite->second;
    if(memcmp(rbuf+8, "DP", 2) == 0) { // Data Payload
      // flush payload to external peer's wbuf
      pkg_external_peer_ctx->pushWbuf(rbuf+PKG_HEADER_SIZE, pkg_payload_len);
    } else if (memcmp(rbuf+8, "KP", 2) == 0) { // Keep-Live
      ;
    } else if (memcmp(rbuf+8, "LC", 2) == 0) { // Lost Connection
      g_external_peer_ctxes.erase(pkg_external_peer_ctx_ite);
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
    if (draw_result == -1) { // read next time
      return ;
    } else if (draw_result == 0) { // eof of a socket?!!
      char peer_cidr[1024] = {0};
      inet_ntop(AF_INET, &((const struct sockaddr_in*)g_internal_peer_ctx->addr())->sin_addr, peer_cidr, sizeof(peer_cidr));
      printf("internal peer(fd: %d, addr(%s:%d)) is eof?!!!\r\n",
	     io->fd, peer_cidr, ((const struct sockaddr_in*)g_internal_peer_ctx->addr())->sin_port);
      g_internal_peer_ctx.reset();
      // TODO: clear current external peers
    } else {
      g_last_activity = ev_now(event_loop);
      consume_internal_peer_pkg(event_loop);
    }
  }
}
static void internal_sock_cb(struct ev_loop* event_loop, ev_io* io, int events) {
  if(g_internal_peer_ctx) {
    struct sockaddr_in* internal_peer_addr_in = (struct sockaddr_in*)g_internal_peer_ctx->addr();
    char internal_peer_cidr[1024] = {0};
    inet_ntop(AF_INET, &internal_peer_addr_in->sin_addr, internal_peer_cidr, sizeof(internal_peer_cidr));
    printf("internal sock has been filled - id(%d), addr(%s:%d)\r\n",
	   g_internal_peer_ctx->id(), internal_peer_cidr, ntohs(internal_peer_addr_in->sin_port));
    return ;
  }

  // peer come
  struct sockaddr_in internal_peer_addr_in = {0};
  socklen_t peer_addr_in_len = 0;
  int peer_fd = accept(io->fd, (struct sockaddr*)&internal_peer_addr_in, &peer_addr_in_len);
  if(peer_fd < 0) { // errno
    if(errno == EWOULDBLOCK) {
      return ;
    }
  }
#if defined(__APPLE__)
    // ignore sigpipe
    static const int ignore = 1;
    if (setsockopt(peer_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&ignore, sizeof(ignore)) != 0) {
      printf("setsockopt internal sock peer SO_NOSIGPIPE failed, errno: %d\r\n", errno);
      return ;
    }
#endif
    // set non-block
    int flags = fcntl(peer_fd, F_GETFL, 0);
    if(flags == -1) {
        printf("fcntl internal sock peer F_GETFL failed, errno: %d\r\n", errno);
        return ;
    }
    if(fcntl(peer_fd, F_SETFL, (flags | O_NONBLOCK)) != 0) {
        printf("fcntl internal sock peer F_SETFL failed, errno: %d\r\n", errno);
	return ;
    }  
  // TODO: i think we need some auth
  g_internal_peer_ctx = shared_ptr<CPeerCtx>(new CPeerCtx(peer_fd, peer_fd, (struct sockaddr*)&internal_peer_addr_in));
  // register peer to libev, then start
  g_internal_peer_ctx->initCallback(internal_peer_cb, EV_READ|EV_WRITE);
  g_internal_peer_ctx->start(event_loop);

  struct ev_loop* loop = ev_default_loop(0);
  assert(loop);
  g_last_activity = ev_now(EV_A);
  keep_live_cb(loop, &g_kp_timer, 0);

  char peer_cidr[1024] = {0};
  if(inet_ntop(AF_INET, &internal_peer_addr_in.sin_addr, peer_cidr, sizeof(peer_cidr)) == NULL) {
      printf("convert internal_sock's peer cidr failed, errno: %d\r\n", errno);
  }
  printf("got a internal peer - fd(%d), addr(%s:%d)\r\n", peer_fd, peer_cidr, ntohs(internal_peer_addr_in.sin_port));
}
