/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#include <boost/thread.hpp>

#include <sys/time.h>
#include <sys/resource.h>
#include <exception>
#include <cstdio>
#include <unistd.h>

#include <oxt/macros.hpp>
#include "Hooks.h"
#include "Bucket.h"
#include "Configuration.h"
#include "Utils.h"
#include "Logging.h"
#include "ApplicationPoolServer.h"
#include "MessageChannel.h"
#include "DirectoryMapper.h"
#include "Timer.h"
#include "Version.h"

/* The Apache/APR headers *must* come after the Boost headers, otherwise
 * compilation will fail on OpenBSD.
 *
 * apr_want.h *must* come after MessageChannel.h, otherwise compilation will
 * fail on platforms on which apr_want.h tries to redefine 'struct iovec'.
 * http://tinyurl.com/b6aatw
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
#include <unixd.h>

using namespace std;
using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA passenger_module;


/*
 * This is the main source file which interfaces directly with Apache by
 * installing hooks. The code here can look a bit convoluted, but it'll make
 * more sense if you read:
 * http://httpd.apache.org/docs/2.2/developer/request.html
 *
 * Scroll all the way down to passenger_register_hooks to get an idea of
 * what we're hooking into and what we do in those hooks.
 */


/**
 * If the HTTP client sends POST data larger than this value (in bytes),
 * then the POST data will be fully buffered into a temporary file, before
 * allocating a Ruby web application session.
 * File uploads smaller than this are buffered into memory instead.
 */
#define LARGE_UPLOAD_THRESHOLD 1024 * 8


/**
 * Apache hook functions, wrapped in a class.
 *
 * @ingroup Core
 */
class Hooks {
private:
	struct AprDestructable {
		virtual ~AprDestructable() { }
		
		static apr_status_t cleanup(void *p) {
			delete (AprDestructable *) p;
			return APR_SUCCESS;
		}
	};
	
	struct RequestNote: public AprDestructable {
		DirectoryMapper mapper;
		DirConfig *config;
		bool forwardToBackend;
		const char *handlerBeforeModRewrite;
		char *filenameBeforeModRewrite;
		apr_filetype_e oldFileType;
		const char *handlerBeforeModAutoIndex;
		
		RequestNote(const DirectoryMapper &m)
			: mapper(m) {
			forwardToBackend = false;
			filenameBeforeModRewrite = NULL;
		}
	};
	
	struct ErrorReport: public AprDestructable {
		virtual int report(request_rec *r) = 0;
	};
	
	struct ReportFileSystemError: public ErrorReport {
		FileSystemException e;
		
		ReportFileSystemError(const FileSystemException &ex): e(ex) { }
		
		int report(request_rec *r) {
			r->status = 500;
			ap_set_content_type(r, "text/html; charset=UTF-8");
			ap_rputs("<h1>Passenger error #2</h1>\n", r);
			ap_rputs("An error occurred while trying to access '", r);
			ap_rputs(ap_escape_html(r->pool, e.filename().c_str()), r);
			ap_rputs("': ", r);
			ap_rputs(ap_escape_html(r->pool, e.what()), r);
			if (e.code() == EACCES || e.code() == EPERM) {
				ap_rputs("<p>", r);
				ap_rputs("Apache doesn't have read permissions to that file. ", r);
				ap_rputs("Please fix the relevant file permissions.", r);
				ap_rputs("</p>", r);
			}
			P_ERROR("A filesystem exception occured.\n" <<
				"  Message: " << e.what() << "\n" <<
				"  Backtrace:\n" << e.backtrace());
			return OK;
		}
	};
	
	/**
	 * A StringListCreator which returns a list of environment variable
	 * names and values, as found in r->subprocess_env.
	 */
	class EnvironmentVariablesStringListCreator: public StringListCreator {
	private:
		request_rec *r;
	public:
		EnvironmentVariablesStringListCreator(request_rec *r) {
			this->r = r;
		}
		
		virtual const StringListPtr getItems() const {
			const apr_array_header_t *env_arr;
			apr_table_entry_t *env_entries;
			StringListPtr result = ptr(new StringList());
			
			// Some standard CGI headers.
			result->push_back("SERVER_SOFTWARE");
			result->push_back(ap_get_server_version());
			
			// Subprocess environment variables.
			env_arr = apr_table_elts(r->subprocess_env);
			env_entries = (apr_table_entry_t *) env_arr->elts;
			for (int i = 0; i < env_arr->nelts; ++i) {
				if (env_entries[i].key != NULL && env_entries[i].val != NULL) {
					result->push_back(env_entries[i].key);
					result->push_back(env_entries[i].val);
				}
			}
			return result;
		}
	};
	
	enum Threeway { YES, NO, UNKNOWN };

	ApplicationPoolServerPtr applicationPoolServer;
	thread_specific_ptr<ApplicationPoolPtr> threadSpecificApplicationPool;
	Threeway m_hasModRewrite, m_hasModDir, m_hasModAutoIndex;
	CachedFileStat cstat;
	
	inline DirConfig *getDirConfig(request_rec *r) {
		return (DirConfig *) ap_get_module_config(r->per_dir_config, &passenger_module);
	}
	
	inline ServerConfig *getServerConfig(server_rec *s) {
		return (ServerConfig *) ap_get_module_config(s->module_config, &passenger_module);
	}
	
	inline RequestNote *getRequestNote(request_rec *r) {
		// The union is needed in order to be compliant with
		// C99/C++'s strict aliasing rules. http://tinyurl.com/g5hgh
		union {
			RequestNote *note;
			void *pointer;
		} u;
		u.note = 0;
		apr_pool_userdata_get(&u.pointer, "Phusion Passenger", r->pool);
		return u.note;
	}
	
	/**
	 * Returns a usable ApplicationPool object.
	 *
	 * When using the worker MPM and global queuing, deadlocks can occur, for
	 * the same reason described in ApplicationPoolServer::connect(). This
	 * method allows us to avoid this deadlock by making sure that each
	 * thread gets its own connection to the application pool server.
	 *
	 * It also checks whether the currently cached ApplicationPool object
	 * is disconnected (which can happen if an error previously occured).
	 * If so, it will reconnect to the ApplicationPool server.
	 */
	ApplicationPoolPtr getApplicationPool() {
		ApplicationPoolPtr *pool_ptr = threadSpecificApplicationPool.get();
		if (pool_ptr == NULL) {
			pool_ptr = new ApplicationPoolPtr(applicationPoolServer->connect());
			threadSpecificApplicationPool.reset(pool_ptr);
		} else if (!(*pool_ptr)->connected()) {
			P_DEBUG("Reconnecting to ApplicationPool server");
			*pool_ptr = applicationPoolServer->connect();
		}
		return *pool_ptr;
	}
	
