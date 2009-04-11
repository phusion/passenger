/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (C) 2008, 2009 Phusion
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

#include <nginx.h>
#include "ngx_http_passenger_module.h"
#include "ContentHandler.h"
#include "StaticContentHandler.h"
#include "Configuration.h"


#define NGX_HTTP_SCGI_PARSE_NO_HEADER  20

typedef enum {
    FT_ERROR,
    FT_FILE,
    FT_DIRECTORY,
    FT_OTHER
} FileType;


static ngx_int_t reinit_request(ngx_http_request_t *r);
static ngx_int_t process_status_line(ngx_http_request_t *r);
static ngx_int_t parse_status_line(ngx_http_request_t *r,
    passenger_context_t *context);
static ngx_int_t process_header(ngx_http_request_t *r);
static void abort_request(ngx_http_request_t *r);
static void finalize_request(ngx_http_request_t *r, ngx_int_t rc);


static void
uint_to_str(ngx_uint_t i, u_char *str, ngx_uint_t size) {
    ngx_memzero(str, size);
    ngx_snprintf(str, size, "%ui", i);
}

static FileType
get_file_type(const u_char *filename, unsigned int throttle_rate) {
    struct stat buf;
    int ret;
    
    ret = cached_multi_file_stat_perform(passenger_stat_cache,
                                         (const char *) filename,
                                         &buf,
                                         throttle_rate);
    if (ret == 0) {
        if (S_ISREG(buf.st_mode)) {
            return FT_FILE;
        } else if (S_ISDIR(buf.st_mode)) {
            return FT_DIRECTORY;
        } else {
            return FT_OTHER;
        }
    } else {
        return FT_ERROR;
    }
}

static int
file_exists(const u_char *filename, unsigned int throttle_rate) {
    return get_file_type(filename, throttle_rate) == FT_FILE;
}

static passenger_app_type_t
detect_application_type(const ngx_str_t *public_dir) {
    u_char filename[NGX_MAX_PATH];
    
    ngx_memzero(filename, sizeof(filename));
    ngx_snprintf(filename, sizeof(filename), "%s/%s",
                 public_dir->data, "../config/environment.rb");
    if (file_exists(filename, 1)) {
        return AP_RAILS;
    }
    
    ngx_memzero(filename, sizeof(filename));
    ngx_snprintf(filename, sizeof(filename), "%s/%s",
                 public_dir->data, "../config.ru");
    if (file_exists(filename, 1)) {
        return AP_RACK;
    }
    
    ngx_memzero(filename, sizeof(filename));
    ngx_snprintf(filename, sizeof(filename), "%s/%s",
                 public_dir->data, "../config/passenger_wsgi.py");
    if (file_exists(filename, 1)) {
        return AP_WSGI;
    }
    
    return AP_NONE;
}

/**
 * Maps the URI for the given request to a page cache file, if possible.
 *
 * @return Whether the URI has been successfully mapped to a page cache file.
 * @param r The corresponding request.
 * @param public_dir The web application's 'public' directory.
 * @param filename The filename that the URI normally maps to.
 * @param filename_len The length of the <tt>filename</tt> string.
 * @param root The size of the root path in <tt>filename</tt>.
 * @param page_cache_file If mapping was successful, then the page cache
 *                        file's filename will be stored in here.
 *                        <tt>page_cache_file.data</tt> must already point to
 *                        a buffer, and <tt>page_cache_file.len</tt> must be set
 *                        to the size of this buffer, including terminating NUL.
 */
