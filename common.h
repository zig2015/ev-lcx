//
// Created by shaw on 10/18/15.
//

#pragma once

#include <errno.h>

#include <ev.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <memory>

using namespace std;

class CPeerCtx {
private:
  int m_fd;
  int32_t m_id;
  struct sockaddr m_addr;
  ev_io* m_io;
  char* m_rbuf;
  size_t m_rbuf_len;
  char* m_wbuf;
  size_t m_wbuf_len;

  struct ev_loop* m_loop;
public:
    CPeerCtx(const int fd, const int32_t id, const struct sockaddr* addr) {
      this->m_fd = fd;
      this->m_id = id;
      this->m_addr = (*addr);
      this->m_io = (ev_io*)calloc(1, sizeof(ev_io));
      this->m_rbuf = NULL;
      this->m_rbuf_len = 0;
      this->m_wbuf = NULL;
      this->m_wbuf_len = 0;
    }

    virtual ~CPeerCtx() {
      printf("destroy peer ctx\r\n");
      ev_io_stop(this->m_loop, this->m_io);
      close(this->m_io->fd);
      free(this->m_rbuf);
      free(this->m_wbuf);
    }

    int initCallback(void (*cb)(struct ev_loop*, ev_io*, int), const int events) {
      ev_io_init(this->m_io, cb, this->m_fd, events);
      return (0);
    }

    int setEvents(const int events) {
      ev_io_set(this->m_io, this->m_fd, events);
      return (0);
    }

    int start(struct ev_loop* loop) {
      this->m_loop = loop;
      ev_io_start(loop, this->m_io);
      return (0);
    }

    int pushWbuf(const char* buf, const size_t len) {
      char* finalWbuf = (char*)malloc(this->m_wbuf_len+len);
      memcpy(finalWbuf, this->m_wbuf, this->m_wbuf_len);
      memcpy(finalWbuf+this->m_wbuf_len, buf, len);
      free(this->m_wbuf);
      this->m_wbuf = finalWbuf;
      this->m_wbuf_len = this->m_wbuf_len+len;
      return (0);
    }

    /**
     * write out wbuf
     */
    int flush() {

      printf("\rid:%4d(fd:%4d) flush once, wbuf_len:%4zu", this->m_id, this->m_fd, this->m_wbuf_len);
      if (this->m_wbuf_len <= 0) {
	return (-1);
      }
      printf("\r\nid:%4d(fd:%4d) flush once, wbuf_len:%4zu\r\n", this->m_id, this->m_fd, this->m_wbuf_len);
#if defined(__linux__)
      int result = send(this->m_io->fd, this->m_wbuf, this->m_wbuf_len, MSG_NOSIGNAL);
#elif defined(__APPLE__)
      int result = write(this->m_io->fd, this->m_wbuf, this->m_wbuf_len);
#endif	    
      if (result == -1) {
	if (errno == EWOULDBLOCK) {
	  result = -1;
	} else if (errno == EPIPE) {
	  result = 0;
	}
      } else {
	printf("flush to id:%d(fd:%d) %d bytes\r\n", this->m_id, this->m_fd, result);
	this->purgeWbuf(result);
      }
      return (result);
    }

    /**
     * @return -1: read next time;0: eof
     */
    int draw() {
      int draw = 0;
      int result = 0;
      static const int once = 1024000;
      char* buf = (char*)calloc(1, once);
      while( (result = read(this->m_io->fd, buf, once)) > 0 ) {
	printf("draw from id:%d(fd:%d) %d bytes\r\n", this->m_id, this->m_fd, result);
	this->pushRbuf(buf, result);
	
	draw += result;
	memset(buf, 0, once);
      }
      free(buf);
      printf("draw errno: %d\r\n", errno);
      if (result == -1) {
	if (errno == EAGAIN && draw == 0) { // the first time EAGAIN
	  draw = -1;
	}
      }
      return (draw);
    }

    int pushRbuf(const char* buf, const size_t len) {
      char* finalRbuf = (char*)malloc(this->m_rbuf_len+len);
      memcpy(finalRbuf, this->m_rbuf, this->m_rbuf_len);
      memcpy(finalRbuf+this->m_rbuf_len, buf, len);
      free(this->m_rbuf);
      this->m_rbuf = finalRbuf;
      this->m_rbuf_len = this->m_rbuf_len+len;
      return (0);
    }

    char* wbuf() const { return (this->m_wbuf); }
    size_t wbufLen() const { return (this->m_wbuf_len); }
    int purgeWbuf(const int bytes = -1) {
      if (bytes == -1) {
	free(this->m_wbuf);
	this->m_wbuf = NULL; this->m_wbuf_len = 0;
      } else {
	char* remainWbuf = (char*)malloc(this->m_wbuf_len-bytes);
	memcpy(remainWbuf, this->m_wbuf+bytes, this->m_wbuf_len-bytes);
	this->m_wbuf = remainWbuf;
	this->m_wbuf_len = this->m_wbuf_len-bytes;
      }
      return (0);
    }

    char* rbuf() const { return (this->m_rbuf); }
    size_t rbufLen() const { return (this->m_rbuf_len); }
    int purgeRbuf(const int bytes = -1) {
      if (bytes == -1) {
	free(this->m_rbuf);
	this->m_rbuf = NULL; this->m_rbuf_len = 0;
      } else {
	char* remainRbuf = (char*)malloc(this->m_rbuf_len-bytes);
	memcpy(remainRbuf, this->m_rbuf+bytes, this->m_rbuf_len-bytes);
	this->m_rbuf = remainRbuf;
	this->m_rbuf_len = this->m_rbuf_len-bytes;
      }
      return (0);
    }

    const struct sockaddr* addr() const { return (&this->m_addr); }

    int32_t id() { return (this->m_id); }

    int fd() { return (this->m_fd); }
};

#define PKG_HEADER_SIZE 10 // 4-payload length,4-external peer's id,2-cmd
