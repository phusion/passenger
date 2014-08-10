/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
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

/*
 * This is the main source file which interfaces directly with Apache by
 * installing hooks. The code here can look a bit convoluted, but it'll make
 * more sense if you read:
 * http://httpd.apache.org/docs/2.2/developer/request.html
 *
 * Scroll all the way down to passenger_register_hooks to get an idea of
 * what we're hooking into and what we do in those hooks. There are many
 * hooks but the gist is implemented in just two methods: prepareRequest()
 * and handleRequest(). Most hooks exist for implementing compatibility
 * with other Apache modules. These hooks create an environment in which
 * prepareRequest() and handleRequest() can be comfortably run.
 */

#include <boost/thread.hpp>

#include <sys/time.h>
#include <sys/resource.h>
#include <exception>
#include <cstdio>
#include <unistd.h>

#include <oxt/initialize.hpp>
#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/detail/context.hpp>
#include "Hooks.h"
#include "Bucket.h"
#include "Configuration.hpp"
#include "Utils.h"
#include "Utils/IOUtils.h"
#include "Utils/Timer.h"
#include "Logging.h"
#include "AgentsStarter.h"
#include "DirectoryMapper.h"
#include "Constants.h"

/* The Apache/APR headers *must* come after the Boost headers, otherwise
 * compilation will fail on OpenBSD.
 */
#include <ap_config.h>
#include <ap_release.h>
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

#ifdef APLOG_USE_MODULE
	APLOG_USE_MODULE(passenger);
#endif


/**
 * If the HTTP client sends POST data larger than this value (in bytes),
 * then the POST data will be fully buffered into a temporary file, before
 * allocating a Ruby web application session.
 * File uploads smaller than this are buffered into memory instead.
 */
#define LARGE_UPLOAD_THRESHOLD 1024 * 8


#if HTTP_VERSION(AP_SERVER_MAJORVERSION_NUMBER, AP_SERVER_MINORVERSION_NUMBER) > 2002
	// Apache > 2.2.x
	#define AP_GET_SERVER_VERSION_DEPRECATED
#elif HTTP_VERSION(AP_SERVER_MAJORVERSION_NUMBER, AP_SERVER_MINORVERSION_NUMBER) == 2002
	// Apache == 2.2.x
	#if AP_SERVER_PATCHLEVEL_NUMBER >= 14
		#define AP_GET_SERVER_VERSION_DEPRECATED
	#endif
#endif

#if HTTP_VERSION(AP_SERVER_MAJORVERSION_NUMBER, AP_SERVER_MINORVERSION_NUMBER) >= 2004
	// Apache >= 2.4
	#define unixd_config ap_unixd_config
#endif


/**
 * Apache hook functions, wrapped in a class.
 *
 * @ingroup Core
 */
class Hooks {
private:
	class ErrorReport {
	public:
		virtual ~ErrorReport() { }
		virtual int report(request_rec *r) = 0;
	};

	class ReportFileSystemError: public ErrorReport {
	private:
		FileSystemException e;

	public:
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

	class ReportDocumentRootDeterminationError: public ErrorReport {
	private:
		DocumentRootDeterminationError e;

	public:
		ReportDocumentRootDeterminationError(const DocumentRootDeterminationError &ex): e(ex) { }

		int report(request_rec *r) {
			r->status = 500;
			ap_set_content_type(r, "text/html; charset=UTF-8");
			ap_rputs("<h1>Passenger error #1</h1>\n", r);
			ap_rputs("Cannot determine the document root for the current request.", r);
			P_ERROR("Cannot determine the document root for the current request.\n" <<
				"  Backtrace:\n" << e.backtrace());
			return OK;
		}
	};

	struct RequestNote {
		DirectoryMapper mapper;
		DirConfig *config;
		ErrorReport *errorReport;

		const char *handlerBeforeModRewrite;
		char *filenameBeforeModRewrite;
		apr_filetype_e oldFileType;
		const char *handlerBeforeModAutoIndex;
		bool enabled;

		RequestNote(const DirectoryMapper &m, DirConfig *c)
			: mapper(m),
			  config(c)
		{
			errorReport      = NULL;
			handlerBeforeModRewrite   = NULL;
			filenameBeforeModRewrite  = NULL;
			oldFileType               = APR_NOFILE;
			handlerBeforeModAutoIndex = NULL;
			enabled                   = true;
		}

		~RequestNote() {
			delete errorReport;
		}

		static apr_status_t cleanup(void *p) {
			delete (RequestNote *) p;
			return APR_SUCCESS;
		}
	};

