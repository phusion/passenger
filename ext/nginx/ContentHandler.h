/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (C) 2008 Phusion
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PASSENGER_NGINX_CONTENT_HANDLER_H_
#define _PASSENGER_NGINX_CONTENT_HANDLER_H_

#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_uint_t                     status;
    ngx_uint_t                     status_count;
    u_char                        *status_start;
    u_char                        *status_end;
} ngx_http_scgi_ctx_t;


#define NGX_HTTP_SCGI_PARSE_NO_HEADER  20


ngx_int_t ngx_http_passenger_handler(ngx_http_request_t *r);


static ngx_int_t ngx_http_scgi_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_scgi_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_scgi_process_status_line(ngx_http_request_t *r);
static ngx_int_t ngx_http_scgi_parse_status_line(ngx_http_request_t *r,
    ngx_http_scgi_ctx_t *p);
static ngx_int_t ngx_http_scgi_process_header(ngx_http_request_t *r);
static void ngx_http_scgi_abort_request(ngx_http_request_t *r);
static void ngx_http_scgi_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

#endif /* _PASSENGER_NGINX_CONTENT_HANDLER_H_ */

