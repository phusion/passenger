/*
 * A few functions are based on the code from mod_scgi 1.9. mod_scgi is
 * Copyright (c) 2004 Corporation for National Research Initiatives; All Rights Reserved
 */

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
#include <apr_lib.h>

#include <exception>
#include <unistd.h>

#include "Hooks.h"
#include "Configuration.h"
#include "Utils.h"
#include "ApplicationPoolClientServer.h"
#include "MessageChannel.h"

using namespace std;
using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA rails_module;

#define DEFAULT_RUBY_COMMAND "ruby"
#define DEFAULT_RAILS_ENV "production"
#define DEFAULT_SPAWN_SERVER_COMMAND "/home/hongli/Projects/mod_rails/lib/mod_rails/spawn_manager.rb"


class Hooks {
private:
	struct Container {
		Application::SessionPtr session;
		
		static apr_status_t cleanup(void *p) {
			delete (Container *) p;
			return APR_SUCCESS;
		}
	};

	ApplicationPoolServerPtr applicationPoolServer;
	ApplicationPoolPtr applicationPool;
	
	DirConfig *getDirConfig(request_rec *r) {
		return (DirConfig *) ap_get_module_config(r->per_dir_config, &rails_module);
	}
	
	ServerConfig *getServerConfig(server_rec *s) {
		return (ServerConfig *) ap_get_module_config(s->module_config, &rails_module);
	}
	
	int fileExists(apr_pool_t *pool, const char *filename) {
		apr_finfo_t info;
		return apr_stat(&info, filename, APR_FINFO_NORM, pool) == APR_SUCCESS && info.filetype == APR_REG;
	}
	
	const char *determineRailsBaseURI(request_rec *r, DirConfig *config) {
		set<string>::const_iterator it;
		const char *uri = r->uri;
		size_t uri_len = strlen(uri);
		
		if (uri_len == 0 || uri[0] != '/') {
			return NULL;
		}
		
		for (it = config->base_uris.begin(); it != config->base_uris.end(); it++) {
			const string &base(*it);
			if (  base == "/"
			 || ( uri_len == base.size() && memcmp(uri, base.c_str(), uri_len) == 0 )
			 || ( uri_len  > base.size() && memcmp(uri, base.c_str(), base.size()) == 0 && uri[base.size()] == '/' )) {
				return apr_pstrdup(r->pool, base.c_str());
			}
		}
		return NULL;
	}
	
	const char *determineRailsDir(request_rec *r, const char *baseURI) {
		const char *docRoot = ap_document_root(r);
		size_t len = strlen(docRoot);
		if (len > 0) {
			string temp;
			if (docRoot[len - 1] == '/') {
				temp.assign(docRoot, len - 1);
			} else {
				temp.assign(docRoot, len);
			}
			temp.append(baseURI);
			return apr_pstrdup(r->pool, temp.c_str());
		} else {
			return NULL;
		}
	}
	
	bool verifyRailsDir(apr_pool_t *pool, const char *dir) {
		string temp(dir);
		temp.append("/../config/environment.rb");
		return fileExists(pool, temp.c_str());
	}
	
	char *http2env(apr_pool_t *p, const char *name) {
		char *env_name = apr_pstrcat(p, "HTTP_", name, NULL);
		char *cp;
		
		for (cp = env_name + 5; *cp != 0; cp++) {
			if (*cp == '-') {
				*cp = '_';
			} else {
				*cp = apr_toupper(*cp);
			}
		}
		
		return env_name;
	}
	
	char *lookupName(apr_table_t *t, const char *name) {
		const apr_array_header_t *hdrs_arr = apr_table_elts(t);
		apr_table_entry_t *hdrs = (apr_table_entry_t *) hdrs_arr->elts;
		int i;
		
		for (i = 0; i < hdrs_arr->nelts; ++i) {
			if (hdrs[i].key == NULL) {
				continue;
			}
			if (strcasecmp(hdrs[i].key, name) == 0) {
				return hdrs[i].val;
			}
		}
		return NULL;
	}
	
	char *lookupHeader(request_rec *r, const char *name) {
		return lookupName(r->headers_in, name);
	}
	
	char *lookupEnv(request_rec *r, const char *name) {
		return lookupName(r->subprocess_env, name);
	}
	