static int
map_uri_to_page_cache_file(ngx_http_request_t *r, ngx_str_t *public_dir,
                           const u_char *filename, size_t filename_len,
                           ngx_str_t *page_cache_file)
{
    u_char *end;
    
    if ((r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) || filename_len == 0) {
        return 0;
    }
    
    /* From this point on we know that filename is not an empty string. */
    
    /* Check whether filename is equal to public_dir. filename may also be equal to
     * public_dir + "/" so check for that as well.
     */
    if ((public_dir->len == filename_len && memcmp(public_dir->data, filename,
                                                   filename_len) == 0) ||
        (public_dir->len == filename_len - 1 &&
         filename[filename_len - 1] == '/' &&
         memcmp(public_dir->data, filename, filename_len - 1) == 0)
       ) {
        /* If the URI maps to the 'public' directory (i.e. the request is the
         * base URI) then index.html is the page cache file.
         */
        
        if (filename_len + sizeof("/index.html") > page_cache_file->len) {
            /* Page cache filename doesn't fit in the buffer. */
            return 0;
        }
        
        end = ngx_copy(page_cache_file->data, filename, filename_len);
        if (filename[filename_len - 1] != '/') {
            end = ngx_copy(end, "/", 1);
        }
        end = ngx_copy(end, "index.html", sizeof("index.html"));
        
    } else {
        /* Otherwise, the page cache file is just filename + ".html". */
        
        if (filename_len + sizeof(".html") > page_cache_file->len) {
            /* Page cache filename doesn't fit in the buffer. */
            return 0;
        }
        
        end = ngx_copy(page_cache_file->data, filename, filename_len);
        end = ngx_copy(end, ".html", sizeof(".html"));
    }
    
    if (file_exists(page_cache_file->data, 0)) {
        page_cache_file->len = end - page_cache_file->data - 1;
        return 1;
    } else {
        return 0;
    }
}

static int
find_base_uri(ngx_http_request_t *r, const passenger_loc_conf_t *loc,
              ngx_str_t *found_base_uri)
{
    ngx_uint_t  i;
    ngx_str_t  *base_uris, *base_uri, *uri;
    
    if (loc->base_uris == NGX_CONF_UNSET_PTR) {
        return 0;
    } else {
        base_uris = (ngx_str_t *) loc->base_uris->elts;
        for (i = 0; i < loc->base_uris->nelts; i++) {
            base_uri = &base_uris[i];
            uri      = &r->uri;
            
            if (uri->len == 1 && uri->data[0] == '/') {
                /* Ignore 'passenger_base_uri /' options. Users usually
                 * specify this out of ignorance.
                 */
                continue;
            }
            
            if ((    uri->len == base_uri->len
                  && ngx_strncmp(uri->data, base_uri->data, uri->len) == 0 )
             || (    uri->len >  base_uri->len
                  && ngx_strncmp(uri->data, base_uri->data, base_uri->len) == 0
                  && uri->data[base_uri->len] == (u_char) '/' )) {
                *found_base_uri = base_uris[i];
                return 1;
            }
        }
        return 0;
    }
}


