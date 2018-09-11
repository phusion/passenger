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

#include <nginx.h>
#include <ngx_http.h>
#include "ngx_http_passenger_module.h"
#include "ContentHandler.h"
#include "StaticContentHandler.h"
#include "Configuration.h"
#include "cxx_supportlib/Constants.h"
#include "cxx_supportlib/FileTools/PathManipCBindings.h"


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


static FileType
get_file_type(const u_char *filename, unsigned int throttle_rate) {
    struct stat buf;
    int ret;

    ret = pp_cached_file_stat_perform(pp_stat_cache,
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

static int
mapped_filename_equals(const u_char *filename, size_t filename_len, ngx_str_t *str)
{
    return (str->len == filename_len &&
            memcmp(str->data, filename, filename_len) == 0) ||
           (str->len == filename_len - 1 &&
            filename[filename_len - 1] == '/' &&
            memcmp(str->data, filename, filename_len - 1) == 0);
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


    /* Check whether `filename` is equal to public_dir.
     * `filename` may also be equal to public_dir + "/" so check for that as well.
     */
    if (mapped_filename_equals(filename, filename_len, public_dir)) {
        /* If the URI maps to the 'public' or the alias directory (i.e. the request is the
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

    } else if (filename[filename_len - 1] == '/') {
        /* if the filename ends with '/' check for filename + "index.html". */

        if (filename_len + sizeof("index.html") > page_cache_file->len) {
            /* Page cache filename doesn't fit in the buffer. */
            return 0;
        }

        end = ngx_copy(page_cache_file->data, filename, filename_len);
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

static void
cleanup_detector_result(void *data) {
    psg_app_type_detector_result_deinit((PsgAppTypeDetectorResult *) data);
}

static int
find_base_uri(ngx_http_request_t *r, const passenger_loc_conf_t *loc,
              ngx_str_t *found_base_uri)
{
    ngx_uint_t  i;
    ngx_str_t  *base_uris, *base_uri, *uri;

    if (loc->autogenerated.base_uris == NGX_CONF_UNSET_PTR) {
        return 0;
    } else {
        base_uris = (ngx_str_t *) loc->autogenerated.base_uris->elts;
        uri       = &r->uri;
        for (i = 0; i < loc->autogenerated.base_uris->nelts; i++) {
            base_uri = &base_uris[i];

            if (base_uri->len == 1 && base_uri->data[0] == '/') {
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
                *found_base_uri = *base_uri;
                return 1;
            }
        }
        return 0;
    }
}

static void
set_upstream_server_address(ngx_http_upstream_t *upstream, ngx_http_upstream_conf_t *upstream_config) {
    ngx_http_upstream_server_t *servers = upstream_config->upstream->servers->elts;
    ngx_addr_t                 *address = &servers[0].addrs[0];
    const char                 *core_address;
    unsigned int                core_address_len;
    struct sockaddr_un         *sockaddr;

    /* The Nginx API makes it extremely difficult to register an upstream server
     * address outside of the configuration loading phase. However we don't know
     * the Passenger core's request socket filename until we're done with loading
     * the configuration. So during configuration loading we register a placeholder
     * address for the upstream configuration, and while processing requests
     * we substitute the placeholder filename with the real Passenger core request
     * socket filename.
     */
    if (address->name.data == pp_placeholder_upstream_address.data) {
        sockaddr = (struct sockaddr_un *) address->sockaddr;
        core_address =
            psg_watchdog_launcher_get_core_address(psg_watchdog_launcher,
                                                   &core_address_len);
        core_address += sizeof("unix:") - 1;
        core_address_len -= sizeof("unix:") - 1;

        address->name.data = (u_char *) core_address;
        address->name.len  = core_address_len;
        strncpy(sockaddr->sun_path, core_address, sizeof(sockaddr->sun_path));
        sockaddr->sun_path[sizeof(sockaddr->sun_path) - 1] = '\0';
    }
}

/**
 * If the Passenger core socket cannot be connected to then we want Nginx to print
 * the proper socket filename in the error message. The socket filename is stored
 * in one of the upstream peer data structures. This name is initialized during
 * the first ngx_http_read_client_request_body() call so there's no way to fix the
 * name before the first request, which is why we do it after the fact.
 */
static void
fix_peer_address(ngx_http_request_t *r) {
    ngx_http_upstream_rr_peer_data_t *rrp;
    ngx_http_upstream_rr_peers_t     *peers;
    ngx_http_upstream_rr_peer_t      *peer;
    unsigned int                      peer_index;
    const char                       *core_address;
    unsigned int                      core_address_len;

    if (r->upstream->peer.get != ngx_http_upstream_get_round_robin_peer) {
        /* This function only supports the round-robin upstream method. */
        return;
    }

    rrp        = r->upstream->peer.data;
    peers      = rrp->peers;
    core_address =
        psg_watchdog_launcher_get_core_address(psg_watchdog_launcher,
                                               &core_address_len);

    while (peers != NULL) {
        if (peers->name) {
            if (peers->name->data == (u_char *) core_address) {
                /* Peer names already fixed. */
                return;
            }
            peers->name->data = (u_char *) core_address;
            peers->name->len  = core_address_len;
        }
        peer_index = 0;
        while (1) {
            peer = &peers->peer[peer_index];
            peer->name.data = (u_char *) core_address;
            peer->name.len  = core_address_len;
            if (peer->down) {
                peer_index++;
            } else {
                break;
            }
        }
        peers = peers->next;
    }
}


#if (NGX_HTTP_CACHE)

static ngx_int_t
create_key(ngx_http_request_t *r)
{
    ngx_str_t            *key;
    passenger_loc_conf_t *slcf;

    key = ngx_array_push(&r->cache->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_passenger_module);

    if (ngx_http_complex_value(r, &slcf->cache_key, key) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif

/**
 * Checks whether the given header is "Transfer-Encoding".
 * We do not pass Transfer-Encoding headers to the Passenger core because
 * Nginx always buffers the request body and always sets Content-Length
 * in the request headers.
 */
static int
header_is_transfer_encoding(ngx_str_t *key)
{
    return key->len == sizeof("transfer-encoding") - 1 &&
        ngx_tolower(key->data[0]) == (u_char) 't' &&
        ngx_tolower(key->data[sizeof("transfer-encoding") - 2]) == (u_char) 'g' &&
        ngx_strncasecmp(key->data + 1, (u_char *) "ransfer-encodin", sizeof("ransfer-encodin") - 1) == 0;
}

#define SET_NGX_STR(str, the_data) \
    do { \
        (str)->data = (u_char *) the_data; \
        (str)->len  = sizeof(the_data) - 1; \
    } while (0)

#define SET_NGX_STR_WITH_NULL(str, the_data) \
    do { \
        (str)->data = (u_char *) the_data; \
        (str)->len  = sizeof(the_data); \
    } while (0)

typedef struct {
    ngx_str_t     method; /* Includes trailing space */
    ngx_str_t     app_type;
    ngx_str_t     escaped_uri;
    ngx_str_t     content_length;
    ngx_str_t     core_password;
    ngx_str_t     remote_port;
} buffer_construction_state;

static ngx_int_t
prepare_request_buffer_construction(ngx_http_request_t *r, passenger_context_t *context,
    buffer_construction_state *state)
{
    unsigned int          len;
    ngx_uint_t            port;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif
    const PsgWrapperRegistryEntry *wrapper_registry_entry;

    switch (r->method) {
    case NGX_HTTP_GET:
        SET_NGX_STR(&state->method, "GET ");
        break;
    case NGX_HTTP_HEAD:
        SET_NGX_STR(&state->method, "HEAD ");
        break;
    case NGX_HTTP_POST:
        SET_NGX_STR(&state->method, "POST ");
        break;
    case NGX_HTTP_PUT:
        SET_NGX_STR(&state->method, "PUT ");
        break;
    case NGX_HTTP_DELETE:
        SET_NGX_STR(&state->method, "DELETE ");
        break;
    case NGX_HTTP_MKCOL:
        SET_NGX_STR(&state->method, "MKCOL ");
        break;
    case NGX_HTTP_COPY:
        SET_NGX_STR(&state->method, "COPY ");
        break;
    case NGX_HTTP_MOVE:
        SET_NGX_STR(&state->method, "MOVE ");
        break;
    case NGX_HTTP_OPTIONS:
        SET_NGX_STR(&state->method, "OPTIONS ");
        break;
    case NGX_HTTP_PROPFIND:
        SET_NGX_STR(&state->method, "PROPFIND ");
        break;
    case NGX_HTTP_PROPPATCH:
        SET_NGX_STR(&state->method, "PROPPATCH ");
        break;
    case NGX_HTTP_LOCK:
        SET_NGX_STR(&state->method, "LOCK ");
        break;
    case NGX_HTTP_UNLOCK:
        SET_NGX_STR(&state->method, "UNLOCK ");
        break;
    case NGX_HTTP_PATCH:
        SET_NGX_STR(&state->method, "PATCH ");
        break;
    case NGX_HTTP_TRACE:
        SET_NGX_STR(&state->method, "TRACE ");
        break;
    default:
        SET_NGX_STR(&state->method, "UNKNOWN ");
        break;
    }

    wrapper_registry_entry = psg_app_type_detector_result_get_wrapper_registry_entry(
        context->detector_result);
    state->app_type.data = (u_char *) psg_wrapper_registry_entry_get_language(wrapper_registry_entry,
        &state->app_type.len);

    /*
     * Nginx unescapes URI's before passing them to Phusion Passenger,
     * but backend processes expect the escaped version.
     * http://code.google.com/p/phusion-passenger/issues/detail?id=404
     *
     * Here we check whether Nginx has rewritten the URI or not. If not,
     * we can use the raw, unparsed URI as sent by the client.
     */
    if (r->valid_unparsed_uri && r->main) {
        state->escaped_uri = r->unparsed_uri;
        const char *pos = memchr((const char *) r->unparsed_uri.data, '?', r->unparsed_uri.len);
        if (pos != NULL) {
            state->escaped_uri.len = pos - (const char *) r->unparsed_uri.data;
        }
    } else {
        state->escaped_uri.len =
            2 * ngx_escape_uri(NULL, r->uri.data, r->uri.len, NGX_ESCAPE_URI)
            + r->uri.len;
        state->escaped_uri.data = ngx_pnalloc(r->pool, state->escaped_uri.len);
        if (state->escaped_uri.data == NULL) {
            return NGX_ERROR;
        }
        ngx_escape_uri(state->escaped_uri.data, r->uri.data, r->uri.len,
            NGX_ESCAPE_URI);
    }

    if (r->headers_in.chunked) {
        /* If the request body is chunked, then Nginx sets r->headers_in.content_length_n
         * but does not set r->headers_in.headers, so we add this header ourselves.
         */
        state->content_length.data = ngx_pnalloc(r->pool, sizeof("4294967295") - 1);
        state->content_length.len = ngx_snprintf(state->content_length.data,
            sizeof("4294967295") - 1, "%O", r->headers_in.content_length_n)
            - state->content_length.data;
    } // else: content_length not used

    state->core_password.data = (u_char *) psg_watchdog_launcher_get_core_password(
        psg_watchdog_launcher, &len);
    state->core_password.len  = len;

    switch (r->connection->sockaddr->sa_family) {
    #if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) r->connection->sockaddr;
        port = ntohs(sin6->sin6_port);
        break;
    #endif

    #if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
        port = 0;
        break;
    #endif

    default: /* AF_INET */
        sin = (struct sockaddr_in *) r->connection->sockaddr;
        port = ntohs(sin->sin_port);
        break;
    }

    state->remote_port.data = ngx_pnalloc(r->pool, sizeof("65535") - 1);
    if (state->remote_port.data == NULL) {
        return NGX_ERROR;
    }

    if (port > 0 && port < 65536) {
        state->remote_port.len = ngx_snprintf(state->remote_port.data,
            sizeof("65535") - 1, "%ui", port) - state->remote_port.data;
    } else {
        state->remote_port.len = 0;
    }

    return NGX_OK;
}

static ngx_uint_t
construct_request_buffer(ngx_http_request_t *r, passenger_loc_conf_t *slcf,
    passenger_context_t *context, buffer_construction_state *state, ngx_buf_t *b)
{
    #define PUSH_STATIC_STR(str) \
        do { \
            if (b != NULL) { \
                b->last = ngx_copy(b->last, (const u_char *) str, \
                    sizeof(str) - 1); \
            } \
            total_size += sizeof(str) - 1; \
        } while (0)

    ngx_uint_t       total_size = 0;
    ngx_uint_t       i;
    ngx_list_part_t *part;
    ngx_table_elt_t *header;
    size_t           len;
    ngx_str_t        public_dir_parent;
    ngx_str_t        public_dir_resolved;
    const char      *temp_path;
    ngx_http_script_len_code_pt lcode;
    ngx_http_script_code_pt     code;
    ngx_http_script_engine_t    e, le;

    if (b != NULL) {
        b->last = ngx_copy(b->last, state->method.data, state->method.len);
    }
    total_size += state->method.len;

    if (b != NULL) {
        b->last = ngx_copy(b->last, state->escaped_uri.data, state->escaped_uri.len);
    }
    total_size += state->escaped_uri.len;
    if (r->args.len > 0) {
        if (b != NULL) {
            b->last = ngx_copy(b->last, "?", 1);
            b->last = ngx_copy(b->last, r->args.data, r->args.len);
        }
        total_size += r->args.len + 1;
    }

    PUSH_STATIC_STR(" HTTP/1.1\r\nConnection: close\r\n");

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

        if (ngx_hash_find(&slcf->headers_set_hash, header[i].hash,
                          header[i].lowcase_key, header[i].key.len)
         || header_is_transfer_encoding(&header[i].key))
        {
            continue;
        }

        if (b != NULL) {
            b->last = ngx_copy(b->last, header[i].key.data, header[i].key.len);
            b->last = ngx_copy(b->last, ": ", 2);
            b->last = ngx_copy(b->last, header[i].value.data, header[i].value.len);
            b->last = ngx_copy(b->last, "\r\n", 2);
        }
        total_size += header[i].key.len + header[i].value.len + 4;
    }

    if (r->headers_in.chunked) {
        PUSH_STATIC_STR("Content-Length: ");
        if (b != NULL) {
            b->last = ngx_copy(b->last, state->content_length.data,
                state->content_length.len);
        }
        total_size += state->content_length.len;
        PUSH_STATIC_STR("\r\n");
    }

    if (slcf->headers_set_len) {
        ngx_memzero(&le, sizeof(ngx_http_script_engine_t));

        ngx_http_script_flush_no_cacheable_variables(r, slcf->flushes);

        le.ip = slcf->headers_set_len->elts;
        le.request = r;
        le.flushed = 1;

        while (*(uintptr_t *) le.ip) {
            while (*(uintptr_t *) le.ip) {
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
                total_size += lcode(&le);
            }
            le.ip += sizeof(uintptr_t);
        }

        if (b != NULL) {
            ngx_memzero(&e, sizeof(ngx_http_script_engine_t));

            e.ip = slcf->headers_set->elts;
            e.pos = b->last;
            e.request = r;
            e.flushed = 1;

            le.ip = slcf->headers_set_len->elts;

            while (*(uintptr_t *) le.ip) {
                lcode = *(ngx_http_script_len_code_pt *) le.ip;

                /* skip the header line name length */
                (void) lcode(&le);

                if (*(ngx_http_script_len_code_pt *) le.ip) {

                    for (len = 0; *(uintptr_t *) le.ip; len += lcode(&le)) {
                        lcode = *(ngx_http_script_len_code_pt *) le.ip;
                    }

                    e.skip = (len == sizeof("\r\n") - 1) ? 1 : 0;

                } else {
                    e.skip = 0;
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
    }

    if (b != NULL) {
        b->last = ngx_copy(b->last, "!~: ", sizeof("!~: ") - 1);
        b->last = ngx_copy(b->last, state->core_password.data,
            state->core_password.len);
        b->last = ngx_copy(b->last, "\r\n", sizeof("\r\n") - 1);
    }
    total_size += (sizeof("!~: \r\n") - 1) + state->core_password.len;

    PUSH_STATIC_STR("!~DOCUMENT_ROOT: ");
    if (b != NULL) {
        b->last = ngx_copy(b->last, context->public_dir.data,
            context->public_dir.len);
    }
    total_size += context->public_dir.len;
    PUSH_STATIC_STR("\r\n");

    if (context->base_uri.len > 0) {
        PUSH_STATIC_STR("!~SCRIPT_NAME: ");
        if (b != NULL) {
            b->last = ngx_copy(b->last, context->base_uri.data,
                context->base_uri.len);
        }
        total_size += context->base_uri.len;
        PUSH_STATIC_STR("\r\n");
    }

    PUSH_STATIC_STR("!~REMOTE_ADDR: ");
    if (b != NULL) {
        b->last = ngx_copy(b->last, r->connection->addr_text.data,
            r->connection->addr_text.len);
    }
    total_size += r->connection->addr_text.len;
    PUSH_STATIC_STR("\r\n");

    PUSH_STATIC_STR("!~REMOTE_PORT: ");
    if (b != NULL) {
        b->last = ngx_copy(b->last, state->remote_port.data,
            state->remote_port.len);
    }
    total_size += state->remote_port.len;
    PUSH_STATIC_STR("\r\n");

    if (r->headers_in.user.len > 0) {
        PUSH_STATIC_STR("!~REMOTE_USER: ");
        if (b != NULL) {
            b->last = ngx_copy(b->last, r->headers_in.user.data,
                r->headers_in.user.len);
        }
        total_size += r->headers_in.user.len;
        PUSH_STATIC_STR("\r\n");
    }

    if (slcf->autogenerated.app_group_name.data == NULL) {
        PUSH_STATIC_STR("!~PASSENGER_APP_GROUP_NAME: ");
        if (slcf->autogenerated.app_root.data == NULL) {
            if (context->base_uri.data == NULL) {
                /* If no passenger_base_uri applies, then the app
                 * group name is based on the parent directory of
                 * the document root.
                 */
                public_dir_parent.data = (u_char *) psg_extract_dir_name_static(
                    (const char *) context->public_dir.data,
                    context->public_dir.len,
                    &public_dir_parent.len);
            } else {
                /* If a passenger_base_uri applies, then the document
                 * root may be a symlink. We base the app group name
                 * on `extractDirName(resolveSymlink(public_dir))`.
                 */
                public_dir_resolved.data = (u_char *)
                    psg_resolve_symlink((const char *) context->public_dir.data,
                        context->public_dir.len, &public_dir_resolved.len);
                if (public_dir_resolved.data == NULL) {
                    /* Resolve or memory allocation error. Fallback to
                     * assuming that no passenger_base_uri applies.
                     */
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno,
                        "error resolving symlink %V",
                        &context->public_dir);
                    public_dir_parent.data = (u_char *) psg_extract_dir_name_static(
                        (const char *) context->public_dir.data,
                        context->public_dir.len,
                        &public_dir_parent.len);
                } else {
                    temp_path = psg_extract_dir_name_static(
                        (const char *) public_dir_resolved.data,
                        public_dir_resolved.len,
                        &public_dir_parent.len);
                    public_dir_parent.data = ngx_pnalloc(r->pool,
                        public_dir_parent.len);
                    memcpy(public_dir_parent.data, temp_path,
                        public_dir_parent.len);
                    free(public_dir_resolved.data);
                }
            }
            if (b != NULL) {
                b->last = ngx_copy(b->last, public_dir_parent.data,
                    public_dir_parent.len);
            }
            total_size += public_dir_parent.len;
        } else {
            if (b != NULL) {
                b->last = ngx_copy(b->last,
                    slcf->autogenerated.app_root.data,
                    slcf->autogenerated.app_root.len);
            }
            total_size += slcf->autogenerated.app_root.len;
        }
        if (slcf->autogenerated.environment.data != NULL) {
            if (b != NULL) {
                b->last = ngx_copy(b->last, " (", 2);
                b->last = ngx_copy(b->last, slcf->autogenerated.environment.data,
                    slcf->autogenerated.environment.len);
                b->last = ngx_copy(b->last, ")", 1);
            }
            total_size += (sizeof(" (") - 1) + slcf->autogenerated.environment.len + (sizeof(")") - 1);
        }
        PUSH_STATIC_STR("\r\n");
    }

    PUSH_STATIC_STR("!~PASSENGER_APP_TYPE: ");
    if (b != NULL) {
        b->last = ngx_copy(b->last, state->app_type.data,
            state->app_type.len);
    }
    total_size += state->app_type.len;
    PUSH_STATIC_STR("\r\n");

    if (b != NULL) {
        b->last = ngx_copy(b->last, slcf->options_cache.data, slcf->options_cache.len);
    }
    total_size += slcf->options_cache.len;

    if (slcf->env_vars_cache.data != NULL) {
        PUSH_STATIC_STR("!~PASSENGER_ENV_VARS: ");
        if (b != NULL) {
            b->last = ngx_copy(b->last, slcf->env_vars_cache.data, slcf->env_vars_cache.len);
        }
        total_size += slcf->env_vars_cache.len;
        PUSH_STATIC_STR("\r\n");
    }

    /* D = Dechunk response
     *     Prevent Nginx from rechunking the response.
     * C = Strip 100 Continue header
     * S = SSL
     */

    PUSH_STATIC_STR("!~FLAGS: DC");
    #if (NGX_HTTP_SSL)
        if (r->http_connection != NULL /* happens in sub-requests */
                && r->http_connection->ssl) {
            PUSH_STATIC_STR("S");
        }
    #endif
    PUSH_STATIC_STR("\r\n\r\n");

    return total_size;

    #undef PUSH_STATIC_STR
}

static ngx_int_t
create_request(ngx_http_request_t *r)
{
    passenger_loc_conf_t          *slcf;
    passenger_context_t           *context;
    buffer_construction_state      state;
    ngx_uint_t                     request_size;
    ngx_buf_t                     *b;
    ngx_chain_t                   *cl, *body;

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_passenger_module);
    context = ngx_http_get_module_ctx(r, ngx_http_passenger_module);
    if (context == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Construct and pass request headers */

    if (prepare_request_buffer_construction(r, context, &state) != NGX_OK) {
        return NGX_ERROR;
    }
    request_size = construct_request_buffer(r, slcf, context, &state, NULL);

    b = ngx_create_temp_buf(r->pool, request_size);
    if (b == NULL) {
        return NGX_ERROR;
    }
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    cl->buf = b;

    construct_request_buffer(r, slcf, context, &state, b);

    /* Pass request body */

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
    r->state = 0;

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
        return NGX_ERROR;
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
        return NGX_ERROR;
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
    ngx_str_t                      *status_line;
    ngx_int_t                       rc, status;
    ngx_table_elt_t                *h;
    ngx_http_upstream_t            *u;
    ngx_http_upstream_header_t     *hh;
    ngx_http_upstream_main_conf_t  *umcf;
    ngx_http_core_loc_conf_t       *clcf;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    for ( ;; ) {

        rc = ngx_http_parse_header_line(r, &r->upstream->buffer, 1);

        if (rc == NGX_OK) {

            /* a header line has been parsed successfully */

            h = ngx_list_push(&r->upstream->headers_in.headers);
            if (h == NULL) {
                return NGX_ERROR;
            }

            h->hash = r->header_hash;

            h->key.len = r->header_name_end - r->header_name_start;
            h->value.len = r->header_end - r->header_start;

            h->key.data = ngx_pnalloc(r->pool,
                                      h->key.len + 1 + h->value.len + 1
                                      + h->key.len);
            if (h->key.data == NULL) {
                return NGX_ERROR;
            }

            h->value.data = h->key.data + h->key.len + 1;
            h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;

            ngx_memcpy(h->key.data, r->header_name_start, h->key.len);
            h->key.data[h->key.len] = '\0';
            ngx_memcpy(h->value.data, r->header_start, h->value.len);
            h->value.data[h->value.len] = '\0';

            if (h->key.len == r->lowcase_index) {
                ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);

            } else {
                ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
            }

            hh = ngx_hash_find(&umcf->headers_in_hash, h->hash,
                               h->lowcase_key, h->key.len);

            if (hh && hh->handler(r, h, hh->offset) != NGX_OK) {
                return NGX_ERROR;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http scgi header: \"%V: %V\"", &h->key, &h->value);

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
                if (!passenger_main_conf.autogenerated.show_version_in_header) {
                    if (clcf->server_tokens) {
                        h->value.data = (u_char *) (NGINX_VER " + " PROGRAM_NAME);
                    } else {
                        h->value.data = (u_char *) ("nginx + " PROGRAM_NAME);
                    }
                } else {
                    if (clcf->server_tokens) {
                        h->value.data = (u_char *) (NGINX_VER " + " PROGRAM_NAME " " PASSENGER_VERSION);
                    } else {
                        h->value.data = (u_char *) ("nginx + " PROGRAM_NAME " " PASSENGER_VERSION);
                    }
                }
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

            /* Process "Status" header. */

            u = r->upstream;

            if (u->headers_in.status_n) {
                goto done;
            }

            if (u->headers_in.status) {
                status_line = &u->headers_in.status->value;

                status = ngx_atoi(status_line->data, 3);
                if (status == NGX_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "upstream sent invalid status \"%V\"",
                                  status_line);
                    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
                }

                u->headers_in.status_n = status;
                u->headers_in.status_line = *status_line;

            } else if (u->headers_in.location) {
                u->headers_in.status_n = 302;
                ngx_str_set(&u->headers_in.status_line,
                            "302 Moved Temporarily");

            } else {
                u->headers_in.status_n = 200;
                ngx_str_set(&u->headers_in.status_line, "200 OK");
            }

            if (u->state && u->state->status == 0) {
                u->state->status = u->headers_in.status_n;
            }

        done:

            /* Supported since Nginx 1.3.15. */
            #ifdef NGX_HTTP_SWITCHING_PROTOCOLS
                if (u->headers_in.status_n == NGX_HTTP_SWITCHING_PROTOCOLS
                    && r->headers_in.upgrade)
                {
                    u->upgrade = 1;
                }
            #endif

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
    size_t                 root_len, len;
    u_char                 page_cache_file_str[NGX_MAX_PATH + 1];
    ngx_str_t              page_cache_file;
    passenger_context_t   *context;
    void                  *detector_result_mem;
    ngx_pool_cleanup_t    *detector_result_cleanup;
    PP_Error               error;
    const PsgWrapperRegistryEntry *wrapper_registry_entry;

    if (passenger_main_conf.autogenerated.root_dir.len == 0) {
        return NGX_DECLINED;
    }

    slcf = ngx_http_get_module_loc_conf(r, ngx_http_passenger_module);

    /* Let the next content handler take care of this request if Phusion
     * Passenger is disabled for this URL.
     */
    if (!slcf->autogenerated.enabled) {
        return NGX_DECLINED;
    }

    /* Let the next content handler take care of this request if this URL
     * maps to an existing file.
     */
    path_last = ngx_http_map_uri_to_path(r, &path, &root_len, 0);
    if (path_last != NULL && file_exists(path.data, 0)) {
        return NGX_DECLINED;
    }

    /* Create a string containing the root path. This path already
     * contains a trailing slash.
     */
    end = ngx_copy(root_path_str, path.data, root_len);
    *end = '\0';
    root_path.data = root_path_str;
    root_path.len  = root_len;


    context = ngx_pcalloc(r->pool, sizeof(passenger_context_t));
    if (context == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_http_set_ctx(r, context, ngx_http_passenger_module);


    /* Find the base URI for this web application, if any. */
    if (find_base_uri(r, slcf, &base_uri)) {
        /* Store the found base URI into context->public_dir. We infer that
         * the 'public' directory of the web app equals document root + base URI.
         */
        if (slcf->autogenerated.document_root.data != NULL) {
            len = slcf->autogenerated.document_root.len + 1;
            context->public_dir.data = ngx_palloc(r->pool, sizeof(u_char) * len);
            end = ngx_copy(context->public_dir.data, slcf->autogenerated.document_root.data,
                           slcf->autogenerated.document_root.len);
        } else {
            len = root_path.len + base_uri.len + 1;
            context->public_dir.data = ngx_palloc(r->pool, sizeof(u_char) * len);
            end = ngx_copy(context->public_dir.data, root_path.data, root_path.len);
            end = ngx_copy(end, base_uri.data, base_uri.len);
        }
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
    if (context->public_dir.len == 0) {
        /* If the `root` directive is set to `/` then `public_dir`
         * becomes the empty string. We fix this into `/`.
         */
        context->public_dir.data = (u_char *) "/";
        context->public_dir.len = 1;
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

    detector_result_mem = ngx_palloc(r->pool,
        psg_app_type_detector_result_get_object_size());
    context->detector_result = psg_app_type_detector_result_init(detector_result_mem);
    detector_result_cleanup = ngx_pool_cleanup_add(r->pool, 0);
    detector_result_cleanup->handler = cleanup_detector_result;
    detector_result_cleanup->data = context->detector_result;

    if (slcf->autogenerated.app_type.data == NULL) {
        pp_error_init(&error);
        if (slcf->autogenerated.app_root.data == NULL) {
            psg_app_type_detector_check_document_root(
                psg_app_type_detector, context->detector_result,
                (const char *) context->public_dir.data, context->public_dir.len,
                context->base_uri.len != 0,
                &error);
        } else {
             psg_app_type_detector_check_app_root(
                psg_app_type_detector, context->detector_result,
                (const char *) slcf->autogenerated.app_root.data, slcf->autogenerated.app_root.len,
                &error);
        }
        if (psg_app_type_detector_result_is_null(context->detector_result)) {
            if (error.message == NULL) {
                return NGX_DECLINED;
            } else if (error.errnoCode == EACCES) {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                              "%s; This error means that the Nginx worker process (PID %d, "
                              "running as UID %d) does not have permission to access this file. "
                              "Please read this page to learn how to fix this problem: "
                              "https://www.phusionpassenger.com/library/admin/nginx/troubleshooting/?a=upon-accessing-the-web-app-nginx-reports-a-permission-denied-error; Extra info",
                              error.message,
                              (int) getpid(),
                              (int) getuid());
            } else {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log,
                              (error.errnoCode == PP_NO_ERRNO) ? 0 : error.errnoCode,
                              "%s",
                              error.message);
            }
            pp_error_destroy(&error);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    } else {
        wrapper_registry_entry = psg_wrapper_registry_lookup(psg_wrapper_registry,
            (const char *) slcf->autogenerated.app_type.data,
            slcf->autogenerated.app_type.len);
        if (psg_wrapper_registry_entry_is_null(wrapper_registry_entry)) {
            return NGX_DECLINED;
        } else {
            psg_app_type_detector_result_set_wrapper_registry_entry(
                context->detector_result, wrapper_registry_entry);
        }
    }


    /* Setup upstream stuff and prepare sending the request to the Passenger core. */

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    u = r->upstream;

    u->schema = pp_schema_string;
    u->output.tag = (ngx_buf_tag_t) &ngx_http_passenger_module;
    set_upstream_server_address(u, &slcf->upstream_config);
    u->conf = &slcf->upstream_config;

#if (NGX_HTTP_CACHE)
    u->create_key       = create_key;
#endif
    u->create_request   = create_request;
    u->reinit_request   = reinit_request;
    u->process_header   = process_status_line;
    u->abort_request    = abort_request;
    u->finalize_request = finalize_request;
    r->state = 0;

    u->buffering = slcf->upstream_config.buffering;

    u->pipe = ngx_pcalloc(r->pool, sizeof(ngx_event_pipe_t));
    if (u->pipe == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u->pipe->input_filter = ngx_event_pipe_copy_input_filter;
    u->pipe->input_ctx = r;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);

    fix_peer_address(r);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}