	// This code is a duplicate of what's in util_script.c.  We can't use
	// r->unparsed_uri because it gets changed if there was a redirect.
	char *originalURI(request_rec *r) {
		char *first, *last;

		if (r->the_request == NULL) {
			return (char *) apr_pcalloc(r->pool, 1);
		}
		
		first = r->the_request;	// use the request-line
		
		while (*first && !apr_isspace(*first)) {
			++first;		// skip over the method
		}
		while (apr_isspace(*first)) {
			++first;		//   and the space(s)
		}
		
		last = first;
		while (*last && !apr_isspace(*last)) {
			++last;			// end at next whitespace
		}
		
		return apr_pstrmemdup(r->pool, first, last - first);
	}

	void addHeader(apr_table_t *table, const char *name, const char *value) {
		if (name != NULL && value != NULL) {
			apr_table_addn(table, name, value);
		}
	}
	
	apr_status_t sendHeaders(request_rec *r, Application::SessionPtr &session, const char *baseURI) {
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
		addHeader(headers, "REQUEST_URI",     originalURI(r));
		addHeader(headers, "QUERY_STRING",    r->args ? r->args : "");
		addHeader(headers, "SCRIPT_NAME",     baseURI);
		addHeader(headers, "HTTPS",           lookupEnv(r, "HTTPS"));
		addHeader(headers, "CONTENT_TYPE",    lookupHeader(r, "Content-type"));
		addHeader(headers, "DOCUMENT_ROOT",   ap_document_root(r));
		
		// Set HTTP headers.
		const apr_array_header_t *hdrs_arr;
		apr_table_entry_t *hdrs;
		int i;
		
		hdrs_arr = apr_table_elts(r->headers_in);
		hdrs = (apr_table_entry_t *) hdrs_arr->elts;
		for (i = 0; i < hdrs_arr->nelts; ++i) {
			if (hdrs[i].key) {
				addHeader(headers, http2env(r->pool, hdrs[i].key), hdrs[i].val);
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
		
		// Now send the headers.
		string buffer;
		
		hdrs_arr = apr_table_elts(headers);
    		hdrs = (apr_table_entry_t*) hdrs_arr->elts;
    		buffer.reserve(1024 * 4);
		for (i = 0; i < hdrs_arr->nelts; ++i) {
			buffer.append(hdrs[i].key);
			buffer.append(1, '\0');
			buffer.append(hdrs[i].val);
			buffer.append(1, '\0');
		}
		session->sendHeaders(buffer);
		return APR_SUCCESS;
	}
	
	apr_status_t sendRequestBody(request_rec *r, Application::SessionPtr &session) {
		if (ap_should_client_block(r)) {
			char buf[1024 * 32];
			apr_off_t len;
			
			while ((len = ap_get_client_block(r, buf, sizeof(buf))) > 0) {
				session->sendBodyBlock(buf, len);
			}
			if (len == -1) {
				return HTTP_INTERNAL_SERVER_ERROR;
			}
		}
		session->closeWriter();
		return APR_SUCCESS;
	}

public:
	Hooks(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
		initDebugging();
		ap_add_version_component(pconf, "Phusion_Passenger/" PASSENGER_VERSION);
		passenger_config_merge_all_servers(pconf, s);
		
		ServerConfig *config = getServerConfig(s);
		const char *ruby, *environment, *spawnServer;
		
		ruby = (config->ruby != NULL) ? config->ruby : DEFAULT_RUBY_COMMAND;
		environment = (config->env != NULL) ? config->env : DEFAULT_RAILS_ENV;
		spawnServer = (config->spawnServer != NULL) ? config->spawnServer : DEFAULT_SPAWN_SERVER_COMMAND;
		
		applicationPoolServer = ptr(new ApplicationPoolServer(spawnServer, "", environment, ruby));
	}
	
	void initChild(apr_pool_t *pchild, server_rec *s) {
		// TODO: check for exceptions here
		applicationPool = applicationPoolServer->connect();
		applicationPoolServer->detach();
	}
	
	int handleRequest(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		const char *railsBaseURI = determineRailsBaseURI(r, config);
		if (railsBaseURI == NULL || r->filename == NULL || fileExists(r->pool, r->filename)) {
			return DECLINED;
		}
		
		const char *railsDir = determineRailsDir(r, railsBaseURI);
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
		
		int httpStatus = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR);
    		if (httpStatus != OK) {
			return httpStatus;
		}
		
		/*
		 * TODO:
		 * - If the request handler dies, it does not get removed from the application pool. It should.
		 * - Implement HTTP body forwarding
		 */
		
		try {
			apr_bucket_brigade *bb;
			apr_bucket *b;
			
			P_DEBUG("Processing HTTP request: " << r->uri);
			Application::SessionPtr session(applicationPool->get(string(railsDir) + "/.."));
			sendHeaders(r, session, railsBaseURI);
			sendRequestBody(r, session);
			
			apr_file_t *readerPipe = NULL;
			int reader = session->getReader();
			apr_os_pipe_put(&readerPipe, &reader, r->pool);

			bb = apr_brigade_create(r->connection->pool, r->connection->bucket_alloc);
			b = apr_bucket_pipe_create(readerPipe, r->connection->bucket_alloc);
			APR_BRIGADE_INSERT_TAIL(bb, b);

			b = apr_bucket_eos_create(r->connection->bucket_alloc);
			APR_BRIGADE_INSERT_TAIL(bb, b);

			ap_scan_script_header_err_brigade(r, bb, NULL);
			ap_pass_brigade(r->output_filters, bb);
			
			Container *container = new Container();
			container->session = session;
			apr_pool_cleanup_register(r->pool, container, Container::cleanup, apr_pool_cleanup_null);

			return OK;
		} catch (const exception &e) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "*** Passenger: uncaught error: %s", e.what());
			return HTTP_INTERNAL_SERVER_ERROR;
		}
	}
	
	int
	mapToStorage(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		if (determineRailsBaseURI(r, config) == NULL
		 || fileExists(r->pool, r->filename)) {
			/*
			 * fileExists():
			 * If the file already exists, serve it directly.
			 * This is for static assets like .css and .js files.
			 */
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
				 * interferes with mod_rails anyway (it delegates requests
				 * to the CGI script dispatch.cgi).
				 */
				return OK;
			}
		}
	}
};