	enum Threeway { YES, NO, UNKNOWN };

	Threeway m_hasModRewrite, m_hasModDir, m_hasModAutoIndex, m_hasModXsendfile;
	CachedFileStat cstat;
	AgentsStarter agentsStarter;

	inline DirConfig *getDirConfig(request_rec *r) {
		return (DirConfig *) ap_get_module_config(r->per_dir_config, &passenger_module);
	}

	/**
	 * The existance of a request note means that the handler should be run.
	 */
	inline RequestNote *getRequestNote(request_rec *r) {
		void *pointer = 0;
		apr_pool_userdata_get(&pointer, "Phusion Passenger", r->pool);
		if (pointer != NULL) {
			RequestNote *note = (RequestNote *) pointer;
			if (OXT_LIKELY(note->enabled)) {
				return note;
			} else {
				return 0;
			}
		} else {
			return 0;
		}
	}

	void disableRequestNote(request_rec *r) {
		RequestNote *note = getRequestNote(r);
		if (note != NULL) {
			note->enabled = false;
		}
	}

	StaticString getRequestSocketFilename() const {
		return agentsStarter.getRequestSocketFilename();
	}

	StaticString getRequestSocketPassword() const {
		return agentsStarter.getRequestSocketPassword();
	}

	/**
	 * Connect to the helper agent. If it looks like the helper agent crashed,
	 * wait and retry for a short period of time until the helper agent has been
	 * restarted.
	 */
	FileDescriptor connectToHelperAgent() {
		TRACE_POINT();
		FileDescriptor conn;

		try {
			conn = connectToUnixServer(getRequestSocketFilename());
			writeExact(conn, getRequestSocketPassword());
		} catch (const SystemException &e) {
			if (e.code() == EPIPE || e.code() == ECONNREFUSED || e.code() == ENOENT) {
				UPDATE_TRACE_POINT();
				bool connected = false;

				// Maybe the helper agent crashed. First wait 50 ms.
				usleep(50000);

				// Then try to reconnect to the helper agent for the
				// next 5 seconds.
				time_t deadline = time(NULL) + 5;
				while (!connected && time(NULL) < deadline) {
					try {
						conn = connectToUnixServer(getRequestSocketFilename());
						writeExact(conn, getRequestSocketPassword());
						connected = true;
					} catch (const SystemException &e) {
						if (e.code() == EPIPE || e.code() == ECONNREFUSED || e.code() == ENOENT) {
							// Looks like the helper agent hasn't been
							// restarted yet. Wait between 20 and 100 ms.
							usleep(20000 + rand() % 80000);
							// Don't care about thread-safety of rand()
						} else {
							throw;
						}
					}
				}

				if (!connected) {
					UPDATE_TRACE_POINT();
					throw IOException("Cannot connect to the helper agent at " +
						getRequestSocketFilename());
				}
			} else {
				throw;
			}
		}
		return conn;
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

	bool hasModXsendfile() {
		if (m_hasModXsendfile == UNKNOWN) {
			if (ap_find_linked_module("mod_xsendfile.c")) {
				m_hasModXsendfile = YES;
			} else {
				m_hasModXsendfile = NO;
			}
		}
		return m_hasModXsendfile == YES;
	}

	int reportBusyException(request_rec *r) {
		ap_custom_response(r, HTTP_SERVICE_UNAVAILABLE,
			"This website is too busy right now.  Please try again later.");
		return HTTP_SERVICE_UNAVAILABLE;
	}

	/**
	 * Gather some information about the request and do some preparations.
	 *
	 * This method will determine whether the Phusion Passenger handler method
	 * should be run for this request, through the following checks:
	 * (B) There is a backend application defined for this URI.
	 * (C) r->filename already exists, meaning that this URI already maps to an existing file.
	 * (D) There is a page cache file for this URI.
	 *
	 * - If B is not true, or if C is true, then the handler shouldn't be run.
	 * - If D is true, then we first transform r->filename to the page cache file's
	 *   filename, and then we let Apache serve it statically. The Phusion Passenger
	 *   handler shouldn't be run.
	 * - If D is not true, then the handler should be run.
	 *
	 * @pre config->isEnabled()
	 * @param coreModuleWillBeRun Whether the core.c map_to_storage hook might be called after this.
	 * @return Whether the Phusion Passenger handler hook method should be run.
	 *         When true, this method will save a request note object so that future hooks
	 *         can store request-specific information.
	 */
	bool prepareRequest(request_rec *r, DirConfig *config, const char *filename, bool coreModuleWillBeRun = false) {
		TRACE_POINT();

		DirectoryMapper mapper(r, config, &cstat, config->getStatThrottleRate());
		try {
			if (mapper.getApplicationType() == PAT_NONE) {
				// (B) is not true.
				disableRequestNote(r);
				return false;
			}
		} catch (const DocumentRootDeterminationError &e) {
			auto_ptr<RequestNote> note(new RequestNote(mapper, config));
			note->errorReport = new ReportDocumentRootDeterminationError(e);
			apr_pool_userdata_set(note.release(), "Phusion Passenger",
				RequestNote::cleanup, r->pool);
			return true;
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
				auto_ptr<RequestNote> note(new RequestNote(mapper, config));
				note->errorReport = new ReportFileSystemError(e);
				apr_pool_userdata_set(note.release(), "Phusion Passenger",
					RequestNote::cleanup, r->pool);
				return true;
			} else {
				disableRequestNote(r);
				return false;
			}
		}

		// (B) is true.

		try {
			FileType fileType = getFileType(filename);
			if (fileType == FT_REGULAR) {
				// (C) is true.
				disableRequestNote(r);
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
							"index.html", (char *) NULL);
					} else {
						pageCacheFile = apr_pstrcat(r->pool, filename,
							".html", (char *) NULL);
					}
				} else {
					pageCacheFile = apr_pstrcat(r->pool, filename,
						".html", (char *) NULL);
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
				RequestNote *note = new RequestNote(mapper, config);
				apr_pool_userdata_set(note, "Phusion Passenger",
					RequestNote::cleanup, r->pool);
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
			disableRequestNote(r);
			return false;
		}
	}

