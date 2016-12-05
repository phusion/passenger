/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (c) 2010-2016 Phusion Holding B.V.
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

#define MODP_B64_DONT_INCLUDE_BOOST_ENDIANNESS_HEADERS
#if NGX_HAVE_LITTLE_ENDIAN
    #define BOOST_LITTLE_ENDIAN
#else
    #define BOOST_BIG_ENDIAN
#endif

#include "ngx_http_passenger_module.h"
#include "Configuration.h"
#include "ContentHandler.h"
#include "cxx_supportlib/Constants.h"
#include "cxx_supportlib/vendor-modified/modp_b64.cpp" /* File is C compatible. */
#include "cxx_supportlib/vendor-modified/modp_b64_strict_aliasing.cpp" /* File is C compatible. */


static int                first_start = 1;
ngx_str_t                 pp_schema_string;
ngx_str_t                 pp_placeholder_upstream_address;
PP_CachedFileStat        *pp_stat_cache;
PP_AppTypeDetector       *pp_app_type_detector;
PsgWatchdogLauncher      *psg_watchdog_launcher = NULL;
ngx_cycle_t              *pp_current_cycle;


static void
ignore_sigpipe() {
    struct sigaction action;

    action.sa_handler = SIG_IGN;
    action.sa_flags   = 0;
    sigemptyset(&action.sa_mask);
    sigaction(SIGPIPE, &action, NULL);
}

static char *
ngx_str_null_terminate(ngx_str_t *str) {
    char *result = malloc(str->len + 1);
    if (result != NULL) {
        memcpy(result, str->data, str->len);
        result[str->len] = '\0';
    }
    return result;
}

static void
psg_variant_map_set_ngx_str(PsgVariantMap *m,
    const char *name,
    ngx_str_t *value)
{
    psg_variant_map_set(m, name, (const char *) value->data, value->len);
}

/**
 * Save the Nginx master process's PID into a file in the instance directory.
 * This PID file is used in the `passenger-config reopen-logs` command.
 *
 * The master process's PID is already passed to the Watchdog through the
 * "web_server_control_process_pid" property, but that isn't enough. The Watchdog
 * is started *before* Nginx has daemonized, so after Nginx has daemonized,
 * the PID that we passed to the Watchdog is no longer valid. We fix that by
 * creating this PID file after daemonization.
 */
static ngx_int_t
save_master_process_pid(ngx_cycle_t *cycle) {
    u_char filename[NGX_MAX_PATH];
    u_char *last;
    FILE *f;

    last = ngx_snprintf(filename, sizeof(filename) - 1, "%s/web_server_info/control_process.pid",
                        psg_watchdog_launcher_get_instance_dir(psg_watchdog_launcher, NULL));
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
 * This function is called after forking and just before exec()ing the watchdog.
 */
static void
starting_watchdog_after_fork(void *paramCycle, void *paramParams) {
    ngx_cycle_t *cycle = (void *) paramCycle;
    PsgVariantMap *params = (void *) paramParams;

    char        *log_filename;
    FILE        *log_file;
    ngx_core_conf_t *ccf;
    ngx_uint_t   i;
    ngx_str_t   *envs;
    const char  *env;

    /* At this point, stdout and stderr may still point to the console.
     * Make sure that they're both redirected to the log file.
     */
    log_file = NULL;
    log_filename = psg_variant_map_get_optional(params, "log_file");
    if (log_filename == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                "no passenger log file configured, discarding log output");
    } else {
        log_file = fopen(log_filename, "a");
        if (log_file == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                    "could not open the passenger log file for writing during Nginx startup, some log lines might be lost (will retry from Passenger core)");
        }
        free(log_filename);
        log_filename = NULL;
    }

    if (log_file == NULL) {
        /* If the log file cannot be opened then we redirect stdout
         * and stderr to /dev/null, because if the user disconnects
         * from the console on which Nginx is started, then on Linux
         * any writes to stdout or stderr will result in an EIO error.
         */
        log_file = fopen("/dev/null", "w");
        if (log_file == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                    "could not open /dev/null for logs, this will probably cause EIO errors");
        }
    }
    if (log_file != NULL) {
        dup2(fileno(log_file), 1);
        dup2(fileno(log_file), 2);
        fclose(log_file);
    }

    /* Set environment variables in Nginx config file. */
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    envs = ccf->env.elts;
    for (i = 0; i < ccf->env.nelts; i++) {
        env = (const char *) envs[i].data;
        if (strchr(env, '=') != NULL) {
            putenv(strdup(env));
        }
    }
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
 * Start the watchdog and save the runtime information into various variables.
 *
 * @pre The watchdog isn't already started.
 * @pre The Nginx configuration has been loaded.
 */
