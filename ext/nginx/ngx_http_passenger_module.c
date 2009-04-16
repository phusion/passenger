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


static ngx_str_t  ngx_http_scgi_script_name = ngx_string("scgi_script_name");
static pid_t      helper_server_pid;
static int        helper_server_admin_pipe;
static u_char     helper_server_password_data[HELPER_SERVER_PASSWORD_SIZE];
const char        passenger_temp_dir[NGX_MAX_PATH];
ngx_str_t         passenger_schema_string;
ngx_str_t         passenger_helper_server_password;
const char        passenger_helper_server_socket[NGX_MAX_PATH];
CachedMultiFileStat *passenger_stat_cache;


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

static ngx_int_t
start_helper_server(ngx_cycle_t *cycle)
{
    passenger_main_conf_t *main_conf = &passenger_main_conf;
    ngx_core_conf_t       *ccf;
    u_char                 helper_server_filename[NGX_MAX_PATH];
    u_char                 log_level_string[10];
    u_char                 max_pool_size_string[10];
    u_char                 max_instances_per_app_string[10];
    u_char                 pool_idle_time_string[10];
    u_char                 user_switching_string[2];
    u_char                 worker_uid_string[15];
    u_char                 worker_gid_string[15];
    u_char                 filename[NGX_MAX_PATH];
    u_char                *last;
    int                    admin_pipe[2], feedback_pipe[2], e;
    pid_t                  pid;
    long                   i;
    ssize_t                ret;
    char                   buf;
    FILE                  *f;
    
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    /* Ignore SIGPIPE now so that, if the helper server fails to start,
     * nginx doesn't get killed by the default SIGPIPE handler upon
     * writing the password to the helper server.
     */
    ignore_sigpipe();
    
    
    /* Build strings that we need later. */
    
    ngx_memzero(helper_server_filename, sizeof(helper_server_filename));
    ngx_snprintf(helper_server_filename, sizeof(helper_server_filename),
                     "%s/ext/nginx/HelperServer",
                     main_conf->root_dir.data);
    
    ngx_memzero(log_level_string, sizeof(log_level_string));
    ngx_snprintf(log_level_string, sizeof(log_level_string), "%d",
                 (int) main_conf->log_level);
    
    ngx_memzero(max_pool_size_string, sizeof(max_pool_size_string));
    ngx_snprintf(max_pool_size_string, sizeof(max_pool_size_string), "%d",
                 (int) main_conf->max_pool_size);
    
    ngx_memzero(max_instances_per_app_string, sizeof(max_instances_per_app_string));
    ngx_snprintf(max_instances_per_app_string, sizeof(max_instances_per_app_string), "%d",
                 (int) main_conf->max_instances_per_app);
    
    ngx_memzero(pool_idle_time_string, sizeof(pool_idle_time_string));
    ngx_snprintf(pool_idle_time_string, sizeof(pool_idle_time_string), "%d",
                 (int) main_conf->pool_idle_time);
    
    ngx_memzero(user_switching_string, sizeof(user_switching_string));
    ngx_snprintf(user_switching_string, 1, "%d",
                 (int) main_conf->user_switching);
    
    ngx_memzero(worker_uid_string, sizeof(worker_uid_string));
    ngx_snprintf(worker_uid_string, sizeof(worker_uid_string), "%L",
                 (long long) ccf->user);
    
    ngx_memzero(worker_gid_string, sizeof(worker_gid_string));
    ngx_snprintf(worker_gid_string, sizeof(worker_gid_string), "%L",
                 (long long) ccf->group);
    
    /* Generate random password for the helper server. */
    
    f = fopen("/dev/urandom", "r");
    if (f == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not generate a random password for the "
                      "Passenger helper server: cannot open /dev/urandom");
        return NGX_ERROR;
    }
    ngx_memzero(helper_server_password_data, HELPER_SERVER_PASSWORD_SIZE);
    if (fread(helper_server_password_data, 1, HELPER_SERVER_PASSWORD_SIZE, f)
              != HELPER_SERVER_PASSWORD_SIZE) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not generate a random password for the "
                      "Passenger helper server: cannot read sufficient "
                      "data from /dev/urandom");
        return NGX_ERROR;
    }
    fclose(f);
    passenger_helper_server_password.data = helper_server_password_data;
    passenger_helper_server_password.len  = HELPER_SERVER_PASSWORD_SIZE;
    
    
    /* Now spawn the helper server. */
    if (pipe(admin_pipe) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not start the Passenger helper server: pipe() failed");
        return NGX_ERROR;
    }
    if (pipe(feedback_pipe) == -1) {
        close(admin_pipe[0]);
        close(admin_pipe[1]);
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "could not start the Passenger helper server: pipe() failed");
        return NGX_ERROR;
    }
    
    pid = fork();
    switch (pid) {
    case 0:
        /* Child process. */
        close(admin_pipe[1]);
        close(feedback_pipe[0]);
        
        /* Nginx redirects stderr to the error log file. Make sure that
         * stdout is redirected to the error log file as well.
         */
        dup2(2, 1);
        
        /* Close all file descriptors except stdin, stdout, stderr and
         * the reader part of the pipe we just created. 
         */
        if (dup2(admin_pipe[0], 3) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "could not start the Passenger helper server: "
                          "dup2() failed");
        }
        if (dup2(feedback_pipe[1], 4) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "could not start the Passenger helper server: "
                          "dup2() failed");
        }
        for (i = sysconf(_SC_OPEN_MAX) - 1; i > 4; i--) {
            close(i);
        }
        
        /* It seems that Nginx's Perl module unsets the
         * PASSENGER_INSTANCE_TEMP_DIR environment variable, so here
         * we set it again.
         */
        setenv("PASSENGER_INSTANCE_TEMP_DIR", passenger_temp_dir, 1);
        
        execlp((const char *) helper_server_filename,
               "PassengerNginxHelperServer",
               main_conf->root_dir.data,
               main_conf->ruby.data,
               "3",  /* Admin pipe file descriptor number. */
               "4",  /* Feedback pipe file descriptor number. */
               log_level_string,
               max_pool_size_string,
               max_instances_per_app_string,
               pool_idle_time_string,
               user_switching_string,
               main_conf->default_user.data,
               worker_uid_string,
               worker_gid_string,
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
        close(admin_pipe[0]);
        close(admin_pipe[1]);
        close(feedback_pipe[0]);
        close(feedback_pipe[1]);
        return NGX_ERROR;
        
    default:
        /* Parent process. */
        close(admin_pipe[0]);
        close(feedback_pipe[1]);
        
        /* Pass our auto-generated password to the helper server. */
        i = 0;
        do {
            ret = write(admin_pipe[1], helper_server_password_data,
                        HELPER_SERVER_PASSWORD_SIZE - i);
            if (ret == -1) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "could not send password to the Passenger helper server: "
                              "write() failed");
                close(admin_pipe[1]);
                kill(pid, SIGTERM);
                waitpid(pid, NULL, 0);
                return NGX_ERROR;
            } else {
                i += ret;
            }
        } while (i < HELPER_SERVER_PASSWORD_SIZE);
        
        /* Wait until the HelperServer has done initializing. */
        do {
            /* We must do something with read()'s return value
             * because otherwise on some platform it might raise a
             * compile warning.
             */
            ret = read(feedback_pipe[0], &buf, 1);
        } while (ret == -1 && errno == EINTR);
        close(feedback_pipe[0]);
        
        /* Create the file passenger_temp_dir + "/control_process.pid"
         * and make it writable by the worker processes. This is because
         * save_master_process_pid is run after Nginx has lowered privileges.
         */
        last = ngx_snprintf(filename, sizeof(filename) - 1,
                            "%s/control_process.pid", passenger_temp_dir);
        *last = (u_char) '\0';
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
                ret = fchown(fileno(f), ccf->user, ccf->group);
            } while (ret == -1 && errno == EINTR);
            fclose(f);
        } else {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "could not create %s", filename);
        }
        
        helper_server_pid        = pid;
        helper_server_admin_pipe = admin_pipe[1];
        break;
    }
    
    return NGX_OK;
}