	bool hasModRewrite() {
		if (m_hasModRewrite == UNKNOWN) {
			if (ap_find_linked_module("mod_rewrite.c")) {
				m_hasModRewrite = YES;
			} else {
				m_hasModRewrite = NO;
			}
		}
		return m_hasModRewrite == YES;
	}
	
	bool hasModDir() {
		if (m_hasModDir == UNKNOWN) {
			if (ap_find_linked_module("mod_dir.c")) {
				m_hasModDir = YES;
			} else {
				m_hasModDir = NO;
			}
		}
		return m_hasModDir == YES;
	}
	
	bool hasModAutoIndex() {
		if (m_hasModAutoIndex == UNKNOWN) {
			if (ap_find_linked_module("mod_autoindex.c")) {
				m_hasModAutoIndex = YES;
			} else {
				m_hasModAutoIndex = NO;
			}
		}
		return m_hasModAutoIndex == YES;
	}
	
	int reportDocumentRootDeterminationError(request_rec *r) {
		ap_set_content_type(r, "text/html; charset=UTF-8");
		ap_rputs("<h1>Passenger error #1</h1>\n", r);
		ap_rputs("Cannot determine the document root for the current request.", r);
		return OK;
	}
	
	int reportBusyException(request_rec *r) {
		ap_custom_response(r, HTTP_SERVICE_UNAVAILABLE,
			"This website is too busy right now.  Please try again later.");
		return HTTP_SERVICE_UNAVAILABLE;
	}
	
	/**
	 * Gather some information about the request and do some preparations. In this method,
	 * it will be determined whether the request URI should be served statically by Apache
	 * (in case of static assets or in case there's a page cache file available) or
	 * whether it should be forwarded to the backend application.
	 *
	 * The strategy is as follows:
	 *
	 * We check whether Phusion Passenger is enabled for this URI (A).
	 * If so, then we check whether the following situations are true:
	 * (B) There is a backend application defined for this URI.
	 * (C) r->filename already exists.
	 * (D) There is a page cache file for the URI.
	 *
	 * - If A is not true, or if B is not true, or if C is true, then don't do anything.
	 *   Passenger will be disabled during the rest of this request.
	 * - If D is true, then we first transform r->filename to the page cache file's
	 *   filename, and then we let Apache serve it statically.
	 * - If D is not true, then we forward the request to the backend application.
	 *
	 * @pre The (A) condition must be true.
	 * @param coreModuleWillBeRun Whether the core.c map_to_storage hook might be called after this.
	 * @return Whether the Passenger handler hook method should be run.
	 */
	bool prepareRequest(request_rec *r, DirConfig *config, const char *filename, bool coreModuleWillBeRun = false) {
		TRACE_POINT();
		DirectoryMapper mapper(r, config, &cstat, config->getStatThrottleRate());
		try {
			if (mapper.getBaseURI() == NULL) {
				// (B) is not true.
				return false;
			}
		} catch (const FileSystemException &e) {
			/* DirectoryMapper tried to examine the filesystem in order
			 * to autodetect the application type (e.g. by checking whether
			 * environment.rb exists. But something went wrong, probably
			 * because of a permission problem. This usually
			 * means that the user is trying to deploy an application, but
			 * set the wrong permissions on the relevant folders.
			 * Later, in the handler hook, we inform the user about this
			 * problem so that he can either disable Phusion Passenger's
			 * autodetection routines, or fix the permissions.
			 *
			 * If it's not a permission problem then we'll disable
			 * Phusion Passenger for the rest of the request.
			 */
			if (e.code() == EACCES || e.code() == EPERM) {
				// TODO: filesystem error is not always reported. need
				// to figure out why. test case:
				// - mkdir /foo
				// - mkdir /foo/public
				// - mkdir /foo/config
				// - chmod 000 /foo/config
				// - add vhost 'foo' with document root /foo/public
				// - curl http://foo/
				apr_pool_userdata_set(new ReportFileSystemError(e),
					"Phusion Passenger: error report",
					ReportFileSystemError::cleanup,
					r->pool);
				return true;
			} else {
				return false;
			}
		}
		
		/* Save some information for the hook methods that are called later.
		 * The existance of this note indicates that the URI belongs to a Phusion
		 * Passenger-served application.
		 */
		RequestNote *note = new RequestNote(mapper);
		note->config = config;
		apr_pool_userdata_set(note, "Phusion Passenger", RequestNote::cleanup, r->pool);
		
		try {
			// (B) is true.
			FileType fileType = getFileType(filename);
			if (fileType == FT_REGULAR) {
				// (C) is true.
				return false;
			}
			
			// (C) is not true. Check whether (D) is true.
			char *pageCacheFile;
			/* Only GET requests may hit the page cache. This is
			 * important because of REST conventions, e.g.
			 * 'POST /foo' maps to 'FooController#create',
			 * while 'GET /foo' maps to 'FooController#index'.
			 * We wouldn't want our page caching support to interfere
			 * with that.
			 */
			if (r->method_number == M_GET) {
				if (fileType == FT_DIRECTORY) {
					size_t len;
					
					len = strlen(filename);
					if (len > 0 && filename[len - 1] == '/') {
						pageCacheFile = apr_pstrcat(r->pool, filename,
							"index.html", NULL);
					} else {
						pageCacheFile = apr_pstrcat(r->pool, filename,
							".html", NULL);
					}
				} else {
					pageCacheFile = apr_pstrcat(r->pool, filename,
						".html", NULL);
				}
				if (!fileExists(pageCacheFile)) {
					pageCacheFile = NULL;
				}
			} else {
				pageCacheFile = NULL;
			}
			if (pageCacheFile != NULL) {
				// (D) is true.
				r->filename = pageCacheFile;
				r->canonical_filename = pageCacheFile;
				if (!coreModuleWillBeRun) {
					r->finfo.filetype = APR_NOFILE;
					ap_set_content_type(r, "text/html");
					ap_directory_walk(r);
					ap_file_walk(r);
				}
				return false;
			} else {
				// (D) is not true.
				note->forwardToBackend = true;
				return true;
			}
		} catch (const FileSystemException &e) {
			/* Something went wrong while accessing the directory in which
			 * r->filename lives. We already know that this URI belongs to
			 * a backend application, so this error probably means that the
			 * user set the wrong permissions for his 'public' folder. We
			 * don't let the handler hook run so that Apache can decide how
			 * to display the error.
			 */
			return false;
		}
	}
	
