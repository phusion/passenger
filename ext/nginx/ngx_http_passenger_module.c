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
#include <sys/stat.h>
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


static int        first_start = 1;
/** perl_module destroys the original environment variables for some reason,
 * so when we get a SIGHUP (for restarting Nginx) $TMPDIR might not have the
 * same value as it had during Nginx startup. When restarting Nginx, we want
 * the new helper server instance to have the same $TMPDIR as it initially had,
 * so here we cache the original value instead of getenv()'ing it every time.
 */
const char          *system_temp_dir = NULL;
ngx_str_t            passenger_schema_string;
CachedFileStat      *passenger_stat_cache;
HelperServerStarter *passenger_helper_server_starter = NULL;


/*
    HISTORIC NOTE:
    We used to register passenger_content_handler as a default content handler,
    instead of setting ngx_http_core_loc_conf_t->handler. However, if
    ngx_http_read_client_request_body (and thus passenger_content_handler)
    returns NGX_AGAIN, then Nginx will pass the not-fully-receive file upload
    data to the upstream handler even though it shouldn't. Is this an Nginx
    bug? In any case, setting ngx_http_core_loc_conf_t->handler fixed the
    problem.
    
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
    *h = passenger_content_handler;
    
    return NGX_OK;
}
*/

static void
ignore_sigpipe()
{
    struct sigaction action;
    
    action.sa_handler = SIG_IGN;
    action.sa_flags   = 0;
    sigemptyset(&action.sa_mask);
    sigaction(SIGPIPE, &action, NULL);
}

char *
ngx_str_null_terminate(ngx_str_t *str) {
    char *result = malloc(str->len + 1);
    memcpy(result, str->data, str->len);
    result[str->len] = '\0';
    return result;
}

/**
 * Save the Nginx master process's PID into a file under the server instance directory.
 *
 * A bug/limitation in Nginx doesn't allow us to create the server instance dir
 * *after* Nginx has daemonized, so the server instance dir's filename contains Nginx's
 * PID before daemonization. Normally PhusionPassenger::AdminTools::ServerInstance (used
 * by e.g. passenger-status) will think that the server instance dir is stale because the
 * PID in the filename doesn't exist. This PID file tells AdminTools::ServerInstance
 * what the actual PID is.
 */
static ngx_int_t
save_master_process_pid(ngx_cycle_t *cycle) {
    u_char filename[NGX_MAX_PATH];
    u_char *last;
    FILE *f;
    
    last = ngx_snprintf(filename, sizeof(filename) - 1, "%s/instance.pid",
        helper_server_starter_get_server_instance_dir(passenger_helper_server_starter));
    *last = (u_char) '\0';
    
    f = fopen((const char *) filename, "w");
    if (f != NULL) {
        fprintf(f, "%ld", (long) getppid());
        fclose(f);
    } else {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not create %s", filename);
    }
    
    return NGX_OK;
}

/**
 * Start the helper server and save its runtime information into various variables.
 *
 * @pre The helper server isn't already started.
 * @pre The Nginx configuration has been loaded.
 */
static ngx_int_t
start_helper_server(ngx_cycle_t *cycle) {
    ngx_core_conf_t *core_conf;
    ngx_int_t        ret, result;
    char            *default_user = NULL;
    char            *passenger_root = NULL;
    char            *ruby = NULL;
    char            *error_message = NULL;
    
    core_conf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    result    = NGX_OK;
    
    /* Create null-terminated versions of some strings. */
    default_user   = ngx_str_null_terminate(&passenger_main_conf.default_user);
    passenger_root = ngx_str_null_terminate(&passenger_main_conf.root_dir);
    ruby           = ngx_str_null_terminate(&passenger_main_conf.ruby);
    
    passenger_helper_server_starter = helper_server_starter_new(HSST_NGINX, &error_message);
    if (passenger_helper_server_starter == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "%s", error_message);
        result = NGX_ERROR;
        goto cleanup;
    }
    
    ret = helper_server_starter_start(passenger_helper_server_starter,
        passenger_main_conf.log_level, getpid(),
        system_temp_dir, passenger_main_conf.user_switching,
        default_user, core_conf->user, core_conf->group,
        passenger_root, ruby, &error_message);
    if (!ret) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "%s", error_message);
        result = NGX_ERROR;
        goto cleanup;
    }

