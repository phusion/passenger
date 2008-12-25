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

#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "ngx_http_passenger_module.h"
#include "Configuration.h"
#include "ContentHandler.h"


#define HELPER_SERVER_MAX_SHUTDOWN_TIME 5
#define HELPER_SERVER_PASSWORD_SIZE     64


static ngx_str_t  ngx_http_scgi_script_name = ngx_string("scgi_script_name");
static pid_t      helper_server_pid;
static int        helper_server_admin_pipe;
static u_char     helper_server_password_data[HELPER_SERVER_PASSWORD_SIZE];
ngx_str_t         ngx_http_passenger_helper_server_password;


static ngx_int_t
register_content_handler(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_passenger_handler;
    
    return NGX_OK;
}

static void
ignore_sigpipe()
{
	struct sigaction action;
	
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

static ngx_int_t
start_helper_server(ngx_cycle_t *cycle)
{
    passenger_main_conf_t *main_conf = &ngx_http_passenger_main_conf;
    u_char                 helper_server_filename[NGX_MAX_PATH];
    int                    p[2], e;
    pid_t                  pid;
    long                   i;
    ssize_t                ret;
    
    ignore_sigpipe();
    
    if (ngx_snprintf(helper_server_filename, sizeof(helper_server_filename),
                     "%s/ext/nginx/HelperServer",
                     main_conf->root_dir.data) == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not create Passenger HelperServer filename string");
        return NGX_ERROR;
    }
    
    if (pipe(p) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not start the Passenger helper server: pipe() failed");
        return NGX_ERROR;
    }
    
    /* TODO: writer proper password generation code */
    ngx_memzero(helper_server_password_data, HELPER_SERVER_PASSWORD_SIZE);
    ngx_http_passenger_helper_server_password.data = helper_server_password_data;
    ngx_http_passenger_helper_server_password.len  = HELPER_SERVER_PASSWORD_SIZE;
    
    pid = fork();
    switch (pid) {
    case 0:
        /* Child process. */
        close(p[1]);
        
        /* Nginx redirects stderr to the error log file. Make sure that
         * stdout is redirected to the error log file as well.
         */
        dup2(2, 1);
        
        /* Close all file descriptors except stdin, stdout, stderr and
         * the reader part of the pipe we just created. 
         */
        if (dup2(p[0], 3) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "could not start the Passenger helper server: "
                          "dup2() failed");
        }
        for (i = sysconf(_SC_OPEN_MAX) - 1; i > 3; i--) {
            close(i);
        }
        
        execlp((const char *) helper_server_filename,
               "PassengerHelperServer",
               main_conf->root_dir.data,            /* Passenger root dir. */
               main_conf->ruby.data,                /* Ruby interpreter. */
               "3",                                 /* Admin pipe. */
               "4",                                 /* Max pool size. */
               NULL);
        e = errno;
        fprintf(stderr, "*** Could not start the Passenger helper server (%s): "
                "exec() failed: %s (%d)\n",
                (const char *) helper_server_filename, strerror(e), e);
        _exit(1);
        
    case -1:
        /* Error */
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not start the Passenger helper server: "
                      "fork() failed");
        close(p[0]);
        close(p[1]);
        return NGX_ERROR;
        
    default:
        /* Parent process. */
        close(p[0]);
        
        i = 0;
        do {
            ret = write(p[1], helper_server_password_data,
                        HELPER_SERVER_PASSWORD_SIZE - i);
            if (ret == -1) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "could not send password to the Passenger helper server: "
                              "write() failed");
                close(p[1]);
                kill(pid, SIGTERM);
                waitpid(pid, NULL, 0);
                return NGX_ERROR;
            } else {
                i += ret;
            }
        } while (i < HELPER_SERVER_PASSWORD_SIZE);
        
        helper_server_pid        = pid;
        helper_server_admin_pipe = p[1];
        break;
    }
    
    return NGX_OK;
}