/******************************************************************
 * Below follows lightweight C wrappers around the C++ Hook class.
 ******************************************************************/

/**
 * @ingroup Hooks
 * @{
 */

static Hooks *hooks = NULL;

static apr_status_t
destroy_hooks(void *arg) {
	delete hooks;
	return APR_SUCCESS;
}

static int
init_module(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
	/*
	 * 1. Apache on Unix calls the post_config hook twice, once before detach() and once
	 *    after. On Windows it never calls detach().
	 * 2. When Apache is compiled to use DSO modules, the modules are unloaded between the
	 *    two post_config hook calls.
	 * 3. On Unix, if the -X commandline option is given, detach() will not be called.
	 *
	 * Because of these 3 issues (and especially #2), we only want to intialize the second
	 * time the post_config hook is called.
	 */
	void *firstInitCall = NULL;
	apr_pool_t *processPool = s->process->pool;
	
	apr_pool_userdata_get(&firstInitCall, "mod_passenger", processPool);
	if (firstInitCall == NULL) {
		apr_pool_userdata_set((const void *) 1, "mod_passenger",
			apr_pool_cleanup_null, processPool);
		return OK;
	} else {
		try {
			hooks = new Hooks(pconf, plog, ptemp, s);
			apr_pool_cleanup_register(pconf, NULL,
				destroy_hooks,
				apr_pool_cleanup_null);
			return OK;
		} catch (const exception &e) {
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
				"*** Passenger could not be initialized because of this error: %s",
				e.what());
			hooks = NULL;
			return DECLINED;
		}
	}
}

static void
init_child(apr_pool_t *pchild, server_rec *s) {
	if (hooks != NULL) {
		return hooks->initChild(pchild, s);
	}
}

static int
handle_request(request_rec *r) {
	if (hooks != NULL) {
		return hooks->handleRequest(r);
	} else {
		return DECLINED;
	}
}

static int
map_to_storage(request_rec *r) {
	if (hooks != NULL) {
		return hooks->mapToStorage(r);
	} else {
		return DECLINED;
	}
}

/**
 * Apache hook registration function.
 */
void
passenger_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(init_module, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_child_init(init_child, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_map_to_storage(map_to_storage, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(handle_request, NULL, NULL, APR_HOOK_FIRST);
}

/**
 * @}
 */