	/**
	 * Most of the high-level logic for forwarding a request to a backend application
	 * is contained in this method.
	 */
	int handleRequest(request_rec *r) {
		/********** Step 1: preparation work **********/
		
		/* Check whether an error occured in prepareRequest() that should be reported
		 * to the browser.
		 */
		
		// The union is needed in order to be compliant with
		// C99/C++'s strict aliasing rules. http://tinyurl.com/g5hgh
		union {
			ErrorReport *errorReport;
			void *pointer;
		} u;
		
		/* Did an error occur in any of the previous hook methods during
		 * this request? If so, show the error and stop here.
		 */
		u.errorReport = 0;
		apr_pool_userdata_get(&u.pointer, "Phusion Passenger: error report", r->pool);
		if (u.errorReport != 0) {
			return u.errorReport->report(r);
		}
		
		RequestNote *note = getRequestNote(r);
		if (note == 0 || !note->forwardToBackend) {
			return DECLINED;
		} else if (r->handler != NULL && strcmp(r->handler, "redirect-handler") == 0) {
			// mod_rewrite is at work.
			return DECLINED;
		}
		
		TRACE_POINT();
		DirConfig *config = note->config;
		DirectoryMapper &mapper(note->mapper);
		
		if (mapper.getPublicDirectory().empty()) {
			return reportDocumentRootDeterminationError(r);
		}
		
		
		/********** Step 2: handle HTTP upload data, if any **********/
		
		int httpStatus = ap_setup_client_block(r, REQUEST_CHUNKED_DECHUNK);
    		if (httpStatus != OK) {
			return httpStatus;
		}
		
		try {
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			Application::SessionPtr session;
			bool expectingUploadData;
			string uploadDataMemory;
			shared_ptr<BufferedUpload> uploadDataFile;
			const char *contentLength;
			
			expectingUploadData = ap_should_client_block(r);
			contentLength = lookupHeader(r, "Content-Length");
			
			/* If the HTTP upload data is larger than a threshold, or if the HTTP
			 * client sent HTTP upload data using the "chunked" transfer encoding
			 * (which implies Content-Length == NULL), then buffer the upload
			 * data into a tempfile. Otherwise, buffer it into memory.
			 *
			 * We never forward the data directly to the backend process because
			 * the HTTP client might block indefinitely until it's done uploading.
			 * This would quickly exhaust the application pool.
			 */
			if (expectingUploadData) {
				if (contentLength == NULL || atol(contentLength) > LARGE_UPLOAD_THRESHOLD) {
					uploadDataFile = receiveRequestBody(r, contentLength);
				} else {
					receiveRequestBody(r, contentLength, uploadDataMemory);
				}
			}
			
			if (expectingUploadData && contentLength == NULL) {
				/* In case of "chunked" transfer encoding, we'll set the
				 * Content-Length header to the length of the received upload
				 * data. Rails requires this header for its HTTP upload data
				 * multipart parsing process.
				 */
				if (uploadDataFile != NULL) {
					apr_table_set(r->headers_in, "Content-Length",
						toString(ftell(uploadDataFile->handle)).c_str());
				} else {
					apr_table_set(r->headers_in, "Content-Length",
						toString(uploadDataMemory.size()).c_str());
				}
			}
			
			
			/********** Step 3: forwarding the request to a backend
			                    process from the application pool **********/
			
			UPDATE_TRACE_POINT();
			try {
				ServerConfig *sconfig = getServerConfig(r->server);
				string publicDirectory(mapper.getPublicDirectory());
				PoolOptions options(
					config->getAppRoot(publicDirectory.c_str()),
					true,
					sconfig->getDefaultUser(),
					mapper.getEnvironment(),
					config->getSpawnMethodString(),
					mapper.getApplicationTypeString(),
					config->frameworkSpawnerTimeout,
					config->appSpawnerTimeout,
					config->getMaxRequests(),
					config->getMemoryLimit(),
					config->usingGlobalQueue(),
					config->getStatThrottleRate(),
					config->getRestartDir(),
					mapper.getBaseURI()
				);
				options.environmentVariables = ptr(new EnvironmentVariablesStringListCreator(r));
				
				session = getApplicationPool()->get(options);
				P_TRACE(3, "Forwarding " << r->uri << " to PID " << session->getPid());
			} catch (const SpawnException &e) {
				r->status = 500;
				if (e.hasErrorPage()) {
					ap_set_content_type(r, "text/html; charset=utf-8");
					ap_rputs(e.getErrorPage().c_str(), r);
					return OK;
				} else {
					throw;
				}
			} catch (const FileSystemException &e) {
				/* The application root cannot be determined. This could
				 * happen if, for example, the user specified 'RailsBaseURI /foo'
				 * while there is no filesystem entry called "foo" in the virtual
				 * host's document root.
				 */
				return ReportFileSystemError(e).report(r);
			} catch (const BusyException &e) {
				return reportBusyException(r);
			}
			
			UPDATE_TRACE_POINT();
			sendHeaders(r, config, session, mapper.getBaseURI());
			if (expectingUploadData) {
				if (uploadDataFile != NULL) {
					sendRequestBody(r, session, uploadDataFile);
					uploadDataFile.reset();
				} else {
					sendRequestBody(r, session, uploadDataMemory);
				}
			}
			try {
				session->shutdownWriter();
			} catch (const SystemException &e) {
				// Ignore ENOTCONN. This error occurs for some people
				// for unknown reasons, but it's harmless.
				if (e.code() != ENOTCONN) {
					throw;
				}
			}
			
			
			/********** Step 4: forwarding the response from the backend
			                    process back to the HTTP client **********/
			
			UPDATE_TRACE_POINT();
			apr_bucket_brigade *bb;
			apr_bucket *b;
			PassengerBucketStatePtr bucketState;
			pid_t backendPid;
			
			/* Setup the bucket brigade. */
			bucketState = ptr(new PassengerBucketState());
			bb = apr_brigade_create(r->connection->pool, r->connection->bucket_alloc);
			b = passenger_bucket_create(session, bucketState, r->connection->bucket_alloc);
			
			/* The bucket (b) still has a reference to the session, so the reset()
			 * call here is guaranteed not to throw any exceptions.
			 */
			backendPid = session->getPid();
			session.reset();
			
			APR_BRIGADE_INSERT_TAIL(bb, b);

			b = apr_bucket_eos_create(r->connection->bucket_alloc);
			APR_BRIGADE_INSERT_TAIL(bb, b);

			/* Now read the HTTP response header, parse it and fill relevant
			 * information in our request_rec structure.
			 */
			
			/* I know the required size for backendData because I read
			 * util_script.c's source. :-(
			 */
			char backendData[MAX_STRING_LEN];
			Timer timer;
			int result = ap_scan_script_header_err_brigade(r, bb, backendData);
			
			if (result == OK) {
				// The API documentation for ap_scan_script_err_brigade() says it
				// returns HTTP_OK on success, but it actually returns OK.
				
				/* We were able to parse the HTTP response header sent by the
				 * backend process! Proceed with passing the bucket brigade,
				 * for forwarding the response body to the HTTP client.
				 */
				
				/* Manually set the Status header because
				 * ap_scan_script_header_err_brigade() filters it
				 * out. Some broken HTTP clients depend on the
				 * Status header for retrieving the HTTP status.
				 */
				if (!r->status_line || *r->status_line == '\0') {
					r->status_line = apr_psprintf(r->pool,
						"%d Unknown Status",
						r->status);
				}
				apr_table_setn(r->headers_out, "Status", r->status_line);
				
				UPDATE_TRACE_POINT();
				ap_pass_brigade(r->output_filters, bb);
				
				if (r->connection->aborted) {
					P_WARN("Either the vistor clicked on the 'Stop' button in the "
						"web browser, or the visitor's connection has stalled "
						"and couldn't receive the data that Apache is sending "
						"to it. As a result, you will probably see a 'Broken Pipe' "
						"error in this log file. Please ignore it, "
						"this is normal. You might also want to increase Apache's "
						"TimeOut configuration option if you experience this "
						"problem often.");
				} else if (!bucketState->completed) {
					P_WARN("Apache stopped forwarding the backend's response, "
						"even though the HTTP client did not close the "
						"connection. Is this an Apache bug?");
				}
				
				return OK;
			} else if (backendData[0] == '\0') {
				if ((long long) timer.elapsed() >= r->server->timeout / 1000) {
					// Looks like an I/O timeout.
					P_ERROR("No data received from " <<
						"the backend application (process " <<
						backendPid << ") within " <<
						(r->server->timeout / 1000) << " msec. Either " <<
						"the backend application is frozen, or " <<
						"your TimeOut value of " <<
						(r->server->timeout / 1000000) <<
						" seconds is too low. Please check " <<
						"whether your application is frozen, or " <<
						"increase the value of the TimeOut " <<
						"configuration directive.");
				} else {
					P_ERROR("The backend application (process " <<
						backendPid << ") did not send a valid " <<
						"HTTP response; instead, it sent nothing " <<
						"at all. It is possible that it has crashed; " <<
						"please check whether there are crashing " <<
						"bugs in this application.");
				}
				apr_table_setn(r->err_headers_out, "Status", "500 Internal Server Error");
				return HTTP_INTERNAL_SERVER_ERROR;
			} else {
				if ((long long) timer.elapsed() >= r->server->timeout / 1000) {
					// Looks like an I/O timeout.
					P_ERROR("The backend application (process " <<
						backendPid << ") hasn't sent a valid " <<
						"HTTP response within " <<
						(r->server->timeout / 1000) << " msec. Either " <<
						"the backend application froze while " <<
						"sending a response, or " <<
						"your TimeOut value of " <<
						(r->server->timeout / 1000000) <<
						" seconds is too low. Please check " <<
						"whether the application is frozen, or " <<
						"increase the value of the TimeOut " <<
						"configuration directive. The application " <<
						"has sent this data so far: [" <<
						backendData << "]");
				} else {
					P_ERROR("The backend application (process " <<
						backendPid << ") didn't send a valid " <<
						"HTTP response. It might have crashed " <<
						"during the middle of sending an HTTP " <<
						"response, so please check whether there " <<
						"are crashing problems in your application. " <<
						"This is the data that it sent: [" <<
						backendData << "]");
				}
				apr_table_setn(r->err_headers_out, "Status", "500 Internal Server Error");
				return HTTP_INTERNAL_SERVER_ERROR;
			}
			
		} catch (const thread_interrupted &e) {
			P_TRACE(3, "A system call was interrupted during an HTTP request. Apache "
				"is probably restarting or shutting down. Backtrace:\n" <<
				e.backtrace());
			return HTTP_INTERNAL_SERVER_ERROR;
			
		} catch (const tracable_exception &e) {
			P_ERROR("Unexpected error in mod_passenger: " <<
				e.what() << "\n" << "  Backtrace:\n" << e.backtrace());
			return HTTP_INTERNAL_SERVER_ERROR;
		
		} catch (const exception &e) {
			P_ERROR("Unexpected error in mod_passenger: " <<
				e.what() << "\n" << "  Backtrace: not available");
			return HTTP_INTERNAL_SERVER_ERROR;
		
		} catch (...) {
			P_ERROR("An unexpected, unknown error occured in mod_passenger.");
			throw;
		}
	}
	