	/**
	 * Most of the high-level logic for forwarding a request to the
	 * HelperAgent is contained in this method.
	 */
	int handleRequest(request_rec *r) {
		/********** Step 1: preparation work **********/

		/* Initialize OXT backtrace support if not already done for this thread */
		if (oxt::get_thread_local_context() == NULL) {
			/* There is no need to cleanup the context. Apache uses a static
			 * number of threads per process.
			 */
			thread_local_context_ptr context = thread_local_context::make_shared_ptr();
			unsigned long tid = (unsigned long) pthread_self();
			context->thread_name = "Worker " + integerToHex(tid);
			oxt::set_thread_local_context(context);
		}

		/* Check whether an error occured in prepareRequest() that should be reported
		 * to the browser.
		 */

		RequestNote *note = getRequestNote(r);
		if (note == NULL) {
			return DECLINED;
		} else if (note->errorReport != NULL) {
			/* Did an error occur in any of the previous hook methods during
			 * this request? If so, show the error and stop here.
			 */
			return note->errorReport->report(r);
		} else if (r->handler != NULL && strcmp(r->handler, "redirect-handler") == 0) {
			// mod_rewrite is at work.
			return DECLINED;
		}

		/* mod_mime might have set the httpd/unix-directory Content-Type
		 * if it detects that the current URL maps to a directory. We do
		 * not want to preserve that Content-Type.
		 */
		ap_set_content_type(r, NULL);

		TRACE_POINT();
		DirConfig *config = note->config;
		DirectoryMapper &mapper = note->mapper;

		try {
			mapper.getPublicDirectory();
		} catch (const DocumentRootDeterminationError &e) {
			return ReportDocumentRootDeterminationError(e).report(r);
		} catch (const FileSystemException &e) {
			/* The application root cannot be determined. This could
			 * happen if, for example, the user specified 'RailsBaseURI /foo'
			 * while there is no filesystem entry called "foo" in the virtual
			 * host's document root.
			 */
			return ReportFileSystemError(e).report(r);
		}


		UPDATE_TRACE_POINT();
		try {
			/********** Step 2: handle HTTP upload data, if any **********/

			int httpStatus = ap_setup_client_block(r, REQUEST_CHUNKED_DECHUNK);
	    	if (httpStatus != OK) {
				return httpStatus;
			}

			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			bool expectingUploadData;
			bool shouldBufferUploads;
			string uploadDataMemory;
			boost::shared_ptr<BufferedUpload> uploadDataFile;
			const char *contentLength;

			expectingUploadData = ap_should_client_block(r);
			contentLength = lookupHeader(r, "Content-Length");
			shouldBufferUploads = config->bufferUpload != DirConfig::DISABLED;

			/* If the HTTP upload data is larger than a threshold, or if the HTTP
			 * client sent HTTP upload data using the "chunked" transfer encoding
			 * (which implies Content-Length == NULL), then buffer the upload
			 * data into a tempfile. Otherwise, buffer it into memory.
			 *
			 * We never forward the data directly to the backend process because
			 * the HTTP client might block indefinitely until it's done uploading.
			 * This would quickly exhaust the application pool.
			 */
			if (expectingUploadData && shouldBufferUploads) {
				if (contentLength == NULL || atol(contentLength) > LARGE_UPLOAD_THRESHOLD) {
					uploadDataFile = receiveRequestBody(r);
				} else {
					receiveRequestBody(r, contentLength, uploadDataMemory);
				}

				/* We'll set the Content-Length header to the length of the
				 * received upload data. Rails 2 requires this header for its
				 * HTTP upload data multipart parsing process.
				 * There are two reasons why we don't rely on the Content-Length
				 * header as sent by the client:
				 * - The client doesn't always send Content-Length, e.g. in case
				 *   of "chunked" transfer encoding.
				 * - mod_deflate input compression can make it so that the
				 *   amount of upload we receive doesn't match Content-Length:
				 *   http://httpd.apache.org/docs/2.0/mod/mod_deflate.html#enable
				 */
				if (uploadDataFile != NULL) {
					apr_table_set(r->headers_in, "Content-Length",
						toString(ftell(uploadDataFile->handle)).c_str());
				} else {
					apr_table_set(r->headers_in, "Content-Length",
						toString(uploadDataMemory.size()).c_str());
				}
			}


			/********** Step 3: forwarding the request and request body
			                    to the HelperAgent **********/

			vector<StaticString> requestData;
			string headerData;
			unsigned int size;
			char sizeString[16];
			int ret;

			requestData.reserve(3);
			headerData.reserve(1024 * 2);
			requestData.push_back(StaticString());
			size = constructHeaders(r, config, requestData, mapper, headerData);
			requestData.push_back(",");

			ret = snprintf(sizeString, sizeof(sizeString) - 1, "%u:", size);
			sizeString[ret] = '\0';
			requestData[0] = StaticString(sizeString, ret);

			if (expectingUploadData && shouldBufferUploads && uploadDataFile == NULL) {
				requestData.push_back(uploadDataMemory);
			}

			FileDescriptor conn = connectToHelperAgent();
			gatheredWrite(conn, &requestData[0], requestData.size());

			if (expectingUploadData) {
				if (shouldBufferUploads && uploadDataFile != NULL) {
					sendRequestBody(conn, uploadDataFile);
					uploadDataFile.reset();
				} else if (!shouldBufferUploads) {
					sendRequestBody(conn, r);
				}
			}

			do {
				ret = shutdown(conn, SHUT_WR);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1 && errno != ENOTCONN) {
				// FreeBSD has a kernel bug which causes shutdown()
				// to harmlessly return ENOTCONN sometimes. See comment
				// in safelyClose().
				int e = errno;
				throw SystemException("Cannot shutdown(SHUT_WR) HelperAgent connection", e);
			}


			/********** Step 4: forwarding the response from the HelperAgent
			                    back to the HTTP client **********/

			UPDATE_TRACE_POINT();
			apr_bucket_brigade *bb;
			apr_bucket *b;
			PassengerBucketStatePtr bucketState;

			/* Setup the bucket brigade. */
			bb = apr_brigade_create(r->connection->pool, r->connection->bucket_alloc);

			bucketState = boost::make_shared<PassengerBucketState>(conn);
			b = passenger_bucket_create(bucketState, r->connection->bucket_alloc, config->getBufferResponse());
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
				if (config->errorOverride == DirConfig::ENABLED
				 && ap_is_HTTP_ERROR(r->status))
				{
					/* Send ErrorDocument.
					 * Clear r->status for override error, otherwise ErrorDocument
					 * thinks that this is a recursive error, and doesn't find the
					 * custom error page.
					 */
					int originalStatus = r->status;
					r->status = HTTP_OK;
					return originalStatus;
				} else if (ap_pass_brigade(r->output_filters, bb) == APR_SUCCESS) {
					apr_brigade_cleanup(bb);
				}
				return OK;
			} else {
				// HelperAgent sent an empty response, or an invalid response.
				apr_brigade_cleanup(bb);
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

		} catch (const std::exception &e) {
			P_ERROR("Unexpected error in mod_passenger: " <<
				e.what() << "\n" << "  Backtrace: not available");
			return HTTP_INTERNAL_SERVER_ERROR;
		}
	}

