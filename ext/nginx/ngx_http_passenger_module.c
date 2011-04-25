/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (C) 2010 Phusion
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


static int               first_start = 1;
ngx_str_t                passenger_schema_string;
ngx_str_t                passenger_placeholder_upstream_address;
PassengerCachedFileStat *passenger_stat_cache;
AgentsStarter           *passenger_agents_starter = NULL;
ngx_cycle_t             *passenger_current_cycle;


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
    
    last = ngx_snprintf(filename, sizeof(filename) - 1, "%s/control_process.pid",
        agents_starter_get_server_instance_dir(passenger_agents_starter));
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
 * This function is called after forking and just before exec()ing the helper server.
 */
static void
starting_helper_server_after_fork(void *arg) {
    ngx_cycle_t *cycle = (void *) arg;
    char        *log_filename;
    FILE        *log_file;
    
    /* At this point, stdout and stderr may still point to the console.
     * Make sure that they're both redirected to the log file.
     */
    log_file = NULL;
    if (cycle->new_log.file->name.len > 0) {
        log_filename = ngx_str_null_terminate(&cycle->new_log.file->name);
        log_file = fopen((const char *) log_filename, "a");
        if (log_file == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "could not open the error log file for writing");
        }
        free(log_filename);
    } else if (cycle->log != NULL && cycle->log->file->name.len > 0) {
        log_filename = ngx_str_null_terminate(&cycle->log->file->name);
        log_file = fopen((const char *) log_filename, "a");
        if (log_file == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "could not open the error log file for writing");
        }
        free(log_filename);
    }
    if (log_file == NULL) {
        /* If the log file cannot be opened then we redirect stdout
         * and stderr to /dev/null, because if the user disconnects
         * from the console on which Nginx is started, then on Linux
         * any writes to stdout or stderr will result in an EIO error.
         */
        log_file = fopen("/dev/null", "w");
    }
    if (log_file != NULL) {
        dup2(fileno(log_file), 1);
        dup2(fileno(log_file), 2);
        fclose(log_file);
    }
    
    /* Set SERVER_SOFTWARE so that application processes know what web
     * server they're running on during startup. */
    setenv("SERVER_SOFTWARE", NGINX_VER, 1);
}

static ngx_int_t
create_file(ngx_cycle_t *cycle, const u_char *filename, const u_char *contents, size_t len) {
    FILE  *f;
    int    ret;
    size_t total_written = 0, written;

    f = fopen((const char *) filename, "w");
    if (f != NULL) {
        /* We must do something with these return values because
         * otherwise on some platforms it will cause a compiler
         * warning.
         */
        do {
            ret = fchmod(fileno(f), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        } while (ret == -1 && errno == EINTR);
        do {
            written = fwrite(contents + total_written, 1,
                len - total_written, f);
            total_written += written;
        } while (total_written < len);
        fclose(f);
        return NGX_OK;
    } else {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
            "could not create %s", filename);
            return NGX_ERROR;
    }
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
    ngx_uint_t       i;
    ngx_str_t       *prestart_uris;
    char           **prestart_uris_ary = NULL;
    u_char  filename[NGX_MAX_PATH], *last;
    char   *debug_log_file = NULL;
    char   *default_user = NULL;
    char   *default_group = NULL;
    char   *passenger_root = NULL;
    char   *ruby = NULL;
    char   *analytics_log_dir;
    char   *analytics_log_user;
    char   *analytics_log_group;
    char   *analytics_log_permissions;
    char   *union_station_gateway_address;
    char   *union_station_gateway_cert;
    char   *error_message = NULL;
    
    core_conf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    result    = NGX_OK;
    
    /* Create null-terminated versions of some strings. */
    debug_log_file = ngx_str_null_terminate(&passenger_main_conf.debug_log_file);
    default_user   = ngx_str_null_terminate(&passenger_main_conf.default_user);
    default_group  = ngx_str_null_terminate(&passenger_main_conf.default_group);
    passenger_root = ngx_str_null_terminate(&passenger_main_conf.root_dir);
    ruby           = ngx_str_null_terminate(&passenger_main_conf.ruby);
    analytics_log_dir = ngx_str_null_terminate(&passenger_main_conf.analytics_log_dir);
    analytics_log_user = ngx_str_null_terminate(&passenger_main_conf.analytics_log_user);
    analytics_log_group = ngx_str_null_terminate(&passenger_main_conf.analytics_log_group);
    analytics_log_permissions = ngx_str_null_terminate(&passenger_main_conf.analytics_log_permissions);
    union_station_gateway_address = ngx_str_null_terminate(&passenger_main_conf.union_station_gateway_address);
    union_station_gateway_cert = ngx_str_null_terminate(&passenger_main_conf.union_station_gateway_cert);
    
    prestart_uris = (ngx_str_t *) passenger_main_conf.prestart_uris->elts;
    prestart_uris_ary = calloc(sizeof(char *), passenger_main_conf.prestart_uris->nelts);
    for (i = 0; i < passenger_main_conf.prestart_uris->nelts; i++) {
        prestart_uris_ary[i] = malloc(prestart_uris[i].len + 1);
        if (prestart_uris_ary[i] == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ENOMEM, "Cannot allocate memory");
            result = NGX_ERROR;
            goto cleanup;
        }
        memcpy(prestart_uris_ary[i], prestart_uris[i].data, prestart_uris[i].len);
        prestart_uris_ary[i][prestart_uris[i].len] = '\0';
    }
    
    ret = agents_starter_start(passenger_agents_starter,
        passenger_main_conf.log_level, debug_log_file, getpid(),
        "", passenger_main_conf.user_switching,
        default_user, default_group,
        core_conf->user, core_conf->group,
        passenger_root, ruby, passenger_main_conf.max_pool_size,
        passenger_main_conf.max_instances_per_app,
        passenger_main_conf.pool_idle_time,
        "",
        analytics_log_dir, analytics_log_user,
        analytics_log_group, analytics_log_permissions,
        union_station_gateway_address,
        passenger_main_conf.union_station_gateway_port,
        union_station_gateway_cert,
        (const char **) prestart_uris_ary, passenger_main_conf.prestart_uris->nelts,
        starting_helper_server_after_fork,
        cycle,
        &error_message);
    if (!ret) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "%s", error_message);
        result = NGX_ERROR;
        goto cleanup;
    }
    
    /* Create the file passenger_temp_dir + "/control_process.pid"
     * and make it writable by the worker processes. This is because
     * save_master_process_pid is run after Nginx has lowered privileges.
     */
    last = ngx_snprintf(filename, sizeof(filename) - 1,
                        "%s/control_process.pid",
                        agents_starter_get_server_instance_dir(passenger_agents_starter));
    *last = (u_char) '\0';
    if (create_file(cycle, filename, (const u_char *) "", 0) != NGX_OK) {
        result = NGX_ERROR;
        goto cleanup;
    }
    do {
        ret = chown((const char *) filename, (uid_t) core_conf->user, (gid_t) -1);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) {
        result = NGX_ERROR;
        goto cleanup;
    }

    /* Create various other info files. */
    last = ngx_snprintf(filename, sizeof(filename) - 1,
                        "%s/web_server.txt",
                        agents_starter_get_generation_dir(passenger_agents_starter));
    *last = (u_char) '\0';
    if (create_file(cycle, filename, (const u_char *) NGINX_VER, strlen(NGINX_VER)) != NGX_OK) {
        result = NGX_ERROR;
        goto cleanup;
    }

    last = ngx_snprintf(filename, sizeof(filename) - 1,
                        "%s/config_files.txt",
                        agents_starter_get_generation_dir(passenger_agents_starter));
    *last = (u_char) '\0';
    if (create_file(cycle, filename, cycle->conf_file.data, cycle->conf_file.len) != NGX_OK) {
        result = NGX_ERROR;
        goto cleanup;
    }
    
    last = ngx_snprintf(filename, sizeof(filename) - 1,
                        "%s/analytics_log_dir.txt",
                        agents_starter_get_generation_dir(passenger_agents_starter));
    *last = (u_char) '\0';
    if (create_file(cycle, filename, passenger_main_conf.analytics_log_dir.data,
                    passenger_main_conf.analytics_log_dir.len) != NGX_OK) {
        result = NGX_ERROR;
        goto cleanup;
    }