	/**
	 * Convert an HTTP header name to a CGI environment name.
	 */
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
	
	void inline addHeader(apr_table_t *table, const char *name, const char *value) {
		if (name != NULL && value != NULL) {
			apr_table_addn(table, name, value);
		}
	}
	
	apr_status_t sendHeaders(request_rec *r, DirConfig *config, Application::SessionPtr &session, const char *baseURI) {
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
		addHeader(headers, "QUERY_STRING",    r->args ? r->args : "");
		addHeader(headers, "HTTPS",           lookupEnv(r, "HTTPS"));
		addHeader(headers, "CONTENT_TYPE",    lookupHeader(r, "Content-type"));
		addHeader(headers, "DOCUMENT_ROOT",   ap_document_root(r));
		
		if (config->allowsEncodedSlashes()) {
			/*
			 * Apache decodes encoded slashes in r->uri, so we must use r->unparsed_uri
			 * if we are to support encoded slashes. However mod_rewrite doesn't change
			 * r->unparsed_uri, so the user must make a choice between mod_rewrite
			 * support or encoded slashes support. Sucks. :-(
			 *
			 * http://code.google.com/p/phusion-passenger/issues/detail?id=113
			 * http://code.google.com/p/phusion-passenger/issues/detail?id=230
			 */
			addHeader(headers, "REQUEST_URI", r->unparsed_uri);
		} else {
			const char *request_uri;
			if (r->args != NULL) {
				request_uri = apr_pstrcat(r->pool, r->uri, "?", r->args, NULL);
			} else {
				request_uri = r->uri;
			}
			addHeader(headers, "REQUEST_URI", request_uri);
		}
		
		if (strcmp(baseURI, "/") == 0) {
			addHeader(headers, "SCRIPT_NAME", "");
			addHeader(headers, "PATH_INFO", r->uri);
		} else {
			addHeader(headers, "SCRIPT_NAME", baseURI);
			addHeader(headers, "PATH_INFO", r->uri + strlen(baseURI));
		}
		
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
		 * The data in the buffer will be processed by the AbstractRequestHandler class,
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
	
	void throwUploadBufferingException(request_rec *r, int code) {
		DirConfig *config = getDirConfig(r);
		string message("An error occured while "
			"buffering HTTP upload data to "
			"a temporary file in ");
		message.append(config->getUploadBufferDir());
		
		switch (code) {
		case ENOSPC:
			message.append(". Disk directory doesn't have enough disk space, "
				"so please make sure that it has "
				"enough disk space for buffering file uploads, "
				"or set the 'PassengerUploadBufferDir' directive "
				"to a directory that has enough disk space.");
			throw RuntimeException(message);
			break;
		case EDQUOT:
			message.append(". The current Apache worker process (which is "
				"running as ");
			message.append(getProcessUsername());
			message.append(") cannot write to this directory because of "
				"disk quota limits. Please make sure that the volume "
				"that this directory resides on has enough disk space "
				"quota for the Apache worker process, or set the "
				"'PassengerUploadBufferDir' directive to a different "
				"directory that has enough disk space quota.");
			throw RuntimeException(message);
			break;
		case ENOENT:
			message.append(". This directory doesn't exist, so please make "
				"sure that this directory exists, or set the "
				"'PassengerUploadBufferDir' directive to a "
				"directory that exists and can be written to.");
			throw RuntimeException(message);
			break;
		case EACCES:
			message.append(". The current Apache worker process (which is "
				"running as ");
			message.append(getProcessUsername());
			message.append(") doesn't have permissions to write to this "
				"directory. Please change the permissions for this "
				"directory (as well as all parent directories) so that "
				"it is writable by the Apache worker process, or set "
				"the 'PassengerUploadBufferDir' directive to a directory "
				"that Apache can write to.");
			throw RuntimeException(message);
			break;
		default:
			throw SystemException(message, code);
			break;
		}
	}
	
	/**
	 * Reads the next chunk of the request body and put it into a buffer.
	 *
	 * This is like ap_get_client_block(), but can actually report errors
	 * in a sane way. ap_get_client_block() tells you that something went
	 * wrong, but not *what* went wrong.
	 *
	 * @param r The current request.
	 * @param buffer A buffer to put the read data into.
	 * @param bufsiz The size of the buffer.
	 * @return The number of bytes read, or 0 on EOF.
	 * @throws RuntimeException Something non-I/O related went wrong, e.g.
	 *                          failure to allocate memory and stuff.
	 * @throws IOException An I/O error occurred while trying to read the
	 *                     request body data.
	 */
	unsigned long readRequestBodyFromApache(request_rec *r, char *buffer, apr_size_t bufsiz) {
		apr_status_t rv;
		apr_bucket_brigade *bb;
		
		if (r->remaining < 0 || (!r->read_chunked && r->remaining == 0)) {
			return 0;
		}

		bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
		if (bb == NULL) {
			r->connection->keepalive = AP_CONN_CLOSE;
			throw RuntimeException("An error occurred while receiving HTTP upload data: "
				"unable to create a bucket brigade. Maybe the system doesn't have "
				"enough free memory.");
		}
		
		rv = ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES,
		                    APR_BLOCK_READ, bufsiz);
			
		/* We lose the failure code here.  This is why ap_get_client_block should
		 * not be used.
		 */
		if (rv != APR_SUCCESS) {
			/* if we actually fail here, we want to just return and
			 * stop trying to read data from the client.
			 */
			r->connection->keepalive = AP_CONN_CLOSE;
			apr_brigade_destroy(bb);
			
			char buf[150], *errorString, message[1024];
			errorString = apr_strerror(rv, buf, sizeof(buf));
			if (errorString != NULL) {
				snprintf(message, sizeof(message),
					"An error occurred while receiving HTTP upload data: %s (%d)",
					errorString, rv);
			} else {
				snprintf(message, sizeof(message),
					"An error occurred while receiving HTTP upload data: unknown error %d",
					rv);
			}
			message[sizeof(message) - 1] = '\0';
			throw RuntimeException(message);
		}
		
		/* If this fails, it means that a filter is written incorrectly and that
		 * it needs to learn how to properly handle APR_BLOCK_READ requests by
		 * returning data when requested.
		 */
		if (APR_BRIGADE_EMPTY(bb)) {
			throw RuntimeException("An error occurred while receiving HTTP upload data: "
				"the next filter in the input filter chain has "
				"a bug. Please contact the author who wrote this filter about "
				"this. This problem is not caused by Phusion Passenger.");
		}

		/* Check to see if EOS in the brigade.
		 *
		 * If so, we have to leave a nugget for the *next* readRequestBodyFromApache()
		 * call to return 0.
		 */
		if (APR_BUCKET_IS_EOS(APR_BRIGADE_LAST(bb))) {
			if (r->read_chunked) {
				r->remaining = -1;
			} else {
				r->remaining = 0;
			}
		}

		rv = apr_brigade_flatten(bb, buffer, &bufsiz);
		if (rv != APR_SUCCESS) {
			apr_brigade_destroy(bb);
			
			char buf[150], *errorString, message[1024];
			errorString = apr_strerror(rv, buf, sizeof(buf));
			if (errorString != NULL) {
				snprintf(message, sizeof(message),
					"An error occurred while receiving HTTP upload data: %s (%d)",
					errorString, rv);
			} else {
				snprintf(message, sizeof(message),
					"An error occurred while receiving HTTP upload data: unknown error %d",
					rv);
			}
			message[sizeof(message) - 1] = '\0';
			throw IOException(message);
		}
		
		/* XXX yank me? */
		r->read_length += bufsiz;
		
		apr_brigade_destroy(bb);
		return bufsiz;
	}
	