	unsigned int
	escapeUri(unsigned char *dst, const unsigned char *src, size_t size) {
		static const char hex[] = "0123456789abcdef";
			           /* " ", "#", "%", "?", %00-%1F, %7F-%FF */
		static uint32_t escape[] = {
		       0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */

		                   /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
		       0x80000029, /* 1000 0000 0000 0000  0000 0000 0010 1001 */

		                   /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
		       0x00000000, /* 0000 0000 0000 0000  0000 0000 0000 0000 */

		                   /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
		       0x80000000, /* 1000 0000 0000 0000  0000 0000 0000 0000 */

		       0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
		       0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
		       0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
		       0xffffffff  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
		};

		if (dst == NULL) {
			/* find the number of the characters to be escaped */
			unsigned int n = 0;
			while (size > 0) {
				if (escape[*src >> 5] & (1 << (*src & 0x1f))) {
					n++;
				}
				src++;
				size--;
			}
			return n;
		}

		while (size > 0) {
			if (escape[*src >> 5] & (1 << (*src & 0x1f))) {
				*dst++ = '%';
				*dst++ = hex[*src >> 4];
				*dst++ = hex[*src & 0xf];
				src++;
			} else {
				*dst++ = *src++;
			}
			size--;
		}
		return 0;
	}