static ngx_int_t
create_request(ngx_http_request_t *r)
{
    u_char                         ch;
    u_char                         buf[sizeof("4294967296")];
    size_t                         len, size, key_len, val_len, content_length;
    const u_char                  *app_type_string;
    size_t                         app_type_string_len;
    u_char                         framework_spawner_idle_time_string[12];
    u_char                         app_spawner_idle_time_string[12];
    u_char                        *end;
    ngx_uint_t                     i, n;
    ngx_buf_t                     *b;
    ngx_chain_t                   *cl, *body;
    ngx_list_part_t               *part;
    ngx_table_elt_t               *header;
    ngx_http_script_code_pt        code;
    ngx_http_script_engine_t       e, le;
    passenger_loc_conf_t          *slcf;
    passenger_main_conf_t         *main_conf;
    passenger_context_t           *context;
    ngx_http_script_len_code_pt    lcode;
    
    slcf = ngx_http_get_module_loc_conf(r, ngx_http_passenger_module);
    main_conf = &passenger_main_conf;
    context = ngx_http_get_module_ctx(r, ngx_http_passenger_module);
    if (context == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    switch (context->app_type) {
    case AP_RAILS:
        app_type_string = (const u_char *) "rails";
        app_type_string_len = sizeof("rails");
        break;
    case AP_RACK:
        app_type_string = (const u_char *) "rack";
        app_type_string_len = sizeof("rack");
        break;
    case AP_WSGI:
        app_type_string = (const u_char *) "wsgi";
        app_type_string_len = sizeof("wsgi");
        break;
    default:
        app_type_string = (const u_char *) "rails";
        app_type_string_len = sizeof("rails");
        break;
    }
    
    
    /**************************************************
     * Determine the request header length.
     **************************************************/
    
    /* Length of the Content-Length header. */
    if (r->headers_in.content_length_n < 0) {
        content_length = 0;
    } else {
        content_length = r->headers_in.content_length_n;
    }
    uint_to_str(content_length, buf, sizeof(buf));
    /* +1 for trailing null */
    len = sizeof("CONTENT_LENGTH") + ngx_strlen(buf) + 1;
    
    /* DOCUMENT_ROOT, PATH_INFO and base URI */
    len += sizeof("DOCUMENT_ROOT") + context->public_dir.len + 1;
    len += sizeof("PATH_INFO") + context->public_dir.len + 1;
    if (context->base_uri.len > 0) {
        len += sizeof("RAILS_RELATIVE_URL_ROOT") +
               context->base_uri.len + 1;
    }
    
    /* Various other HTTP headers. */
    if (r->headers_in.content_type != NULL
     && r->headers_in.content_type->value.len > 0) {
        len += sizeof("CONTENT_TYPE") + r->headers_in.content_type->value.len + 1;
    }
    
    
    /* Lengths of Passenger application pool options. */
    if (slcf->use_global_queue) {
        len += sizeof("PASSENGER_USE_GLOBAL_QUEUE") + sizeof("true");
    } else {
        len += sizeof("PASSENGER_USE_GLOBAL_QUEUE") + sizeof("false");
    }
    len += sizeof("PASSENGER_ENVIRONMENT") + slcf->environment.len + 1;
    len += sizeof("PASSENGER_SPAWN_METHOD") + slcf->spawn_method.len + 1;
    len += sizeof("PASSENGER_APP_TYPE") + app_type_string_len;
    
    end = ngx_snprintf(framework_spawner_idle_time_string,
                       sizeof(framework_spawner_idle_time_string) - 1,
                       "%d", slcf->framework_spawner_idle_time);
    *end = '\0';
    len += sizeof("PASSENGER_FRAMEWORK_SPAWNER_IDLE_TIME") +
           ngx_strlen(framework_spawner_idle_time_string) + 1;
    
    end = ngx_snprintf(app_spawner_idle_time_string,
                       sizeof(app_spawner_idle_time_string) - 1,
                       "%d", slcf->app_spawner_idle_time);
    *end = '\0';
    len += sizeof("PASSENGER_APP_SPAWNER_IDLE_TIME") +
           ngx_strlen(app_spawner_idle_time_string) + 1;

    /* Lengths of various CGI variables. */
    if (slcf->vars_len) {
        ngx_memzero(&le, sizeof(ngx_http_script_engine_t));

        ngx_http_script_flush_no_cacheable_variables(r, slcf->flushes);
        le.flushed = 1;

        le.ip = slcf->vars_len->elts;
        le.request = r;

        while (*(uintptr_t *) le.ip) {

            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            key_len = lcode(&le);

            for (val_len = 0; *(uintptr_t *) le.ip; val_len += lcode(&le)) {
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
            }
            le.ip += sizeof(uintptr_t);

            len += key_len + val_len;
        }
    }

    /* Lengths of HTTP headers. */
    if (slcf->upstream.pass_request_headers) {

        part = &r->headers_in.headers.part;
        header = part->elts;

        for (i = 0; /* void */; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next;
                header = part->elts;
                i = 0;
            }

            len += sizeof("HTTP_") - 1 + header[i].key.len + 1
                + header[i].value.len + 1;
        }
    }


    /**************************************************
     * Build the request header data.
     **************************************************/
    
    size = passenger_helper_server_password.len +
        /* netstring length + ":" + trailing "," */
        /* note: 10 == sizeof("4294967296") - 1 */
        len + 10 + 1 + 1;

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    
    /* Build SCGI header netstring length part. */
    b->last = ngx_copy(b->last, passenger_helper_server_password.data,
                       passenger_helper_server_password.len);

    b->last = ngx_snprintf(b->last, 10, "%ui", len);
    *b->last++ = (u_char) ':';

    /* Build CONTENT_LENGTH header. This must always be sent, even if 0. */
    b->last = ngx_copy(b->last, "CONTENT_LENGTH",
                       sizeof("CONTENT_LENGTH"));

    b->last = ngx_snprintf(b->last, 10, "%ui", content_length);
    *b->last++ = (u_char) 0;
    
    /* Build DOCUMENT_ROOT and base URI. */
    b->last = ngx_copy(b->last, "DOCUMENT_ROOT", sizeof("DOCUMENT_ROOT"));
    b->last = ngx_copy(b->last, context->public_dir.data,
                       context->public_dir.len + 1);
    
    b->last = ngx_copy(b->last, "PATH_INFO", sizeof("PATH_INFO"));
    b->last = ngx_copy(b->last, context->public_dir.data,
                       context->public_dir.len + 1);
    
    if (context->base_uri.len > 0) {
        b->last = ngx_copy(b->last, "RAILS_RELATIVE_URL_ROOT",
                           sizeof("RAILS_RELATIVE_URL_ROOT"));
        b->last = ngx_copy(b->last, context->base_uri.data,
                           context->base_uri.len + 1);
    }
    
    /* Various other HTTP headers. */
    if (r->headers_in.content_type != NULL
     && r->headers_in.content_type->value.len > 0) {
        b->last = ngx_copy(b->last, "CONTENT_TYPE", sizeof("CONTENT_TYPE"));
        b->last = ngx_copy(b->last, r->headers_in.content_type->value.data,
                           r->headers_in.content_type->value.len + 1);
    }
    

    /* Build Passenger application pool option headers. */
    b->last = ngx_copy(b->last, "PASSENGER_USE_GLOBAL_QUEUE",
                       sizeof("PASSENGER_USE_GLOBAL_QUEUE"));
    if (slcf->use_global_queue) {
        b->last = ngx_copy(b->last, "true", sizeof("true"));
    } else {
        b->last = ngx_copy(b->last, "false", sizeof("false"));
    }
    
    b->last = ngx_copy(b->last, "PASSENGER_ENVIRONMENT",
                       sizeof("PASSENGER_ENVIRONMENT"));
    b->last = ngx_copy(b->last, slcf->environment.data,
                       slcf->environment.len + 1);

    b->last = ngx_copy(b->last, "PASSENGER_SPAWN_METHOD",
                       sizeof("PASSENGER_SPAWN_METHOD"));
    b->last = ngx_copy(b->last, slcf->spawn_method.data,
                       slcf->spawn_method.len + 1);

    b->last = ngx_copy(b->last, "PASSENGER_APP_TYPE",
                       sizeof("PASSENGER_APP_TYPE"));
    b->last = ngx_copy(b->last, app_type_string, app_type_string_len);

    b->last = ngx_copy(b->last, "PASSENGER_FRAMEWORK_SPAWNER_IDLE_TIME",
                       sizeof("PASSENGER_FRAMEWORK_SPAWNER_IDLE_TIME"));
    b->last = ngx_copy(b->last, framework_spawner_idle_time_string,
                       ngx_strlen(framework_spawner_idle_time_string) + 1);

    b->last = ngx_copy(b->last, "PASSENGER_APP_SPAWNER_IDLE_TIME",
                       sizeof("PASSENGER_APP_SPAWNER_IDLE_TIME"));
    b->last = ngx_copy(b->last, app_spawner_idle_time_string,
                       ngx_strlen(app_spawner_idle_time_string) + 1);

    if (slcf->vars_len) {
        ngx_memzero(&e, sizeof(ngx_http_script_engine_t));

        e.ip = slcf->vars->elts;
        e.pos = b->last;
        e.request = r;
        e.flushed = 1;

        le.ip = slcf->vars_len->elts;

        while (*(uintptr_t *) le.ip) {

            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            (void) lcode(&le);

            for (val_len = 0; *(uintptr_t *) le.ip; val_len += lcode(&le)) {
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
            }
            le.ip += sizeof(uintptr_t);

            while (*(uintptr_t *) e.ip) {
                code = *(ngx_http_script_code_pt *) e.ip;
                code((ngx_http_script_engine_t *) &e);
            }
            e.ip += sizeof(uintptr_t);
        }

        b->last = e.pos;
    }


    if (slcf->upstream.pass_request_headers) {

        part = &r->headers_in.headers.part;
        header = part->elts;

        for (i = 0; /* void */; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next;
                header = part->elts;
                i = 0;
            }

            b->last = ngx_cpymem(b->last, "HTTP_", sizeof("HTTP_") - 1);

            for (n = 0; n < header[i].key.len; n++) {
                ch = header[i].key.data[n];

                if (ch >= 'a' && ch <= 'z') {
                    ch &= ~0x20;

                } else if (ch == '-') {
                    ch = '_';
                }

                *b->last++ = ch;
            }

            *b->last++ = (u_char) 0;

            b->last = ngx_copy(b->last, header[i].value.data,
                               header[i].value.len);
            *b->last++ = (u_char) 0;
         }
    }


    *b->last++ = (u_char) ',';

    if (slcf->upstream.pass_request_body) {

        body = r->upstream->request_bufs;
        r->upstream->request_bufs = cl;

        while (body) {
            b = ngx_alloc_buf(r->pool);
            if (b == NULL) {
                return NGX_ERROR;
            }

            ngx_memcpy(b, body->buf, sizeof(ngx_buf_t));

            cl->next = ngx_alloc_chain_link(r->pool);
            if (cl->next == NULL) {
                return NGX_ERROR;
            }

            cl = cl->next;
            cl->buf = b;

            body = body->next;
        }

        b->flush = 1;

    } else {
        r->upstream->request_bufs = cl;
    }


    cl->next = NULL;

    return NGX_OK;
}


