/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (c) 2010-2017 Phusion Holding B.V.
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
#include "cxx_supportlib/AppTypeDetector/CBindings.h"


typedef struct {
    /** Proxy state. */
    ngx_uint_t  status;
    ngx_uint_t  status_count;
    u_char     *status_start;
    u_char     *status_end;

    /** The application's 'public' directory. */
    ngx_str_t   public_dir;

    /** The application's base URI. Points to an empty string if none. */
    ngx_str_t   base_uri;

    /** Detected information about the app. */
    PsgAppTypeDetectorResult *detector_result;
} passenger_context_t;


ngx_int_t passenger_content_handler(ngx_http_request_t *r);


#endif /* _PASSENGER_NGINX_CONTENT_HANDLER_H_ */