/**
 * Save the Nginx master process's PID into a file under the Phusion Passenger
 * temp directory.
 *
 * A bug/limitation in Nginx doesn't allow us to initialize the temp dir *after*
 * Nginx has daemonized, so the temp dir's filename contains Nginx's PID before
 * daemonization. Normally PhusionPassenger::AdminTools::ControlProcess (used
 * by e.g. passenger-status) will think that the temp dir is stale because the
 * PID in the filename doesn't exist. This PID file tells AdminTools::ControlProcess
 * what the actual PID is.
 */
static ngx_int_t
save_master_process_pid(ngx_cycle_t *cycle) {
    u_char filename[NGX_MAX_PATH];
    u_char *last;
    FILE *f;
    
    last = ngx_snprintf(filename, sizeof(filename) - 1,
        "%s/control_process.pid", passenger_temp_dir);
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

static void
shutdown_helper_server(ngx_cycle_t *cycle)
{
    time_t begin_time;
    int    helper_server_exited, ret;
    u_char command[NGX_MAX_PATH + 10];
    
    /* We write one byte to the admin pipe, doesn't matter what the byte is.
     * The helper server will detect this as an exit command.
     */
    do {
        ret = write(helper_server_admin_pipe, "x", 1);
    } while ((ret == -1 && errno == EINTR) || ret == 0);
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

    
    /* Remove Passenger temp dir. */
    ngx_memzero(command, sizeof(command));
    if (ngx_snprintf(command, sizeof(command), "chmod -R u=rwx \"%s\"",
                     passenger_temp_dir) != NULL) {
        do {
            ret = system((const char *) command);
        } while (ret == -1 && errno == EINTR);
    }
    
    ngx_memzero(command, sizeof(command));
    if (ngx_snprintf(command, sizeof(command), "rm -rf \"%s\"",
                     passenger_temp_dir) != NULL) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cycle->log, 0,
                       "Removing Passenger temp folder with command: %s",
                       command);
        errno = 0;
        if (system((const char *) command) != 0) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "Could not remove Passenger temp folder '%s'",
                          passenger_temp_dir);
        }
    }
}