static ngx_int_t
reinit_request(ngx_http_request_t *r)
{
    passenger_context_t  *context;

    context = ngx_http_get_module_ctx(r, ngx_http_passenger_module);

    if (context == NULL) {
        return NGX_OK;
    }

    context->status = 0;
    context->status_count = 0;
    context->status_start = NULL;
    context->status_end = NULL;

    r->upstream->process_header = process_status_line;

    return NGX_OK;
}


static ngx_int_t
process_status_line(ngx_http_request_t *r)
{
    ngx_int_t             rc;
    ngx_http_upstream_t  *u;
    passenger_context_t  *context;

    context = ngx_http_get_module_ctx(r, ngx_http_passenger_module);

    if (context == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = parse_status_line(r, context);

    if (rc == NGX_AGAIN) {
        return rc;
    }

    u = r->upstream;

    if (rc == NGX_HTTP_SCGI_PARSE_NO_HEADER) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent no valid HTTP/1.0 header");

#if 0
        if (u->accel) {
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }
#endif

        r->http_version = NGX_HTTP_VERSION_9;
        u->headers_in.status_n = NGX_HTTP_OK;
        u->state->status = NGX_HTTP_OK;

        return NGX_OK;
    }

    u->headers_in.status_n = context->status;
    u->state->status = context->status;

    u->headers_in.status_line.len = context->status_end - context->status_start;
    u->headers_in.status_line.data = ngx_palloc(r->pool,
                                                u->headers_in.status_line.len);
    if (u->headers_in.status_line.data == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_memcpy(u->headers_in.status_line.data, context->status_start,
               u->headers_in.status_line.len);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http scgi status %ui \"%V\"",
                   u->headers_in.status_n, &u->headers_in.status_line);

    u->process_header = process_header;

    return process_header(r);
}