	/**
	 * Checks case-insensitively whether the given header is "Transfer-Encoding".
	 */
	bool headerIsTransferEncoding(const char *headerName, size_t len) const {
		return len == sizeof("transfer-encoding") - 1 &&
			apr_tolower(headerName[0]) == (u_char) 't' &&
			apr_tolower(headerName[sizeof("transfer-encoding") - 2]) == (u_char) 'g' &&
			apr_strnatcasecmp(headerName + 1, "ransfer-encoding") == 0;
	}

	/**
	 * Convert an HTTP header name to a CGI environment name.
	 */
	char *httpToEnv(apr_pool_t *p, const char *headerName, size_t len) {
		char *result  = apr_pstrcat(p, "HTTP_", headerName, (char *) NULL);
		char *current = result + sizeof("HTTP_") - 1;

		while (*current != '\0') {
			if (*current == '-') {
				*current = '_';
			} else {
				*current = apr_toupper(*current);
			}
			current++;
		}

		return result;
	}

	const char *lookupInTable(apr_table_t *table, const char *name) {
		const apr_array_header_t *headers = apr_table_elts(table);
		apr_table_entry_t *elements = (apr_table_entry_t *) headers->elts;

		for (int i = 0; i < headers->nelts; i++) {
			if (elements[i].key != NULL && strcasecmp(elements[i].key, name) == 0) {
				return elements[i].val;
			}
		}
		return NULL;
	}

	const char *lookupHeader(request_rec *r, const char *name) {
		return lookupInTable(r->headers_in, name);
	}

	const char *lookupEnv(request_rec *r, const char *name) {
		return lookupInTable(r->subprocess_env, name);
	}

	void addHeader(string &headers, const char *name, const char *value) {
		if (name != NULL && value != NULL) {
			headers.append(name);
			headers.append(1, '\0');
			headers.append(value);
			headers.append(1, '\0');
		}
	}

	void addHeader(string &headers, const char *name, const StaticString &value) {
		if (name != NULL) {
			headers.append(name);
			headers.append(1, '\0');
			headers.append(value.c_str(), value.size());
			headers.append(1, '\0');
		}
	}

	void addHeader(request_rec *r, string &headers, const char *name, int value) {
		if (value != UNSET_INT_VALUE) {
			headers.append(name);
			headers.append(1, '\0');
			headers.append(apr_psprintf(r->pool, "%d", value));
			headers.append(1, '\0');
		}
	}

	void addHeader(request_rec *r, string &headers, const char *name, DirConfig::Threeway value) {
		if (value != DirConfig::UNSET) {
			headers.append(name);
			if (value == DirConfig::ENABLED) {
				headers.append("\0true\0", 6);
			} else {
				headers.append("\0false\0", 7);
			}
		}
	}

