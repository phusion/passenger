#include <ap_config.h>
#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_request.h>
#include <http_protocol.h>
#include <http_log.h>
#include <util_script.h>
#include <apr_pools.h>
#include <apr_strings.h>

#include <exception>
#include <unistd.h>

#include "Hooks.h"
#include "Types.h"
#include "Utils.h"
#include "DispatcherBucket.h"
#include "ApplicationPool.h"
#include "MessageChannel.h"

using namespace std;
using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA rails_module;


class Hooks {
private:
	static ApplicationPoolPtr applicationPool;

	RailsConfig *getConfig(request_rec *r) {
		return (RailsConfig *) ap_get_module_config(r->per_dir_config, &rails_module);
	}

	int fileExists(apr_pool_t *pool, const char *filename) {
		apr_finfo_t info;
		return apr_stat(&info, filename, APR_FINFO_NORM, pool) == APR_SUCCESS;
	}
	
	bool isWellFormedURI(const char *uri) {
		return uri[0] == '/';
	}
	
	/**
	 * Check whether config->base_uri is a base URI of the URI of the given request.
	 */
	bool insideBaseURI(request_rec *r, RailsConfig *config) {
		return strcmp(r->uri, config->base_uri) == 0
		    || strncmp(r->uri, config->base_uri_with_slash, strlen(config->base_uri_with_slash)) == 0;
	}
	
	const char *determineRailsDir(request_rec *r, RailsConfig *config) {
		size_t len = strlen(r->filename) - strlen(r->uri);
		if (len <= 0) {
			return NULL;
		} else {
			return apr_pstrndup(r->pool, r->filename, len);
		}
	}
	
	bool verifyRailsDir(apr_pool_t *pool, const char *dir) {
		return fileExists(pool, apr_pstrcat(pool, dir, "/../config/environment.rb", NULL));
	}

	void addHeader(apr_table_t *table, const char *name, const char *value) {
		if (name != NULL && value != NULL) {
			apr_table_addn(table, name, value);
		}
	}
	
	apr_status_t sendHeaders(request_rec *r, int fd) {
		apr_table_t *headers;
		headers = apr_table_make(r->pool, 40);
		if (headers == NULL) {
			return APR_ENOMEM;
		}
		
		// Set standard CGI variables.
		addHeader(headers, "SERVER_SOFTWARE", ap_get_server_version());
		addHeader(headers, "SERVER_PROTOCOL", r->protocol);
		addHeader(headers, "SERVER_NAME",     ap_get_server_name(r));
		addHeader(headers, "SERVER_ADMIN",    r->server->server_admin);
		addHeader(headers, "SERVER_ADDR",     r->connection->local_ip);
		addHeader(headers, "SERVER_PORT",     apr_psprintf(r->pool, "%u", ap_get_server_port(r)));
		addHeader(headers, "REMOTE_ADDR",     r->connection->remote_ip);
		addHeader(headers, "REMOTE_PORT",     apr_psprintf(r->pool, "%d", r->connection->remote_addr->port));
		addHeader(headers, "REMOTE_USER",     r->user);
		addHeader(headers, "REQUEST_METHOD",  r->method);
		//addHeader(headers, "REQUEST_URI",     original_uri(r));
		addHeader(headers, "QUERY_STRING",    r->args ? r->args : "");
		addHeader(headers, "SCRIPT_NAME",     r->uri);
		if (r->path_info) {
			int path_info_start = strlen(r->uri) - strlen(r->path_info);
			ap_assert(path_info_start >= 0);
			addHeader(headers, "SCRIPT_NAME", apr_pstrndup(r->pool, r->uri, path_info_start));
			addHeader(headers, "PATH_INFO",   r->path_info);
		} else {
			addHeader(headers, "SCRIPT_NAME", r->uri);
		}
		//addHeader(headers, "HTTPS",           lookup_env(r, "HTTPS"));
		//addHeader(headers, "CONTENT_TYPE",    lookup_header(r, "Content-type"));
		addHeader(headers, "DOCUMENT_ROOT",   ap_document_root(r));
	
		// Set HTTP headers.
		const apr_array_header_t *hdrs_arr;
		apr_table_entry_t *hdrs;
		int i;
		
		hdrs_arr = apr_table_elts(r->headers_in);
		hdrs = (apr_table_entry_t *) hdrs_arr->elts;
		for (i = 0; i < hdrs_arr->nelts; ++i) {
			if (hdrs[i].key) {
				//addHeader(headers, http2env(r->pool, hdrs[i].key), hdrs[i].val);
			}
		}
	
		// Add other environment variables.
		const apr_array_header_t *env_arr;
		apr_table_entry_t *env;
		
		env_arr = apr_table_elts(r->subprocess_env);
		env = (apr_table_entry_t*) env_arr->elts;
		for (i = 0; i < env_arr->nelts; ++i) {
			addHeader(headers, env[i].key, env[i].val);
		}
		
		MessageChannel channel(fd);
		hdrs_arr = apr_table_elts(headers);
    		hdrs = (apr_table_entry_t*) hdrs_arr->elts;
		for (i = 0; i < hdrs_arr->nelts; ++i) {
			// TODO: Internally, this creates a new list every time.
			// It can be optimized by using an array or a vector.
			channel.write(hdrs[i].key, hdrs[i].val, NULL);
		}
		channel.write("", NULL);
	
		return APR_SUCCESS;
	}