static ngx_int_t
start_watchdog(ngx_cycle_t *cycle) {
    ngx_core_conf_t *core_conf;
    ngx_int_t        ret, result;
    ngx_uint_t       i;
    ngx_str_t       *prestart_uris;
    char           **prestart_uris_ary = NULL;
    ngx_keyval_t    *ctl = NULL;
    PsgVariantMap   *params = NULL;
    u_char  filename[NGX_MAX_PATH], *last;
    char   *passenger_root = NULL;
    char   *error_message = NULL;

    core_conf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    result    = NGX_OK;
    params    = psg_variant_map_new();
    passenger_root = ngx_str_null_terminate(&passenger_main_conf.root_dir);
    if (passenger_root == NULL) {
        goto error_enomem;
    }

    pp_app_type_detector_set_throttle_rate(pp_app_type_detector,
        passenger_main_conf.stat_throttle_rate);

    prestart_uris = (ngx_str_t *) passenger_main_conf.prestart_uris->elts;
    prestart_uris_ary = calloc(sizeof(char *), passenger_main_conf.prestart_uris->nelts);
    for (i = 0; i < passenger_main_conf.prestart_uris->nelts; i++) {
        prestart_uris_ary[i] = ngx_str_null_terminate(&prestart_uris[i]);
        if (prestart_uris_ary[i] == NULL) {
            goto error_enomem;
        }
    }

    psg_variant_map_set_int    (params, "web_server_control_process_pid", getpid());
    psg_variant_map_set        (params, "server_software", NGINX_VER, strlen(NGINX_VER));
    psg_variant_map_set        (params, "server_version", NGINX_VERSION, strlen(NGINX_VERSION));
    psg_variant_map_set_bool   (params, "multi_app", 1);
    psg_variant_map_set_bool   (params, "load_shell_envvars", 1);
    psg_variant_map_set_int    (params, "log_level", passenger_main_conf.log_level);
    psg_variant_map_set_ngx_str(params, "file_descriptor_log_file", &passenger_main_conf.file_descriptor_log_file);
    psg_variant_map_set_int    (params, "socket_backlog", passenger_main_conf.socket_backlog);
    psg_variant_map_set_ngx_str(params, "data_buffer_dir", &passenger_main_conf.data_buffer_dir);
    psg_variant_map_set_ngx_str(params, "instance_registry_dir", &passenger_main_conf.instance_registry_dir);
    psg_variant_map_set_bool   (params, "disable_security_update_check", passenger_main_conf.disable_security_update_check);
    psg_variant_map_set_ngx_str(params, "security_update_check_proxy", &passenger_main_conf.security_update_check_proxy);
    psg_variant_map_set_bool   (params, "user_switching", passenger_main_conf.user_switching);
    psg_variant_map_set_bool   (params, "show_version_in_header", passenger_main_conf.show_version_in_header);
    psg_variant_map_set_bool   (params, "turbocaching", passenger_main_conf.turbocaching);
    psg_variant_map_set_ngx_str(params, "default_user", &passenger_main_conf.default_user);
    psg_variant_map_set_ngx_str(params, "default_group", &passenger_main_conf.default_group);
    psg_variant_map_set_ngx_str(params, "default_ruby", &passenger_main_conf.default_ruby);
    psg_variant_map_set_int    (params, "max_pool_size", passenger_main_conf.max_pool_size);
    psg_variant_map_set_int    (params, "pool_idle_time", passenger_main_conf.pool_idle_time);
    psg_variant_map_set_int    (params, "response_buffer_high_watermark", passenger_main_conf.response_buffer_high_watermark);
    psg_variant_map_set_int    (params, "stat_throttle_rate", passenger_main_conf.stat_throttle_rate);
    psg_variant_map_set_ngx_str(params, "analytics_log_user", &passenger_main_conf.analytics_log_user);
    psg_variant_map_set_ngx_str(params, "analytics_log_group", &passenger_main_conf.analytics_log_group);
    psg_variant_map_set_bool   (params, "union_station_support", passenger_main_conf.union_station_support);
    psg_variant_map_set_ngx_str(params, "union_station_gateway_address", &passenger_main_conf.union_station_gateway_address);
    psg_variant_map_set_int    (params, "union_station_gateway_port", passenger_main_conf.union_station_gateway_port);
    psg_variant_map_set_ngx_str(params, "union_station_gateway_cert", &passenger_main_conf.union_station_gateway_cert);
    psg_variant_map_set_ngx_str(params, "union_station_proxy_address", &passenger_main_conf.union_station_proxy_address);
    psg_variant_map_set_strset (params, "prestart_urls", (const char **) prestart_uris_ary, passenger_main_conf.prestart_uris->nelts);

    if (passenger_main_conf.core_file_descriptor_ulimit != NGX_CONF_UNSET_UINT) {
        psg_variant_map_set_int(params, "core_file_descriptor_ulimit", passenger_main_conf.core_file_descriptor_ulimit);
    }

    if (passenger_main_conf.log_file.len > 0) {
        psg_variant_map_set_ngx_str(params, "log_file", &passenger_main_conf.log_file);
    } else if (cycle->new_log.file == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "Cannot initialize " PROGRAM_NAME
            " because Nginx is not configured with an error log file."
            " Please either configure Nginx with an error log file, or configure "
            PROGRAM_NAME " with a `passenger_log_file`");
        result = NGX_ERROR;
        goto cleanup;
    } else if (cycle->new_log.file->name.len > 0) {
        psg_variant_map_set_ngx_str(params, "log_file", &cycle->new_log.file->name);
    } else if (cycle->log->file->name.len > 0) {
        psg_variant_map_set_ngx_str(params, "log_file", &cycle->log->file->name);
    }

    ctl = (ngx_keyval_t *) passenger_main_conf.ctl->elts;
    for (i = 0; i < passenger_main_conf.ctl->nelts; i++) {
        psg_variant_map_set2(params,
            (const char *) ctl[i].key.data, ctl[i].key.len - 1,
            (const char *) ctl[i].value.data, ctl[i].value.len - 1);
    }

    ret = psg_watchdog_launcher_start(psg_watchdog_launcher,
        passenger_root,
        params,
        starting_watchdog_after_fork,
        cycle,
        &error_message);
    if (!ret) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "%s", error_message);
        result = NGX_ERROR;
        goto cleanup;
    }

    /* Create the file instance_dir + "/web_server_info/control_process.pid"
     * and make it writable by the worker processes. This is because
     * save_master_process_pid is run after Nginx has lowered privileges.
     */
    last = ngx_snprintf(filename, sizeof(filename) - 1,
                        "%s/web_server_info/control_process.pid",
                        psg_watchdog_launcher_get_instance_dir(psg_watchdog_launcher, NULL));
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