	unsigned int constructHeaders(request_rec *r, DirConfig *config,
		vector<StaticString> &requestData, DirectoryMapper &mapper,
		string &output)
	{
		const char *baseURI = mapper.getBaseURI();

		/*
		 * Apache unescapes URI's before passing them to Phusion Passenger,
		 * but backend processes expect the escaped version.
		 * http://code.google.com/p/phusion-passenger/issues/detail?id=404
		 */
		size_t uriLen = strlen(r->uri);
		unsigned int escaped = escapeUri(NULL, (const unsigned char *) r->uri, uriLen);
		char *escapedUri = (char *) apr_palloc(r->pool, uriLen + 2 * escaped + 1);
		escapeUri((unsigned char *) escapedUri, (const unsigned char *) r->uri, uriLen);
		escapedUri[uriLen + 2 * escaped] = '\0';


		// Set standard CGI variables.
		#ifdef AP_GET_SERVER_VERSION_DEPRECATED
			addHeader(output, "SERVER_SOFTWARE", ap_get_server_banner());
		#else
			addHeader(output, "SERVER_SOFTWARE", ap_get_server_version());
		#endif
		addHeader(output, "SERVER_PROTOCOL", r->protocol);
		addHeader(output, "SERVER_NAME",     ap_get_server_name(r));
		addHeader(output, "SERVER_ADMIN",    r->server->server_admin);
		addHeader(output, "SERVER_ADDR",     r->connection->local_ip);
		addHeader(output, "SERVER_PORT",     apr_psprintf(r->pool, "%u", ap_get_server_port(r)));
		#if HTTP_VERSION(AP_SERVER_MAJORVERSION_NUMBER, AP_SERVER_MINORVERSION_NUMBER) >= 2004
			addHeader(output, "REMOTE_ADDR", r->connection->client_ip);
			addHeader(output, "REMOTE_PORT", apr_psprintf(r->pool, "%d", r->connection->client_addr->port));
		#else
			addHeader(output, "REMOTE_ADDR", r->connection->remote_ip);
			addHeader(output, "REMOTE_PORT", apr_psprintf(r->pool, "%d", r->connection->remote_addr->port));
		#endif
		addHeader(output, "REMOTE_USER",     r->user);
		addHeader(output, "REQUEST_METHOD",  r->method);
		addHeader(output, "QUERY_STRING",    r->args ? r->args : "");
		addHeader(output, "HTTPS",           lookupEnv(r, "HTTPS"));
		addHeader(output, "CONTENT_TYPE",    lookupHeader(r, "Content-type"));
		addHeader(output, "DOCUMENT_ROOT",   ap_document_root(r));

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
			addHeader(output, "REQUEST_URI", r->unparsed_uri);
		} else {
			const char *request_uri;
			if (r->args != NULL) {
				request_uri = apr_pstrcat(r->pool, escapedUri, "?", r->args, (char *) NULL);
			} else {
				request_uri = escapedUri;
			}
			addHeader(output, "REQUEST_URI", request_uri);
		}

		if (baseURI == NULL) {
			addHeader(output, "SCRIPT_NAME", "");
			addHeader(output, "PATH_INFO", escapedUri);
		} else {
			addHeader(output, "SCRIPT_NAME", baseURI);
			addHeader(output, "PATH_INFO", escapedUri + strlen(baseURI));
		}

		// Set HTTP headers.
		const apr_array_header_t *hdrs_arr;
		apr_table_entry_t *hdrs;
		int i;

		hdrs_arr = apr_table_elts(r->headers_in);
		hdrs = (apr_table_entry_t *) hdrs_arr->elts;
		for (i = 0; i < hdrs_arr->nelts; ++i) {
			if (hdrs[i].key == NULL) {
				continue;
			}
			size_t keylen = strlen(hdrs[i].key);
			// We only pass the Transfer-Encoding header if PassengerBufferUpload is disabled,
			// so that the HelperAgent and the app knows that there is a request body despite
			// there not being a Content-Length header.
			if (!headerIsTransferEncoding(hdrs[i].key, keylen) || config->bufferUpload == DirConfig::DISABLED) {
				addHeader(output, httpToEnv(r->pool, hdrs[i].key, keylen), hdrs[i].val);
			}
		}

		// Add other environment variables.
		const apr_array_header_t *env_arr;
		apr_table_entry_t *env;

		env_arr = apr_table_elts(r->subprocess_env);
		env = (apr_table_entry_t*) env_arr->elts;
		for (i = 0; i < env_arr->nelts; ++i) {
			addHeader(output, env[i].key, env[i].val);
		}