cleanup:
    free(default_user);
    free(passenger_root);
    free(ruby);
    free(error_message);
    return result;
}

/**
 * Shutdown the helper server, if there's one running.
 */
static void
shutdown_helper_server(ngx_cycle_t *cycle) {
    if (passenger_helper_server_starter != NULL) {
        helper_server_starter_free(passenger_helper_server_starter);
        passenger_helper_server_starter = NULL;
    }
}


/**
 * Called when:
 * - Nginx is started, before the configuration is reloaded and before daemonization.
 * - Nginx is restarted, before the configuration is reloaded.
 */
static ngx_int_t
pre_config_init(ngx_conf_t *cf)
{   
    ngx_memzero(&passenger_main_conf, sizeof(passenger_main_conf_t));
    passenger_schema_string.data = (u_char *) "passenger://";
    passenger_schema_string.len  = sizeof("passenger://") - 1;
    passenger_stat_cache = cached_file_stat_new(1024);
    
    if (system_temp_dir == NULL) {
        const char *tmp = getenv("TMPDIR");
        if (tmp == NULL || *tmp == '\0') {
            system_temp_dir = "/tmp";
        } else {
            system_temp_dir = strdup(tmp);
        }
    }
    
    return NGX_OK;
}

/**
 * Called when:
 * - Nginx is started, before daemonization and after the configuration has loaded.
 * - Nginx is restarted, after the configuration has reloaded.
 */
static ngx_int_t
init_module(ngx_cycle_t *cycle) {
    shutdown_helper_server(cycle);
    if (passenger_main_conf.root_dir.len != 0) {
        if (first_start) {
            /* Ignore SIGPIPE now so that, if the helper server fails to start,
             * Nginx doesn't get killed by the default SIGPIPE handler upon
             * writing the password to the helper server.
             */
            ignore_sigpipe();
            first_start = 0;
        }
        if (start_helper_server(cycle) != NGX_OK) {
            passenger_main_conf.root_dir.len = 0;
            return NGX_OK;
        }
    }
    return NGX_OK;
}

/**
 * Called when an Nginx worker process is started. This happens after init_module
 * is called.
 *
 * If 'master_process' is turned off, then there is only one single Nginx process
 * in total, and this process also acts as the worker process. In this case
 * init_worker_process is only called when Nginx is started, but not when it's restarted.
 */
static ngx_int_t
init_worker_process(ngx_cycle_t *cycle) {
    if (passenger_main_conf.root_dir.len != 0) {
        save_master_process_pid(cycle);
    }
    return NGX_OK;
}

/**
 * Called when Nginx exits. Not called when Nginx is restarted.
 */
static void
exit_master(ngx_cycle_t *cycle) {
    shutdown_helper_server(cycle);
}


static ngx_http_module_t passenger_module_ctx = {
    pre_config_init,                     /* preconfiguration */
    /* register_content_handler */ NULL, /* postconfiguration */

    passenger_create_main_conf,          /* create main configuration */
    passenger_init_main_conf,            /* init main configuration */

    NULL,                                /* create server configuration */
    NULL,                                /* merge server configuration */

    passenger_create_loc_conf,           /* create location configuration */
    passenger_merge_loc_conf             /* merge location configuration */
};


ngx_module_t ngx_http_passenger_module = {
    NGX_MODULE_V1,
    &passenger_module_ctx,                  /* module context */
    (ngx_command_t *) passenger_commands,   /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    init_module,                            /* init module */
    init_worker_process,                    /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    exit_master,                            /* exit master */
    NGX_MODULE_V1_PADDING
};