cleanup:
    psg_variant_map_free(params);
    free(passenger_root);
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

error_enomem:
    ngx_log_error(NGX_LOG_ALERT, cycle->log, ENOMEM, "Cannot allocate memory");
    result = NGX_ERROR;
    goto cleanup;
}

/**
 * Shutdown the watchdog, if there's one running.
 */
static void
shutdown_watchdog() {
    if (psg_watchdog_launcher != NULL) {
        psg_watchdog_launcher_free(psg_watchdog_launcher);
        psg_watchdog_launcher = NULL;
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

    shutdown_watchdog();

    ngx_memzero(&passenger_main_conf, sizeof(passenger_main_conf_t));
    pp_schema_string.data = (u_char *) "passenger:";
    pp_schema_string.len  = sizeof("passenger:") - 1;
    pp_placeholder_upstream_address.data = (u_char *) "unix:/passenger_core";
    pp_placeholder_upstream_address.len  = sizeof("unix:/passenger_core") - 1;
    pp_stat_cache = pp_cached_file_stat_new(1024);
    pp_app_type_detector = pp_app_type_detector_new(DEFAULT_STAT_THROTTLE_RATE);
    psg_watchdog_launcher = psg_watchdog_launcher_new(IM_NGINX, &error_message);

    if (psg_watchdog_launcher == NULL) {
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
    if (passenger_main_conf.root_dir.len != 0 && !ngx_test_config) {
        if (first_start) {
            /* Ignore SIGPIPE now so that, if the watchdog fails to start,
             * Nginx doesn't get killed by the default SIGPIPE handler upon
             * writing the password to the watchdog.
             */
            ignore_sigpipe();
            first_start = 0;
        }
        if (start_watchdog(cycle) != NGX_OK) {
            passenger_main_conf.root_dir.len = 0;
            return NGX_OK;
        }
        pp_current_cycle = cycle;
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

    if (passenger_main_conf.root_dir.len != 0 && !ngx_test_config) {
        save_master_process_pid(cycle);

        core_conf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
        if (core_conf->master) {
            psg_watchdog_launcher_detach(psg_watchdog_launcher);
        }
    }
    return NGX_OK;
}

/**
 * Called when Nginx exits. Not called when Nginx is restarted.
 */
static void
exit_master(ngx_cycle_t *cycle) {
    shutdown_watchdog();
}


static ngx_http_module_t passenger_module_ctx = {
    pre_config_init,                     /* preconfiguration */
    passenger_postprocess_config,        /* postconfiguration */

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
