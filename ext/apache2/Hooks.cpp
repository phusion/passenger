/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include <boost/thread.hpp>

#include <sys/time.h>
#include <sys/resource.h>
#include <exception>
#include <unistd.h>

#include "Hooks.h"
#include "Configuration.h"
#include "Utils.h"
#include "Logging.h"
#include "ApplicationPoolServer.h"
#include "MessageChannel.h"

using namespace std;
using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA passenger_module;

#define DEFAULT_RUBY_COMMAND "ruby"
#define DEFAULT_RAILS_ENV "production"


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
		return (DirConfig *) ap_get_module_config(r->per_dir_config, &passenger_module);
	}
	
	ServerConfig *getServerConfig(server_rec *s) {
		return (ServerConfig *) ap_get_module_config(s->module_config, &passenger_module);
	}
	
	/**
	 * Determine whether the given HTTP request falls under one of the specified
	 * RailsBaseURIs. If yes, then the first matching base URI will be returned.
	 *
	 * If Rails autodetection was enabled in the configuration, then this function
	 * will return '/' if the document root seems to be a valid Rails 'public' folder.
	 *
	 * Otherwise, NULL will be returned.
	 */
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
			 || ( uri_len  > base.size() && memcmp(uri, base.c_str(), base.size()) == 0
			                             && uri[base.size()] == '/' )
			) {
				return apr_pstrdup(r->pool, base.c_str());
			}
		}
		
		if ((config->autoDetect == DirConfig::ENABLED || config->autoDetect == DirConfig::UNSET)
		 && verifyRailsDir(ap_document_root(r))) {
			return "/";
		}
		
		return NULL;
	}
	
	string determineRailsDir(request_rec *r, const char *baseURI) {
		const char *docRoot = ap_document_root(r);
		size_t len = strlen(docRoot);
		if (len > 0) {
			string path;
			if (docRoot[len - 1] == '/') {
				path.assign(docRoot, len - 1);
			} else {
				path.assign(docRoot, len);
			}
			if (strcmp(baseURI, "/") != 0) {
				path.append(baseURI);
			}
			return path;
		} else {
			return "";
		}
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
		if (strcmp(baseURI, "/") != 0) {
			addHeader(headers, "SCRIPT_NAME",     baseURI);
		}
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
		
		/*
		 * If the last header value is an empty string, then the buffer
		 * will end with "\0\0". For example, if 'SSLOptions +ExportCertData'
		 * is set, and there's no client certificate, and 'SSL_CLIENT_CERT'
		 * is the last header, then the buffer will end with:
		 *
		 *   "SSL_CLIENT_CERT\0\0"
		 *
		 * The data in the buffer will be processed by the RequestHandler class,
		 * which is implemented in Ruby. But it uses Hash[*data.split("\0")] to
		 * unserialize the data. Unfortunately String#split will not transform
		 * the trailing "\0\0" into an empty string:
		 *
		 *   "SSL_CLIENT_CERT\0\0".split("\0")
		 *   # => desired result: ["SSL_CLIENT_CERT", ""]
		 *   # => actual result:  ["SSL_CLIENT_CERT"]
		 *
		 * When that happens, Hash[..] will raise an ArgumentError because
		 * data.split("\0") does not return an array with a length that is a
		 * multiple of 2.
		 *
		 * So here, we add a dummy header to prevent situations like that from
		 * happening.
		 */
		buffer.append("_\0_\0", 4);
		
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
		ap_add_version_component(pconf, "Phusion_Passenger/" PASSENGER_VERSION);
		passenger_config_merge_all_servers(pconf, s);
		
		ServerConfig *config = getServerConfig(s);
		const char *ruby, *environment, *user;
		string applicationPoolServerExe, spawnServer;
		
		ruby = (config->ruby != NULL) ? config->ruby : DEFAULT_RUBY_COMMAND;
		environment = (config->env != NULL) ? config->env : DEFAULT_RAILS_ENV;
		if (config->userSwitching) {
			user = "";
		} else if (config->defaultUser != NULL) {
			user = config->defaultUser;
		} else {
			user = "nobody";
		}
		
		if (config->root == NULL) {
			throw ConfigurationException("The 'PassengerRoot' configuration option "
				"is not specified. This option is required, so please specify it. "
				"TIP: The correct value for this option was given to you by "
				"'passenger-install-apache2-module'.");
		}
		
		spawnServer = findSpawnServer(config->root);
		if (!fileExists(spawnServer.c_str())) {
			string message("The Passenger spawn server script, '");
			message.append(spawnServer);
			message.append("', does not exist. Please check whether the 'PassengerRoot' "
				"option is specified correctly.");
			throw FileNotFoundException(message);
		}
		applicationPoolServerExe = findApplicationPoolServer(config->root);
		if (!fileExists(applicationPoolServerExe.c_str())) {
			string message("The Passenger application pool server, '");
			message.append(applicationPoolServerExe);
			message.append("', does not exist. Please check whether the 'PassengerRoot' "
				"option is specified correctly.");
			throw FileNotFoundException(message);
		}
		
		applicationPoolServer = ptr(
			new ApplicationPoolServer(
				applicationPoolServerExe, spawnServer, "",
				environment, ruby, user)
		);
	}
	
	void initChild(apr_pool_t *pchild, server_rec *s) {
		ServerConfig *config = getServerConfig(s);
		
		try {
			applicationPool = applicationPoolServer->connect();
			applicationPoolServer->detach();
			applicationPool->setMax(config->maxPoolSize);
			applicationPool->setMaxIdleTime(config->poolIdleTime);
		} catch (const exception &e) {
			fprintf(stderr, "*** Cannot initialize Passenger: %s\n", e.what());
			fflush(stderr);
			abort();
		}
	}
	
	int handleRequest(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		const char *railsBaseURI = determineRailsBaseURI(r, config);
		if (railsBaseURI == NULL || r->filename == NULL || fileExists(r->filename)) {
			return DECLINED;
		}
		
		string railsDir(determineRailsDir(r, railsBaseURI));
		if (railsDir.empty()) {
			ap_set_content_type(r, "text/html; charset=UTF-8");
			ap_rputs("<h1>Passenger error #1</h1>\n", r);
			ap_rputs("Cannot determine the location of the Rails application's \"public\" directory.", r);
			return OK;
		} else if (!verifyRailsDir(railsDir)) {
			ap_set_content_type(r, "text/html; charset=UTF-8");
			ap_rputs("<h1>Passenger error #2</h1>\n", r);
			ap_rputs("Passenger thought that the Rails application's \"public\" directory is \"", r);
			ap_rputs(ap_escape_html(r->pool, railsDir.c_str()), r);
			ap_rputs("\". But upon further inspection, it doesn't seem to be a valid Rails ", r);
			ap_rputs("\"public\" folder. It is possible that Apache doesn't have read ", r);
			ap_rputs("permissions to your Rails application's folder. Please check your ", r);
			ap_rputs("file permissions.", r);
			return OK;
		}
		
		int httpStatus = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR);
    		if (httpStatus != OK) {
			return httpStatus;
		}
		
		try {
			apr_bucket_brigade *bb;
			apr_bucket *b;
			Application::SessionPtr session;
			
			P_DEBUG("Processing HTTP request: " << r->uri);
			try {
				const char *defaultUser;
				ServerConfig *sconfig;
				
				sconfig = getServerConfig(r->server);
				if (sconfig->defaultUser != NULL) {
					defaultUser = sconfig->defaultUser;
				} else {
					defaultUser = "nobody";
				}
				session = applicationPool->get(canonicalizePath(railsDir + "/.."),
					true, defaultUser);
			} catch (const SpawnException &e) {
				if (e.hasErrorPage()) {
					ap_set_content_type(r, "text/html; charset=utf-8");
					ap_rputs(e.getErrorPage().c_str(), r);
					// Unfortunately we can't return a 500 Internal Server
					// Error. Apache's HTTP error handler would kick in.
					return OK;
				} else {
					throw;
				}
			}
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
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "*** Unexpected error in Passenger: %s", e.what());
			return HTTP_INTERNAL_SERVER_ERROR;
		}
	}
	
	int
	mapToStorage(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		bool forwardToRails;
		
		if (determineRailsBaseURI(r, config) == NULL
		 || fileExists(r->filename)) {
			/*
			 * fileExists():
			 * If the file already exists, serve it directly.
			 * This is for static assets like .css and .js files.
			 */
			forwardToRails = false;
		} else if (r->method_number == M_GET) {
			char *html_file;
			size_t len;
			
			len = strlen(r->filename);
			if (len > 0 && r->filename[len - 1] == '/') {
				html_file = apr_pstrcat(r->pool, r->filename, "index.html", NULL);
			} else {
				html_file = apr_pstrcat(r->pool, r->filename, ".html", NULL);
			}
			if (fileExists(html_file)) {
				/* If a .html version of the URI exists, serve it directly.
				 * We're essentially accelerating Rails page caching.
				 */
				r->filename = html_file;
				r->canonical_filename = html_file;
				forwardToRails = false;
			} else {
				forwardToRails = true;
			}
		} else {
			/*
			 * Non-GET requests are always forwarded to Rails.
			 * This important because of REST conventions, e.g.
			 * 'POST /foo' maps to 'FooController.create',
			 * while 'GET /foo' maps to 'FooController.index'.
			 * We wouldn't want our page caching support to interfere
			 * with that.
			 */
			forwardToRails = true;
		}
		
		if (forwardToRails) {
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
			 * interferes with Passenger anyway (it delegates requests
			 * to the CGI script dispatch.cgi).
			 */
			if (config->allowModRewrite == DirConfig::UNSET
			 || config->allowModRewrite == DirConfig::DISABLED) {
				/* Of course, we only do that if the config allows us
				 * to. Some people have complex mod_rewrite rules that
				 * they don't want to abandon. Those people will have to
				 * make sure that the Rails app's .htaccess doesn't
				 * interfere.
				 */
				return OK;
			} else {
				return DECLINED;
			}
		} else {
			return DECLINED;
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
	 * The Apache initialization process has the following properties:
	 *
	 * 1. Apache on Unix calls the post_config hook twice, once before detach() and once
	 *    after. On Windows it never calls detach().
	 * 2. When Apache is compiled to use DSO modules, the modules are unloaded between the
	 *    two post_config hook calls.
	 * 3. On Unix, if the -X commandline option is given (the 'DEBUG' config is set),
	 *    detach() will not be called.
	 *
	 * The Passenger initialization process is pretty expensive because a spawn server has
	 * to be started, so we'll want to avoid initializing twice because of property #2.
	 *
	 * The most straightforward solution is to initialize Passenger the second time
	 * post_config is called. But unfortunately, that doesn't work if the Passenger
	 * module is loaded after a graceful restart. There also doesn't seem to be any
	 * good hooks that we can use to avoid double initialization.
	 *
	 * So as a hack, we check whether Apache has already been daemonized, by checking
	 * whether ppid() returns 1.
	 */
	if (getppid() == 1 || ap_exists_config_define("DEBUG")) {
		try {
			hooks = new Hooks(pconf, plog, ptemp, s);
			apr_pool_cleanup_register(pconf, NULL,
				destroy_hooks,
				apr_pool_cleanup_null);
			return OK;
		} catch (const thread_resource_error &e) {
			struct rlimit lim;
			string pthread_threads_max;
			
			lim.rlim_cur = 0;
			lim.rlim_max = 0;
			getrlimit(RLIMIT_NPROC, &lim);
			#ifdef PTHREAD_THREADS_MAX
				pthread_threads_max = toString(PTHREAD_THREADS_MAX);
			#else
				pthread_threads_max = "unknown";
			#endif
			
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
				"*** Passenger could not be initialize because a "
				"threading resource could not be allocated or initialized. "
				"The error message is:");
			fprintf(stderr,
				"  %s\n\n"
				"System settings:\n"
				"  RLIMIT_NPROC: soft = %d, hard = %d\n"
				"  PTHREAD_THREADS_MAX: %s\n"
				"\n",
				e.what(),
				(int) lim.rlim_cur, (int) lim.rlim_max,
				pthread_threads_max.c_str());
			
			fprintf(stderr, "Output of 'uname -a' follows:\n");
			fflush(stderr);
			system("uname -a >&2");
			
			fprintf(stderr, "\nOutput of 'ulimit -a' follows:\n");
			fflush(stderr);
			system("ulimit -a >&2");
			
			return DECLINED;
			
		} catch (const exception &e) {
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
				"*** Passenger could not be initialized because of this error: %s",
				e.what());
			hooks = NULL;
			return DECLINED;
		}
	} else {
		return OK;
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
	ap_hook_handler(handle_request, NULL, NULL, APR_HOOK_MIDDLE);
}

/**
 * @}
 */