		// Phusion Passenger options.
		addHeader(output, "PASSENGER_STATUS_LINE", "false");
		addHeader(output, "PASSENGER_APP_ROOT", mapper.getAppRoot());
		addHeader(output, "PASSENGER_APP_GROUP_NAME", config->getAppGroupName(mapper.getAppRoot()));
		#include "SetHeaders.cpp"
		addHeader(output, "PASSENGER_SPAWN_METHOD", config->getSpawnMethodString());
		addHeader(r, output, "PASSENGER_MAX_REQUEST_QUEUE_SIZE", config->maxRequestQueueSize);
		addHeader(output, "PASSENGER_APP_TYPE", mapper.getApplicationTypeName());
		addHeader(output, "PASSENGER_MAX_PRELOADER_IDLE_TIME",
			apr_psprintf(r->pool, "%ld", config->maxPreloaderIdleTime));
		addHeader(output, "PASSENGER_DEBUGGER", "false");
		addHeader(output, "PASSENGER_SHOW_VERSION_IN_HEADER", "true");
		addHeader(output, "PASSENGER_STAT_THROTTLE_RATE",
			apr_psprintf(r->pool, "%ld", config->getStatThrottleRate()));
		addHeader(output, "PASSENGER_RESTART_DIR", config->getRestartDir());
		addHeader(output, "PASSENGER_FRIENDLY_ERROR_PAGES",
			config->showFriendlyErrorPages() ? "true" : "false");
		if (config->useUnionStation() && !config->unionStationKey.empty()) {
			addHeader(output, "UNION_STATION_SUPPORT", "true");
			addHeader(output, "UNION_STATION_KEY", config->unionStationKey);
			if (!config->unionStationFilters.empty()) {
				addHeader(output, "UNION_STATION_FILTERS",
					config->getUnionStationFilterString());
			}
		}

		/*********************/
		/*********************/

