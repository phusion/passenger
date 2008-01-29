#ifdef INSIDE_MOD_RAILS

#include <unistd.h>
#include "DispatcherBucket.h"

static int
file_exists(apr_pool_t *pool, const char *filename) {
	apr_finfo_t info;
	return apr_stat(&info, filename, APR_FINFO_NORM, pool) == APR_SUCCESS;
}

static INLINE bool
is_well_formed_uri(const char *uri) {
	return uri[0] == '/';
}

/**
 * Check whether config->base_uri is a base URI of the URI of the given request.
 */
static INLINE bool
inside_base_uri(request_rec *r, RailsConfig *config) {
	return strcmp(r->uri, config->base_uri) == 0
	    || strncmp(r->uri, config->base_uri_with_slash, strlen(config->base_uri_with_slash)) == 0;
}

static INLINE const char *
determine_rails_dir(request_rec *r, RailsConfig *config) {
	size_t len = strlen(r->filename) - strlen(r->uri);
	if (len <= 0) {
		return NULL;
	} else {
		return apr_pstrndup(r->pool, r->filename, len);
	}
}

static INLINE bool
verify_rails_dir(apr_pool_t *pool, const char *dir) {
	return file_exists(pool, apr_pstrcat(pool, dir, "/../config/environment.rb", NULL));
}


static int
mod_rails_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *base_server) {
	pid_t pid;
	int fds[2];

	socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
	pid = fork();
	if (pid == 0) {
		pid = fork();
		if (pid == 0) {
			char fd_string[20];

			close(fds[0]);
			apr_snprintf(fd_string, sizeof(fd_string), "%d", fds[1]);
			execlp("ruby", "ruby", "/home/hongli/Projects/mod_rails/lib/mod_rails/spawn_manager.rb", fd_string, NULL);
			_exit(1);
		} else if (pid == -1) {
			ap_log_error(APLOG_MARK, APLOG_STARTUP, 0, NULL, "mod_rails: Unable to fork a process: %s", strerror(errno));
			_exit(0);
		} else {
			_exit(0);
		}
	} else if (pid == -1) {
		close(fds[0]);
		close(fds[1]);
		ap_log_error(APLOG_MARK, APLOG_STARTUP, 0, NULL, "mod_rails: Unable to fork a process: %s", strerror(errno));
		return DECLINED;
	} else {
		close(fds[1]);
		waitpid(pid, NULL, 0);
		ap_add_version_component(p, "mod_rails/" MOD_RAILS_VERSION);
		return OK;
	}
}

static void
spawn_instance(int pipes[2]) {
	int p1[2], p2[2];
	pipe(p1);
	pipe(p2);
	pid_t pid = fork();
	if (pid == 0) {
		pid = fork();
		if (pid == 0) {
			dup2(p1[0], 0);
			dup2(p2[1], 1);
			close(p1[0]);
			close(p1[1]);
			close(p2[0]);
			close(p2[1]);
			execlp("ruby", "ruby", "/home/hongli/Projects/mod_rails/handler_demo.rb", NULL);
			FILE *f;
			_exit(0);
		} else {
			_exit(0);
		}
	} else {
		close(p1[0]);
		close(p2[1]);
		waitpid(pid, NULL, 0);
		pipes[0] = p2[0];
		pipes[1] = p1[1];
	}
}

static void
debug(const char *format, ...) {
	va_list ap;
	char message[1024];
	
	va_start(ap, format);
	int size = apr_vsnprintf(message, sizeof(message), format, ap);
	FILE *f = fopen("/dev/pts/2", "w");
	if (f != NULL) {
		fwrite(message, 1, size, f);
		fclose(f);
	}
	va_end(ap);
}

static int
mod_rails_handle_request(request_rec *r) {
	/* The main request handler hook function.
	 */

	RailsConfig *config = get_config(r);
	const char *rails_dir;
	
	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL
	 || !inside_base_uri(r, config) || r->filename == NULL || file_exists(r->pool, r->filename)) {
		return DECLINED;
	}
	
	rails_dir = determine_rails_dir(r, config);
	if (rails_dir == NULL) {
		ap_set_content_type(r, "text/html; charset=UTF-8");
		ap_rputs("<h1>mod_rails error #1</h1>\n", r);
		ap_rputs("Cannot determine the location of the Rails application's \"public\" directory.", r);
		return OK;
	} else if (!verify_rails_dir(r->pool, rails_dir)) {
		ap_set_content_type(r, "text/html; charset=UTF-8");
		ap_rputs("<h1>mod_rails error #2</h1>\n", r);
		ap_rputs("mod_rails thinks that the Rails application's \"public\" directory is \"", r);
		ap_rputs(ap_escape_html(r->pool, rails_dir), r);
		ap_rputs("\", but it doesn't seem to be valid.", r);
		return OK;
	}
	
	apr_bucket_brigade *bb;
	apr_bucket *b;
	int p[2];
	uint16_t x;
	
	bb = apr_brigade_create(r->connection->pool, r->connection->bucket_alloc);
	
	spawn_instance(p);
	x = 0;
	write(p[1], &x, sizeof(x));
	close(p[1]);
	debug("hooks: %d, %d\n", p[0], p[1]);
	
	b = dispatcher_bucket_create(r->pool, p[0], r->server->timeout, r->connection->bucket_alloc);
	APR_BRIGADE_INSERT_TAIL(bb, b);
	
	b = apr_bucket_eos_create(r->connection->bucket_alloc);
	APR_BRIGADE_INSERT_TAIL(bb, b);
	
	ap_scan_script_header_err_brigade(r, bb, NULL);
	ap_pass_brigade(r->output_filters, bb);
	
	return OK;
}

static int
mod_rails_map_to_storage(request_rec *r) {
	RailsConfig *config = get_config(r);
	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL || !inside_base_uri(r, config)) {
		return DECLINED;
	} else {
		char *html_file = apr_pstrcat(r->pool, r->filename, ".html", NULL);
		if (file_exists(r->pool, html_file)) {
			/* If a .html version of the URI exists, serve it directly.
			 * This is used by page caching.
			 */
			r->filename = html_file;
			r->canonical_filename = html_file;
			return DECLINED;
		} else {
			/* Apache's default map_to_storage process does strange things with the filename.
			 * Suppose that the DocumentRoot is /website, on server http://test.com/. If we access
			 * http://test.com/foo/bar, and /website/foo/bar does not exist, then Apache will
			 * change the filename to /website/foo instead of the expected /website/bar.
			 * We make sure that doesn't happen.
			 *
			 * Incidentally, this also disables mod_rewrite. That is a good thing because
			 * the default Rails .htaccess file interferes with mod_rails anyway.
			 */
			return OK;
		}
	}
}

static void mod_rails_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(mod_rails_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_map_to_storage(mod_rails_map_to_storage, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(mod_rails_handle_request, NULL, NULL, APR_HOOK_FIRST);
}

#endif /* INSIDE_MOD_RAILS */
