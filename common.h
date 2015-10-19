//
// Created by shaw on 10/18/15.
//

#pragma once

#include <ev.h>

typedef struct SPeerCtx {
    int fd;
    struct sockaddr addr;
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