	void
	debug(const char *format, ...) {
		va_list ap;
		char message[1024];
		
		va_start(ap, format);
		int size = apr_vsnprintf(message, sizeof(message), format, ap);
		FILE *f = fopen("/dev/pts/3", "w");
		if (f != NULL) {
			fwrite(message, 1, size, f);
			fclose(f);
		}
		va_end(ap);
	}

public:
	int
	init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *base_server) {
		initDebugging("/tmp/passenger.txt");
		ap_add_version_component(p, "Phusion_Passenger/" PASSENGER_VERSION);
		const char *spawnManagerCommand = "/home/hongli/Projects/mod_rails/lib/mod_rails/spawn_manager.rb";
		const char *logFile = "/home/hongli/Projects/mod_rails/spawner_log.txt";
		applicationPool = ApplicationPoolPtr(new ApplicationPool(spawnManagerCommand, logFile));
		return OK;
	}
	
	int
	handleRequest(request_rec *r) {
		// The main request handler hook function.
		RailsConfig *config = getConfig(r);
		const char *railsDir;
		
		if (!isWellFormedURI(r->uri)  || config->base_uri == NULL
		 || !insideBaseURI(r, config) || r->filename == NULL
		 || fileExists(r->pool, r->filename)) {
			return DECLINED;
		}
		
		railsDir = determineRailsDir(r, config);
		if (railsDir == NULL) {
			ap_set_content_type(r, "text/html; charset=UTF-8");
			ap_rputs("<h1>mod_rails error #1</h1>\n", r);
			ap_rputs("Cannot determine the location of the Rails application's \"public\" directory.", r);
			return OK;
		} else if (!verifyRailsDir(r->pool, railsDir)) {
			ap_set_content_type(r, "text/html; charset=UTF-8");
			ap_rputs("<h1>mod_rails error #2</h1>\n", r);
			ap_rputs("mod_rails thinks that the Rails application's \"public\" directory is \"", r);
			ap_rputs(ap_escape_html(r->pool, railsDir), r);
			ap_rputs("\", but it doesn't seem to be valid.", r);
			return OK;
		}
		
		/* int httpStatus = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR);
    		if (httpStatus != OK) {
			return httpStatus;
		} */
		
		try {
			apr_bucket_brigade *bb;
			apr_bucket *b;
			
			P_DEBUG("Processing HTTP request: " << r->uri);
			ApplicationPtr app(applicationPool->get(string(railsDir) + "/.."));
			P_DEBUG("Connected to application: reader FD = " << app->getReader() << ", writer FD = " << app->getWriter());
			sendHeaders(r, app->getWriter());

			bb = apr_brigade_create(r->connection->pool, r->connection->bucket_alloc);
			b = dispatcher_bucket_create(r->pool, app,
				r->server->timeout, r->connection->bucket_alloc);
			APR_BRIGADE_INSERT_TAIL(bb, b);

			b = apr_bucket_eos_create(r->connection->bucket_alloc);
			APR_BRIGADE_INSERT_TAIL(bb, b);

			ap_scan_script_header_err_brigade(r, bb, NULL);
			ap_pass_brigade(r->output_filters, bb);
		} catch (const exception &e) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, r, "mod_passenger unknown error: %s", e.what());
		}
		return OK;
	}
	
	int
	mapToStorage(request_rec *r) {
		RailsConfig *config = getConfig(r);
		
		if (!isWellFormedURI(r->uri) || config->base_uri == NULL
		 || !insideBaseURI(r, config)) {
			return DECLINED;
		} else {
			char *html_file = apr_pstrcat(r->pool, r->filename, ".html", NULL);
			if (fileExists(r->pool, html_file)) {
				/* If a .html version of the URI exists, serve it directly.
				 * This is used by page caching.
				 */
				r->filename = html_file;
				r->canonical_filename = html_file;
				return DECLINED;
			} else {
				/* Apache's default map_to_storage process does strange
				 * things with the filename. Suppose that the DocumentRoot
				 * is /website, on server http://test.com/. If we access
				 * http://test.com/foo/bar, and /website/foo/bar does not
				 * exist, then Apache will change the filename to
				 * /website/foo instead of the expected /website/bar.
				 * We make sure that doesn't happen.
				 *
				 * Incidentally, this also disables mod_rewrite. That is a
				 * good thing because the default Rails .htaccess file
				 * interferes with mod_rails anyway (it delegates request
				 * to the CGI script dispatch.cgi).
				 */
				return OK;
			}
		}
	}
};


ApplicationPoolPtr Hooks::applicationPool;


static int
init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *base_server) {
	return Hooks().init(p, plog, ptemp, base_server);
}

static int
handle_request(request_rec *r) {
	return Hooks().handleRequest(r);
}

static int
map_to_storage(request_rec *r) {
	return Hooks().mapToStorage(r);
}

void
passenger_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_map_to_storage(map_to_storage, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(handle_request, NULL, NULL, APR_HOOK_FIRST);
}