static void
shutdown_helper_server(ngx_cycle_t *cycle)
{
    time_t begin_time;
    int    helper_server_exited;
    
    /* We write one byte to the admin pipe, doesn't matter what the byte is.
     * The helper server will detect this as an exit command.
     */
    write(helper_server_admin_pipe, "x", 1);
    close(helper_server_admin_pipe);
    
    /* Wait at most HELPER_SERVER_MAX_SHUTDOWN_TIME seconds for the helper
     * server to exit.
     */
    begin_time = time(NULL);
    helper_server_exited = 0;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cycle->log, 0,
                   "Waiting for Passenger helper server (PID %l) to exit...",
                   (long) helper_server_pid);
    while (!helper_server_exited && time(NULL) - begin_time < HELPER_SERVER_MAX_SHUTDOWN_TIME) {
        pid_t ret;
        
        ret = waitpid(helper_server_pid, NULL, WNOHANG);
        if (ret > 0 || (ret == -1 && errno == ECHILD)) {
            helper_server_exited = 1;
        } else {
            usleep(100000);
        }
    }
    
    /* Kill helper server if it did not exit in time. */
    if (!helper_server_exited) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cycle->log, 0,
                       "Passenger helper server did not exit in time. "
                       "Killing it...");
        kill(helper_server_pid, SIGTERM);
        waitpid(helper_server_pid, NULL, 0);
    }
    
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cycle->log, 0,
                   "Passenger helper server exited.");
}

static ngx_int_t
ngx_http_scgi_script_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                         *p;
    ngx_http_passenger_loc_conf_t  *slcf;

    if (r->uri.len) {
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;

        slcf = ngx_http_get_module_loc_conf(r, ngx_http_passenger_module);

        if (r->uri.data[r->uri.len - 1] != '/') {
            v->len = r->uri.len;
            v->data = r->uri.data;
            return NGX_OK;
        }

        v->len = r->uri.len + slcf->index.len;

        v->data = ngx_palloc(r->pool, v->len);
        if (v->data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_copy(v->data, r->uri.data, r->uri.len);
        ngx_memcpy(p, slcf->index.data, slcf->index.len);

    } else {
        v->len = 0;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = NULL;

        return NGX_OK;
    }

    return NGX_OK;
}

static ngx_int_t
add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var;

    var = ngx_http_add_variable(cf, &ngx_http_scgi_script_name,
                                NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE);
    if (var == NULL) {
        return NGX_ERROR;
    }

    var->get_handler = ngx_http_scgi_script_name_variable;

    return NGX_OK;
}

static ngx_int_t
pre_config_init(ngx_conf_t *cf)
{
    ngx_int_t ret;
    
    ngx_memzero(&ngx_http_passenger_main_conf, sizeof(passenger_main_conf_t));
    
    ret = add_variables(cf);
    if (ret != NGX_OK) {
        return ret;
    }
    
    return NGX_OK;
}


static ngx_http_module_t  ngx_http_passenger_module_ctx = {
    pre_config_init,                     /* preconfiguration */
    register_content_handler,            /* postconfiguration */

    ngx_http_passenger_create_main_conf, /* create main configuration */
    ngx_http_passenger_init_main_conf,   /* init main configuration */

    NULL,                                /* create server configuration */
    NULL,                                /* merge server configuration */

    ngx_http_passenger_create_loc_conf,  /* create location configuration */
    ngx_http_passenger_merge_loc_conf    /* merge location configuration */
};


ngx_module_t  ngx_http_passenger_module = {
    NGX_MODULE_V1,
    &ngx_http_passenger_module_ctx,                  /* module context */
    (ngx_command_t *) ngx_http_passenger_commands,   /* module directives */
    NGX_HTTP_MODULE,                                 /* module type */
    NULL,                                            /* init master */
    start_helper_server,                             /* init module */
    NULL,                                            /* init process */
    NULL,                                            /* init thread */
    NULL,                                            /* exit thread */
    NULL,                                            /* exit process */
    shutdown_helper_server,                          /* exit master */
    NGX_MODULE_V1_PADDING
};