	/**
	 * Receive the HTTP upload data and buffer it into a BufferedUpload temp file.
	 *
	 * @param r The request.
	 * @param contentLength The value of the HTTP Content-Length header. This is used
	 *                      to check whether the HTTP client has sent complete upload
	 *                      data. NULL indicates that there is no Content-Length header,
	 *                      i.e. that the HTTP client used chunked transfer encoding.
	 * @throws RuntimeException
	 * @throws SystemException
	 * @throws IOException
	 */
	shared_ptr<BufferedUpload> receiveRequestBody(request_rec *r, const char *contentLength) {
		TRACE_POINT();
		DirConfig *config = getDirConfig(r);
		shared_ptr<BufferedUpload> tempFile;
		try {
			tempFile.reset(new BufferedUpload(config->getUploadBufferDir()));
		} catch (const SystemException &e) {
			throwUploadBufferingException(r, e.code());
		}
		
		char buf[1024 * 32];
		apr_off_t len;
		size_t total_written = 0;
		
		while ((len = readRequestBodyFromApache(r, buf, sizeof(buf))) > 0) {
			size_t written = 0;
			do {
				size_t ret = fwrite(buf, 1, len - written, tempFile->handle);
				if (ret <= 0 || fflush(tempFile->handle) == EOF) {
					throwUploadBufferingException(r, errno);
				}
				written += ret;
			} while (written < (size_t) len);
			total_written += written;
		}
		
		if (contentLength != NULL && ftell(tempFile->handle) != atol(contentLength)) {
			string message = "It looks like the browser did not finish the file upload: "
				"it said it will upload ";
			message.append(contentLength);
			message.append(" bytes, but it closed the connection after sending ");
			message.append(toString(ftell(tempFile->handle)));
			message.append(" bytes. The user probably clicked Stop in the browser "
				"or his Internet connection stalled.");
			throw IOException(message);
		}
		return tempFile;
	}
	