static ngx_int_t
parse_status_line(ngx_http_request_t *r, passenger_context_t *context)
{
    u_char                ch;
    u_char               *pos;
    ngx_http_upstream_t  *u;
    enum  {
        sw_start = 0,
        sw_H,
        sw_HT,
        sw_HTT,
        sw_HTTP,
        sw_first_major_digit,
        sw_major_digit,
        sw_first_minor_digit,
        sw_minor_digit,
        sw_status,
        sw_space_after_status,
        sw_status_text,
        sw_almost_done
    } state;

    u = r->upstream;

    state = r->state;

    for (pos = u->buffer.pos; pos < u->buffer.last; pos++) {
        ch = *pos;

        switch (state) {

        /* "HTTP/" */
        case sw_start:
            switch (ch) {
            case 'H':
                state = sw_H;
                break;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
            break;

        case sw_H:
            switch (ch) {
            case 'T':
                state = sw_HT;
                break;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
            break;

        case sw_HT:
            switch (ch) {
            case 'T':
                state = sw_HTT;
                break;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
            break;

        case sw_HTT:
            switch (ch) {
            case 'P':
                state = sw_HTTP;
                break;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
            break;

        case sw_HTTP:
            switch (ch) {
            case '/':
                state = sw_first_major_digit;
                break;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
            break;

        /* the first digit of major HTTP version */
        case sw_first_major_digit:
            if (ch < '1' || ch > '9') {
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }

            state = sw_major_digit;
            break;

        /* the major HTTP version or dot */
        case sw_major_digit:
            if (ch == '.') {
                state = sw_first_minor_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }

            break;

        /* the first digit of minor HTTP version */
        case sw_first_minor_digit:
            if (ch < '0' || ch > '9') {
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }

            state = sw_minor_digit;
            break;

        /* the minor HTTP version or the end of the request line */
        case sw_minor_digit:
            if (ch == ' ') {
                state = sw_status;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }

            break;

        /* HTTP status code */
        case sw_status:
            if (ch == ' ') {
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }

            context->status = context->status * 10 + ch - '0';

            if (++context->status_count == 3) {
                state = sw_space_after_status;
                context->status_start = pos - 2;
            }

            break;

         /* space or end of line */
         case sw_space_after_status:
            switch (ch) {
            case ' ':
                state = sw_status_text;
                break;
            case '.':                    /* IIS may send 403.1, 403.2, etc */
                state = sw_status_text;
                break;
            case CR:
                state = sw_almost_done;
                break;
            case LF:
                goto done;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
            break;

        /* any text until end of line */
        case sw_status_text:
            switch (ch) {
            case CR:
                state = sw_almost_done;

                break;
            case LF:
                goto done;
            }
            break;

        /* end of status line */
        case sw_almost_done:
            context->status_end = pos - 1;
            switch (ch) {
            case LF:
                goto done;
            default:
                return NGX_HTTP_SCGI_PARSE_NO_HEADER;
            }
        }
    }

    u->buffer.pos = pos;
    r->state = state;

    return NGX_AGAIN;

done:

    u->buffer.pos = pos + 1;

    if (context->status_end == NULL) {
        context->status_end = pos;
    }

    r->state = sw_start;

    return NGX_OK;
}


static ngx_int_t
process_header(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_uint_t                      i;
    ngx_table_elt_t                *h;
    ngx_http_upstream_header_t     *hh;
    ngx_http_upstream_main_conf_t  *umcf;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    for ( ;;  ) {

        #if NGINX_VERSION_NUM >= 7000
            rc = ngx_http_parse_header_line(r, &r->upstream->buffer, 1);
        #else
            rc = ngx_http_parse_header_line(r, &r->upstream->buffer);
        #endif

        if (rc == NGX_OK) {

            /* a header line has been parsed successfully */

            h = ngx_list_push(&r->upstream->headers_in.headers);
            if (h == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            h->hash = r->header_hash;

            h->key.len = r->header_name_end - r->header_name_start;
            h->value.len = r->header_end - r->header_start;

            h->key.data = ngx_palloc(r->pool,
                               h->key.len + 1 + h->value.len + 1 + h->key.len);
            if (h->key.data == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            h->value.data = h->key.data + h->key.len + 1;
            h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;

            ngx_cpystrn(h->key.data, r->header_name_start, h->key.len + 1);
            ngx_cpystrn(h->value.data, r->header_start, h->value.len + 1);

            if (h->key.len == r->lowcase_index) {
                ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);

            } else {
                for (i = 0; i < h->key.len; i++) {
                    h->lowcase_key[i] = ngx_tolower(h->key.data[i]);
                }
            }

            hh = ngx_hash_find(&umcf->headers_in_hash, h->hash,
                               h->lowcase_key, h->key.len);

            if (hh && hh->handler(r, h, hh->offset) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http scgi header: \"%V: %V\"",
                           &h->key, &h->value);

            continue;
        }

        if (rc == NGX_HTTP_PARSE_HEADER_DONE) {

            /* a whole header has been parsed successfully */

            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http scgi header done");

            /*
             * if no "Server" and "Date" in header line,
             * then add the default headers
             */

            if (r->upstream->headers_in.server == NULL) {
                h = ngx_list_push(&r->upstream->headers_in.headers);
                if (h == NULL) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                h->hash = ngx_hash(ngx_hash(ngx_hash(ngx_hash(
                                    ngx_hash('s', 'e'), 'r'), 'v'), 'e'), 'r');

                h->key.len = sizeof("Server") - 1;
                h->key.data = (u_char *) "Server";
                h->value.data = (u_char *) (NGINX_VER " + Phusion Passenger (mod_rails/mod_rack)");
                h->value.len = ngx_strlen(h->value.data);
                h->lowcase_key = (u_char *) "server";
            }

            if (r->upstream->headers_in.date == NULL) {
                h = ngx_list_push(&r->upstream->headers_in.headers);
                if (h == NULL) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                h->hash = ngx_hash(ngx_hash(ngx_hash('d', 'a'), 't'), 'e');

                h->key.len = sizeof("Date") - 1;
                h->key.data = (u_char *) "Date";
                h->value.len = 0;
                h->value.data = NULL;
                h->lowcase_key = (u_char *) "date";
            }

            return NGX_OK;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        /* there was error while a header line parsing */

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent invalid header");

        return NGX_HTTP_UPSTREAM_INVALID_HEADER;
    }
}


static void
abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort Passenger request");
}


static void
finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize Passenger request");
}


ngx_int_t
passenger_content_handler(ngx_http_request_t *r)
{
    ngx_int_t              rc;
    ngx_http_upstream_t   *u;
    passenger_loc_conf_t  *slcf;
    ngx_str_t              path, base_uri;
    u_char                *path_last, *end;
    u_char                 root_path_str[NGX_MAX_PATH + 1];
    ngx_str_t              root_path;
    size_t                 root, len;
    u_char                 page_cache_file_str[NGX_MAX_PATH + 1];
    ngx_str_t              page_cache_file;
    passenger_context_t   *context;

    if (r->subrequest_in_memory) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "ngx_http_passenger_module does not support "
                      "subrequest in memory");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_passenger_module);

    /* Let the next content handler take care of this request if Phusion
     * Passenger is disabled for this URL.
     */
    if (!slcf->enabled) {
        return NGX_DECLINED;
    }
    
    /* Let the next content handler take care of this request if this URL
     * maps to an existing file.
     */
    path_last = ngx_http_map_uri_to_path(r, &path, &root, 0);
    if (path_last != NULL && file_exists(path.data, 0)) {
        return NGX_DECLINED;
    }
    
    /* Create a string containing the root path. This path already
     * contains a trailing slash.
     */
    end = ngx_copy(root_path_str, path.data, root);
    *end = '\0';
    root_path.data = root_path_str;
    root_path.len  = root;
    
    
    context = ngx_pcalloc(r->pool, sizeof(passenger_context_t));
    if (context == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_http_set_ctx(r, context, ngx_http_passenger_module);
    
    
    /* Find the base URI for this web application, if any. */
    if (find_base_uri(r, slcf, &base_uri)) {
        /* Store the found base URI in context->public_dir. We infer that the 'public'
         * directory of the web application is document root + base URI.
         */
        len = root_path.len + base_uri.len + 1;
        context->public_dir.data = ngx_palloc(r->pool, sizeof(u_char) * len);
        end = ngx_copy(context->public_dir.data, root_path.data, root_path.len);
        end = ngx_copy(end, base_uri.data, base_uri.len);
        *end = '\0';
        context->public_dir.len = len - 1;
        context->base_uri = base_uri;
    } else {
        /* No base URI directives are applicable for this request. So assume that
         * the web application's public directory is the document root.
         * context->base_uri is now a NULL string.
         */
        len = sizeof(u_char *) * (root_path.len + 1);
        context->public_dir.data = ngx_palloc(r->pool, len);
        end = ngx_copy(context->public_dir.data, root_path.data,
                       root_path.len);
        *end = '\0';
        context->public_dir.len  = root_path.len;
    }
    
    /* If there's a corresponding page cache file for this URL, then serve that
     * file instead.
     */
    page_cache_file.data = page_cache_file_str;
    page_cache_file.len  = sizeof(page_cache_file_str);
    if (map_uri_to_page_cache_file(r, &context->public_dir, path.data,
                                   path_last - path.data, &page_cache_file)) {
        return passenger_static_content_handler(r, &page_cache_file);
    }
    
    context->app_type = detect_application_type(&context->public_dir);
    if (context->app_type == AP_NONE) {
        return NGX_DECLINED;
    }
    
    
    /* Setup upstream stuff and prepare sending the request to the backend. */
    
    u = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_t));
    if (u == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
#if NGINX_VERSION_NUM >= 7000
    u->schema = passenger_schema_string;
#endif

    u->peer.log = r->connection->log;
    u->peer.log_error = NGX_ERROR_ERR;
#if (NGX_THREADS)
    u->peer.lock = &r->connection->lock;
#endif

    u->output.tag = (ngx_buf_tag_t) &ngx_http_passenger_module;

    u->conf = &slcf->upstream;

    u->create_request   = create_request;
    u->reinit_request   = reinit_request;
    u->process_header   = process_status_line;
    u->abort_request    = abort_request;
    u->finalize_request = finalize_request;

    u->buffering = 1;

    u->pipe = ngx_pcalloc(r->pool, sizeof(ngx_event_pipe_t));
    if (u->pipe == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u->pipe->input_filter = ngx_event_pipe_copy_input_filter;

    r->upstream = u;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}