cleanup:
    free(debug_log_file);
    free(default_user);
    free(default_group);
    free(passenger_root);
    free(ruby);
    free(analytics_log_dir);
    free(analytics_log_user);
    free(analytics_log_group);
    free(analytics_log_permissions);
    free(union_station_gateway_address);
    free(union_station_gateway_cert);
    free(error_message);
    if (prestart_uris_ary != NULL) {
        for (i = 0; i < passenger_main_conf.prestart_uris->nelts; i++) {
            free(prestart_uris_ary[i]);
        }
        free(prestart_uris_ary);
    }
    
    if (result == NGX_ERROR && passenger_main_conf.abort_on_startup_error) {
        exit(1);
    }
    
    return result;
}

/**
 * Shutdown the helper server, if there's one running.
 */
static void
shutdown_helper_server() {
    if (passenger_agents_starter != NULL) {
        agents_starter_free(passenger_agents_starter);
        passenger_agents_starter = NULL;
    }
}


/**
 * Called when:
 * - Nginx is started, before the configuration is loaded and before daemonization.
 * - Nginx is restarted, before the configuration is reloaded.
 */
static ngx_int_t
pre_config_init(ngx_conf_t *cf)
{
    char *error_message;
    
    shutdown_helper_server();
    
    ngx_memzero(&passenger_main_conf, sizeof(passenger_main_conf_t));
    passenger_schema_string.data = (u_char *) "passenger:";
    passenger_schema_string.len  = sizeof("passenger:") - 1;
    passenger_placeholder_upstream_address.data = (u_char *) "unix:/passenger_helper_server";
    passenger_placeholder_upstream_address.len  = sizeof("unix:/passenger_helper_server") - 1;
    passenger_stat_cache = cached_file_stat_new(1024);
    passenger_agents_starter = agents_starter_new(AS_NGINX, &error_message);
    
    if (passenger_agents_starter == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno, "%s", error_message);
        free(error_message);
        return NGX_ERROR;
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
        passenger_current_cycle = cycle;
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
    ngx_core_conf_t *core_conf;
    
    if (passenger_main_conf.root_dir.len != 0) {
        save_master_process_pid(cycle);
        
        core_conf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
        if (core_conf->master) {
            agents_starter_detach(passenger_agents_starter);
        }
    }
    return NGX_OK;
}

/**
 * Called when Nginx exits. Not called when Nginx is restarted.
 */
static void
exit_master(ngx_cycle_t *cycle) {
    shutdown_helper_server();
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