	/**
	 * Receive the HTTP upload data and buffer it into a string.
	 *
	 * @param r The request.
	 * @param contentLength The value of the HTTP Content-Length header. This is used
	 *                      to check whether the HTTP client has sent complete upload
	 *                      data. NULL indicates that there is no Content-Length header,
	 *                      i.e. that the HTTP client used chunked transfer encoding.
	 * @param string The string to buffer into.
	 * @throws RuntimeException
	 * @throws IOException
	 */
	void receiveRequestBody(request_rec *r, const char *contentLength, string &buffer) {
		TRACE_POINT();
		unsigned long l_contentLength = 0;
		char buf[1024 * 32];
		apr_off_t len;
		
		buffer.clear();
		if (contentLength != NULL) {
			l_contentLength = atol(contentLength);
			buffer.reserve(l_contentLength);
		}
		
		while ((len = readRequestBodyFromApache(r, buf, sizeof(buf))) > 0) {
			buffer.append(buf, len);
		}
		
		if (contentLength != NULL && buffer.size() != l_contentLength) {
			string message = "It looks like the browser did not finish the file upload: "
				"it said it will upload ";
			message.append(contentLength);
			message.append(" bytes, but it closed the connection after sending ");
			message.append(toString(buffer.size()));
			message.append(" bytes. The user probably clicked Stop in the browser "
				"or his Internet connection stalled.");
			throw IOException(message);
		}
	}
	
	void sendRequestBody(request_rec *r, Application::SessionPtr &session, shared_ptr<BufferedUpload> &uploadData) {
		TRACE_POINT();
		rewind(uploadData->handle);
		while (!feof(uploadData->handle)) {
			char buf[1024 * 32];
			size_t size;
			
			size = fread(buf, 1, sizeof(buf), uploadData->handle);
			
			session->sendBodyBlock(buf, size);
		}
	}
	
	void sendRequestBody(request_rec *r, Application::SessionPtr &session, const string &buffer) {
		session->sendBodyBlock(buffer.c_str(), buffer.size());
	}

public:
	Hooks(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
	    : cstat(1024) {
		passenger_config_merge_all_servers(pconf, s);
		ServerConfig *config = getServerConfig(s);
		Passenger::setLogLevel(config->logLevel);
		m_hasModRewrite = UNKNOWN;
		m_hasModDir = UNKNOWN;
		m_hasModAutoIndex = UNKNOWN;
		
		P_DEBUG("Initializing Phusion Passenger...");
		ap_add_version_component(pconf, "Phusion_Passenger/" PASSENGER_VERSION);
		
		const char *user;
		string applicationPoolServerExe, spawnServer;
		
		createPassengerTempDir(config->getTempDir(), config->userSwitching,
			config->getDefaultUser(), unixd_config.user_id,
			unixd_config.group_id);
		
		if (config->userSwitching) {
			user = "";
		} else {
			user = config->getDefaultUser();
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
				config->getRuby(), user)
		);
		
		ApplicationPoolPtr pool(applicationPoolServer->connect());
		pool->setMax(config->maxPoolSize);
		pool->setMaxPerApp(config->maxInstancesPerApp);
		pool->setMaxIdleTime(config->poolIdleTime);
	}
	
	~Hooks() {
		removeDirTree(getPassengerTempDir().c_str());
	}
	