static ngx_int_t
ngx_http_scgi_script_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                *p;
    passenger_loc_conf_t  *slcf;

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
    ngx_int_t   ret;
    const char *system_temp_dir;
    u_char      command[NGX_MAX_PATH + 30];
    
    ngx_memzero(&passenger_main_conf, sizeof(passenger_main_conf_t));
    
    passenger_schema_string.data = (u_char *) "passenger://";
    passenger_schema_string.len  = sizeof("passenger://") - 1;
    
    passenger_stat_cache = cached_multi_file_stat_new(1024);
    
    ret = add_variables(cf);
    if (ret != NGX_OK) {
        return ret;
    }
    
    /* Setup Passenger temp folder. */
    
    system_temp_dir = getenv("TMPDIR");
    if (!system_temp_dir || !*system_temp_dir) {
        system_temp_dir = "/tmp";
    }
    
    ngx_memzero(&passenger_temp_dir, sizeof(passenger_temp_dir));
    if (ngx_snprintf((u_char *) passenger_temp_dir, sizeof(passenger_temp_dir),
                     "%s/passenger.%d", system_temp_dir, getpid()) == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                      "could not create Passenger temp dir string");
        return NGX_ERROR;
    }
    setenv("PASSENGER_INSTANCE_TEMP_DIR", passenger_temp_dir, 1);
    
    /* Temporarily create this temp directory. It must exist before the configuration is loaded,
     * because during Configuration loading Nginx's upstream module will attempt to create
     * the directory passenger_temp_dir + "/webserver_private".
     *
     * passenger_temp_dir will be removed and recreated by the HelperServer, with the right
     * permissions. So right now we don't have to pay any attention to the permissions.
     */
    ngx_memzero(command, sizeof(command));
    ngx_snprintf(command, sizeof(command), "mkdir -p \"%s\"", passenger_temp_dir);
    do {
        ret = system((const char *) command);
    } while (ret == -1 && errno == EINTR);
    
    /* Build helper server socket filename string. */
    
    if (ngx_snprintf((u_char *) passenger_helper_server_socket, NGX_MAX_PATH,
                     "unix:%s/master/helper_server.sock",
                     passenger_temp_dir) == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                      "could not create Passenger helper server "
                      "socket filename string");
        return NGX_ERROR;
    }
    
    return NGX_OK;
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
    start_helper_server,                    /* init module */
    save_master_process_pid,                /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    shutdown_helper_server,                 /* exit master */
    NGX_MODULE_V1_PADDING
};