		requestData.push_back(output);
		return output.size();
	}

	void throwUploadBufferingException(request_rec *r, int code) {
		DirConfig *config = getDirConfig(r);
		string message("An error occured while "
			"buffering HTTP upload data to "
			"a temporary file in ");
		message.append(getUploadBufferDir(config));

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

	string getUploadBufferDir(DirConfig *config) {
		ServerInstanceDir::GenerationPtr generation = agentsStarter.getGeneration();
		return config->getUploadBufferDir(generation.get());
	}

	/**
	 * Receive the HTTP upload data and buffer it into a BufferedUpload temp file.
	 *
	 * @param r The request.
	 * @throws RuntimeException
	 * @throws SystemException
	 * @throws IOException
	 */
	boost::shared_ptr<BufferedUpload> receiveRequestBody(request_rec *r) {
		TRACE_POINT();
		DirConfig *config = getDirConfig(r);
		boost::shared_ptr<BufferedUpload> tempFile;
		try {
			tempFile.reset(new BufferedUpload(getUploadBufferDir(config)));
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
	}

	void sendRequestBody(const FileDescriptor &fd, boost::shared_ptr<BufferedUpload> &uploadData) {
		TRACE_POINT();
		rewind(uploadData->handle);
		while (!feof(uploadData->handle)) {
			char buf[1024 * 32];
			size_t size;

			size = fread(buf, 1, sizeof(buf), uploadData->handle);
			try {
				writeExact(fd, buf, size);
			} catch (const SystemException &e) {
				if (e.code() == EPIPE || e.code() == ECONNRESET) {
					// The HelperAgent stopped reading the body, probably
					// because the application already sent EOF.
					return;
				} else {
					throw e;
				}
			}
		}
	}

	void sendRequestBody(const FileDescriptor &fd, request_rec *r) {
		TRACE_POINT();
		char buf[1024 * 32];
		apr_off_t len;

		try {
			while ((len = readRequestBodyFromApache(r, buf, sizeof(buf))) > 0) {
				writeExact(fd, buf, len);
			}
		} catch (const SystemException &e) {
			if (e.code() == EPIPE || e.code() == ECONNRESET) {
				// The HelperAgent stopped reading the body, probably
				// because the application already sent EOF.
				return;
			} else {
				throw e;
			}
		}
	}

public:
	Hooks(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
	    : cstat(1024),
	      agentsStarter(AS_APACHE)
	{
		serverConfig.finalize();
		Passenger::setLogLevel(serverConfig.logLevel);
		if (serverConfig.debugLogFile != NULL) {
			Passenger::setDebugFile(serverConfig.debugLogFile);
		}
		m_hasModRewrite = UNKNOWN;
		m_hasModDir = UNKNOWN;
		m_hasModAutoIndex = UNKNOWN;
		m_hasModXsendfile = UNKNOWN;

		P_DEBUG("Initializing Phusion Passenger...");
		ap_add_version_component(pconf, "Phusion_Passenger/" PASSENGER_VERSION);

		if (serverConfig.root == NULL) {
			throw ConfigurationException("The 'PassengerRoot' configuration option "
				"is not specified. This option is required, so please specify it. "
				"TIP: The correct value for this option was given to you by "
				"'passenger-install-apache2-module'.");
		}

		VariantMap params;
		params
			.setPid ("web_server_pid", getpid())
			.setUid ("web_server_worker_uid", unixd_config.user_id)
			.setGid ("web_server_worker_gid", unixd_config.group_id)
			.setInt ("log_level", serverConfig.logLevel)
			.set    ("debug_log_file", (serverConfig.debugLogFile == NULL) ? "" : serverConfig.debugLogFile)
			.set    ("temp_dir", serverConfig.tempDir)
			.setBool("user_switching", serverConfig.userSwitching)
			.set    ("default_user", serverConfig.defaultUser)
			.set    ("default_group", serverConfig.defaultGroup)
			.set    ("default_ruby", serverConfig.defaultRuby)
			.setInt ("max_pool_size", serverConfig.maxPoolSize)
			.setInt ("pool_idle_time", serverConfig.poolIdleTime)
			.set    ("analytics_log_user", serverConfig.analyticsLogUser)
			.set    ("analytics_log_group", serverConfig.analyticsLogGroup)
			.set    ("union_station_gateway_address", serverConfig.unionStationGatewayAddress)
			.setInt ("union_station_gateway_port", serverConfig.unionStationGatewayPort)
			.set    ("union_station_gateway_cert", serverConfig.unionStationGatewayCert)
			.set    ("union_station_proxy_address", serverConfig.unionStationProxyAddress)
			.setStrSet("prestart_urls", serverConfig.prestartURLs);

		serverConfig.ctl.addTo(params);

		agentsStarter.start(serverConfig.root, params);

		// Store some relevant information in the generation directory.
		string generationPath = agentsStarter.getGeneration()->getPath();
		server_rec *server;
		string configFiles;

		#ifdef AP_GET_SERVER_VERSION_DEPRECATED
			createFile(generationPath + "/web_server.txt",
				ap_get_server_description());
		#else
			createFile(generationPath + "/web_server.txt",
				ap_get_server_version());
		#endif

		for (server = s; server != NULL; server = server->next) {
			if (server->defn_name != NULL) {
				configFiles.append(server->defn_name);
				configFiles.append(1, '\n');
			}
		}
		createFile(generationPath + "/config_files.txt", configFiles);
	}

	void childInit(apr_pool_t *pchild, server_rec *s) {
		agentsStarter.detach();
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
	} catch (const std::exception &e) {
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
	if (hooks == NULL) {
		oxt::initialize();
	} else {
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
		int ret;

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
		ret = ::system("uname -a >&2");
		(void) ret; // Ignore compiler warning.

		fprintf(stderr, "\nOutput of 'ulimit -a' follows:\n");
		fflush(stderr);
		ret = ::system("ulimit -a >&2");
		(void) ret; // Ignore compiler warning.

		return DECLINED;

	} catch (const std::exception &e) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
			"*** Passenger could not be initialized because of this error: %s",
			e.what());
		hooks = NULL;
		return DECLINED;
	}
}

static void
child_init(apr_pool_t *pchild, server_rec *s) {
	if (OXT_LIKELY(hooks != NULL)) {
		hooks->childInit(pchild, s);
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
	ap_hook_child_init(child_init, NULL, NULL, APR_HOOK_MIDDLE);

	// The hooks here are defined in the order that they're called.

	ap_hook_map_to_storage(prepare_request_when_in_high_performance_mode, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_map_to_storage(save_original_filename, NULL, NULL, APR_HOOK_LAST);

	ap_hook_fixups(prepare_request_when_not_in_high_performance_mode, NULL, rewrite_module, APR_HOOK_FIRST);
	ap_hook_fixups(save_state_before_rewrite_rules, NULL, rewrite_module, APR_HOOK_LAST);
	ap_hook_fixups(undo_redirection_to_dispatch_cgi, rewrite_module, NULL, APR_HOOK_FIRST);
	ap_hook_fixups(start_blocking_mod_dir, NULL, dir_module, APR_HOOK_LAST);
	ap_hook_fixups(end_blocking_mod_dir, dir_module, NULL, APR_HOOK_LAST);

	ap_hook_handler(handle_request_when_in_high_performance_mode, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(start_blocking_mod_autoindex, NULL, autoindex_module, APR_HOOK_LAST);
	ap_hook_handler(end_blocking_mod_autoindex, autoindex_module, NULL, APR_HOOK_FIRST);
	ap_hook_handler(handle_request_when_not_in_high_performance_mode, NULL, NULL, APR_HOOK_LAST);
}

/**
 * @}
 */