	int prepareRequestWhenInHighPerformanceMode(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		if (config->isEnabled() && config->highPerformanceMode()) {
			if (prepareRequest(r, config, r->filename, true)) {
				return OK;
			} else {
				return DECLINED;
			}
		} else {
			return DECLINED;
		}
	}
	
	/**
	 * This is the hook method for the map_to_storage hook. Apache's final map_to_storage hook
	 * method (defined in core.c) will do the following:
	 *
	 * If r->filename doesn't exist, then it will change the filename to the
	 * following form:
	 *    
	 *     A/B
	 *    
	 * A is top-most directory that exists. B is the first filename piece that
	 * normally follows A. For example, suppose that a website's DocumentRoot
	 * is /website, on server http://test.com/. Suppose that there's also a
	 * directory /website/images. No other files or directories exist in /website.
	 * 
	 * If we access:                    then r->filename will be:
	 * http://test.com/foo/bar          /website/foo
	 * http://test.com/foo/bar/baz      /website/foo
	 * http://test.com/images/foo/bar   /website/images/foo
	 *
	 * We obviously don't want this to happen because it'll interfere with our page
	 * cache file search code. So here we save the original value of r->filename so
	 * that we can use it later.
	 */
	int saveOriginalFilename(request_rec *r) {
		apr_table_set(r->notes, "Phusion Passenger: original filename", r->filename);
		return DECLINED;
	}
	
	int prepareRequestWhenNotInHighPerformanceMode(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		if (config->isEnabled()) {
			if (config->highPerformanceMode()) {
				/* Preparations have already been done in the map_to_storage hook.
				 * Prevent other modules' fixups hooks from being run.
				 */
				return OK;
			} else {
				/* core.c's map_to_storage hook will transform the filename, as
				 * described by saveOriginalFilename(). Here we restore the
				 * original filename.
				 */
				const char *filename = apr_table_get(r->notes, "Phusion Passenger: original filename");
				if (filename == NULL) {
					return DECLINED;
				} else {
					prepareRequest(r, config, filename);
					/* Always return declined in order to let other modules'
					 * hooks run, regardless of what prepareRequest()'s
					 * result is.
					 */
					return DECLINED;
				}
			}
		} else {
			return DECLINED;
		}
	}
	
	/**
	 * The default .htaccess provided by on Rails on Rails (that is, before version 2.1.0)
	 * has the following mod_rewrite rules in it:
	 *
	 *   RewriteEngine on
	 *   RewriteRule ^$ index.html [QSA]
	 *   RewriteRule ^([^.]+)$ $1.html [QSA]
	 *   RewriteCond %{REQUEST_FILENAME} !-f
	 *   RewriteRule ^(.*)$ dispatch.cgi [QSA,L]
	 *
	 * As a result, all requests that do not map to a filename will be redirected to
	 * dispatch.cgi (or dispatch.fcgi, if the user so specified). We don't want that
	 * to happen, so before mod_rewrite applies its rules, we save the current state.
	 * After mod_rewrite has applied its rules, undoRedirectionToDispatchCgi() will
	 * check whether mod_rewrite attempted to perform an internal redirection to
	 * dispatch.(f)cgi. If so, then it will revert the state to the way it was before
	 * mod_rewrite took place.
	 */
	int saveStateBeforeRewriteRules(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note != 0 && hasModRewrite()) {
			note->handlerBeforeModRewrite = r->handler;
			note->filenameBeforeModRewrite = r->filename;
		}
		return DECLINED;
	}

	int undoRedirectionToDispatchCgi(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note == 0 || !hasModRewrite()) {
			return DECLINED;
		}
		
		if (r->handler != NULL && strcmp(r->handler, "redirect-handler") == 0) {
			// Check whether r->filename looks like "redirect:.../dispatch.(f)cgi"
			size_t len = strlen(r->filename);
			// 22 == strlen("redirect:/dispatch.cgi")
			if (len >= 22 && memcmp(r->filename, "redirect:", 9) == 0
			 && (memcmp(r->filename + len - 13, "/dispatch.cgi", 13) == 0
			  || memcmp(r->filename + len - 14, "/dispatch.fcgi", 14) == 0)) {
				if (note->filenameBeforeModRewrite != NULL) {
					r->filename = note->filenameBeforeModRewrite;
					r->canonical_filename = note->filenameBeforeModRewrite;
					r->handler = note->handlerBeforeModRewrite;
				}
			}
		}
		return DECLINED;
	}
	
	/**
	 * mod_dir does the following:
	 * If r->filename is a directory, and the URI doesn't end with a slash,
	 * then it will redirect the browser to an URI with a slash. For example,
	 * if you go to http://foo.com/images, then it will redirect you to
	 * http://foo.com/images/.
	 *
	 * This behavior is undesired. Suppose that there is an ImagesController,
	 * and there's also a 'public/images' folder used for storing page cache
	 * files. Then we don't want mod_dir to perform the redirection.
	 *
	 * So in startBlockingModDir(), we temporarily change some fields in the
	 * request structure in order to block mod_dir. In endBlockingModDir() we
	 * revert those fields to their old value.
	 */
	int startBlockingModDir(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note != 0 && hasModDir()) {
			note->oldFileType = r->finfo.filetype;
			r->finfo.filetype = APR_NOFILE;
		}
		return DECLINED;
	}
	
	int endBlockingModDir(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note != 0 && hasModDir()) {
			r->finfo.filetype = note->oldFileType;
		}
		return DECLINED;
	}
	
	/**
	 * mod_autoindex will try to display a directory index for URIs that map to a directory.
	 * This is undesired because of page caching semantics. Suppose that a Rails application
	 * has an ImagesController which has page caching enabled, and thus also a 'public/images'
	 * directory. When the visitor visits /images we'll want the request to be forwarded to
	 * the Rails application, instead of displaying a directory index.
	 *
	 * So in this hook method, we temporarily change some fields in the request structure
	 * in order to block mod_autoindex. In endBlockingModAutoIndex(), we restore the request
	 * structure to its former state.
	 */
	int startBlockingModAutoIndex(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note != 0 && hasModAutoIndex()) {
			note->handlerBeforeModAutoIndex = r->handler;
			r->handler = "";
		}
		return DECLINED;
	}
	
	int endBlockingModAutoIndex(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note != 0 && hasModAutoIndex()) {
			r->handler = note->handlerBeforeModAutoIndex;
		}
		return DECLINED;
	}
	
	int handleRequestWhenInHighPerformanceMode(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		if (config->highPerformanceMode()) {
			return handleRequest(r);
		} else {
			return DECLINED;
		}
	}
	
	int handleRequestWhenNotInHighPerformanceMode(request_rec *r) {
		DirConfig *config = getDirConfig(r);
		if (config->highPerformanceMode()) {
			return DECLINED;
		} else {
			return handleRequest(r);
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
	try {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		P_DEBUG("Shutting down Phusion Passenger...");
		delete hooks;
		hooks = NULL;
	} catch (const thread_interrupted &) {
		// Ignore interruptions, we're shutting down anyway.
		P_TRACE(3, "A system call was interrupted during shutdown of mod_passenger.");
	} catch (const exception &e) {
		// Ignore other exceptions, we're shutting down anyway.
		P_TRACE(3, "Exception during shutdown of mod_passenger: " << e.what());
	}
	return APR_SUCCESS;
}

static int
init_module(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
	/*
	 * HISTORICAL NOTE:
	 *
	 * The Apache initialization process has the following properties:
	 *
	 * 1. Apache on Unix calls the post_config hook twice, once before detach() and once
	 *    after. On Windows it never calls detach().
	 * 2. When Apache is compiled to use DSO modules, the modules are unloaded between the
	 *    two post_config hook calls.
	 * 3. On Unix, if the -X commandline option is given (the 'DEBUG' config is set),
	 *    detach() will not be called.
	 *
	 * Because of property #2, the post_config hook is called twice. We initially tried
	 * to avoid this with all kinds of hacks and workarounds, but none of them are
	 * universal, i.e. it works for some people but not for others. So we got rid of the
	 * hacks, and now we always initialize in the post_config hook.
	 */
	if (hooks != NULL) {
		P_DEBUG("Restarting Phusion Passenger....");
		delete hooks;
		hooks = NULL;
	}
	try {
		hooks = new Hooks(pconf, plog, ptemp, s);
		apr_pool_cleanup_register(pconf, NULL,
			destroy_hooks,
			apr_pool_cleanup_null);
		return OK;
	
	} catch (const thread_interrupted &e) {
		P_TRACE(2, "A system call was interrupted during mod_passenger "
			"initialization. Apache might be restarting or shutting "
			"down. Backtrace:\n" << e.backtrace());
		return DECLINED;
	
	} catch (const thread_resource_error &e) {
		struct rlimit lim;
		string pthread_threads_max;
		
		lim.rlim_cur = 0;
		lim.rlim_max = 0;

		/* Solaris does not define the RLIMIT_NPROC limit. Setting it to infinity... */
#ifdef RLIMIT_NPROC
		getrlimit(RLIMIT_NPROC, &lim);
#else
		lim.rlim_cur = lim.rlim_max = RLIM_INFINITY; 
#endif

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
}

#define DEFINE_REQUEST_HOOK(c_name, cpp_name)        \
	static int c_name(request_rec *r) {          \
		if (OXT_LIKELY(hooks != NULL)) {     \
			return hooks->cpp_name(r);   \
		} else {                             \
			return DECLINED;             \
		}                                    \
	}

DEFINE_REQUEST_HOOK(prepare_request_when_in_high_performance_mode, prepareRequestWhenInHighPerformanceMode)
DEFINE_REQUEST_HOOK(save_original_filename, saveOriginalFilename)
DEFINE_REQUEST_HOOK(prepare_request_when_not_in_high_performance_mode, prepareRequestWhenNotInHighPerformanceMode)
DEFINE_REQUEST_HOOK(save_state_before_rewrite_rules, saveStateBeforeRewriteRules)
DEFINE_REQUEST_HOOK(undo_redirection_to_dispatch_cgi, undoRedirectionToDispatchCgi)
DEFINE_REQUEST_HOOK(start_blocking_mod_dir, startBlockingModDir)
DEFINE_REQUEST_HOOK(end_blocking_mod_dir, endBlockingModDir)
DEFINE_REQUEST_HOOK(start_blocking_mod_autoindex, startBlockingModAutoIndex)
DEFINE_REQUEST_HOOK(end_blocking_mod_autoindex, endBlockingModAutoIndex)
DEFINE_REQUEST_HOOK(handle_request_when_in_high_performance_mode, handleRequestWhenInHighPerformanceMode)
DEFINE_REQUEST_HOOK(handle_request_when_not_in_high_performance_mode, handleRequestWhenNotInHighPerformanceMode)


/**
 * Apache hook registration function.
 */
void
passenger_register_hooks(apr_pool_t *p) {
	static const char * const rewrite_module[] = { "mod_rewrite.c", NULL };
	static const char * const dir_module[] = { "mod_dir.c", NULL };
	static const char * const autoindex_module[] = { "mod_autoindex.c", NULL };

	ap_hook_post_config(init_module, NULL, NULL, APR_HOOK_MIDDLE);
	
	ap_hook_map_to_storage(prepare_request_when_in_high_performance_mode, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_map_to_storage(save_original_filename, NULL, NULL, APR_HOOK_LAST);
	
	ap_hook_fixups(prepare_request_when_not_in_high_performance_mode, NULL, rewrite_module, APR_HOOK_FIRST);
	ap_hook_fixups(save_state_before_rewrite_rules, NULL, rewrite_module, APR_HOOK_LAST);
	ap_hook_fixups(undo_redirection_to_dispatch_cgi, rewrite_module, NULL, APR_HOOK_FIRST);
	ap_hook_fixups(start_blocking_mod_dir, NULL, dir_module, APR_HOOK_MIDDLE);
	ap_hook_fixups(end_blocking_mod_dir, dir_module, NULL, APR_HOOK_MIDDLE);
	
	ap_hook_handler(handle_request_when_in_high_performance_mode, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(start_blocking_mod_autoindex, NULL, autoindex_module, APR_HOOK_LAST);
	ap_hook_handler(end_blocking_mod_autoindex, autoindex_module, NULL, APR_HOOK_FIRST);
	ap_hook_handler(handle_request_when_not_in_high_performance_mode, NULL, NULL, APR_HOOK_LAST);
}

/**
 * @}
 */

