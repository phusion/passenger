/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_DIRECTORY_MAPPER_H_
#define _PASSENGER_DIRECTORY_MAPPER_H_

#include <string>
#include <set>
#include <cstring>

#include <oxt/backtrace.hpp>

#include "Configuration.hpp"
#include "Utils.h"
#include "Utils/CachedFileStat.hpp"

// The Apache/APR headers *must* come after the Boost headers, otherwise
// compilation will fail on OpenBSD.
#include <httpd.h>
#include <http_core.h>

namespace Passenger {

using namespace std;
using namespace oxt;

/**
 * Utility class for determining URI-to-application directory mappings.
 * Given a URI, it will determine whether that URI belongs to a Phusion
 * Passenger-handled application, what the base URI of that application is,
 * and what the associated 'public' directory is.
 *
 * @note This class is not thread-safe, but is reentrant.
 * @ingroup Core
 */
class DirectoryMapper {
public:
	enum ApplicationType {
		NONE,
		RAILS,
		RACK,
		WSGI
	};

private:
	DirConfig *config;
	request_rec *r;
	CachedFileStat *cstat;
	unsigned int throttleRate;
	bool baseURIKnown;
	const char *baseURI;
	ApplicationType appType;
	
	inline bool shouldAutoDetectRails() {
		return config->autoDetectRails == DirConfig::ENABLED ||
			config->autoDetectRails == DirConfig::UNSET;
	}
	
	inline bool shouldAutoDetectRack() {
		return config->autoDetectRack == DirConfig::ENABLED ||
			config->autoDetectRack == DirConfig::UNSET;
	}
	
	inline bool shouldAutoDetectWSGI() {
		return config->autoDetectWSGI == DirConfig::ENABLED ||
			config->autoDetectWSGI == DirConfig::UNSET;
	}
	
public:
	/**
	 * Create a new DirectoryMapper object.
	 *
	 * @param cstat A CachedFileStat object used for statting files.
	 * @param throttleRate A throttling rate for cstat.
	 * @warning Do not use this object after the destruction of <tt>r</tt>,
	 *          <tt>config</tt> or <tt>cstat</tt>.
	 */
	DirectoryMapper(request_rec *r, DirConfig *config,
	                CachedFileStat *cstat, unsigned int throttleRate) {
		this->r = r;
		this->config = config;
		this->cstat = cstat;
		this->throttleRate = throttleRate;
		appType = NONE;
		baseURIKnown = false;
		baseURI = NULL;
	}
	
	/**
	 * Determine whether the given HTTP request falls under one of the specified
	 * RailsBaseURIs or RackBaseURIs. If yes, then the first matching base URI will
	 * be returned.
	 *
	 * If Rails/Rack autodetection was enabled in the configuration, and the document
	 * root seems to be a valid Rails/Rack 'public' folder, then this method will
	 * return "/".
	 *
	 * Otherwise, NULL will be returned.
	 *
	 * @throws FileSystemException This method might also examine the filesystem in
	 *            order to detect the application's type. During that process, a
	 *            FileSystemException might be thrown.
	 * @warning The return value may only be used as long as <tt>config</tt>
	 *          hasn't been destroyed.
	 */
	const char *getBaseURI() {
		TRACE_POINT();
		if (baseURIKnown) {
			return baseURI;
		}
		
		set<string>::const_iterator it;
		const char *uri = r->uri;
		size_t uri_len = strlen(uri);
		
		if (uri_len == 0 || uri[0] != '/') {
			baseURIKnown = true;
			return NULL;
		}
		
		UPDATE_TRACE_POINT();
		for (it = config->railsBaseURIs.begin(); it != config->railsBaseURIs.end(); it++) {
			const string &base(*it);
			if (  base == "/"
			 || ( uri_len == base.size() && memcmp(uri, base.c_str(), uri_len) == 0 )
			 || ( uri_len  > base.size() && memcmp(uri, base.c_str(), base.size()) == 0
			                             && uri[base.size()] == '/' )
			) {
				baseURIKnown = true;
				baseURI = base.c_str();
				appType = RAILS;
				return baseURI;
			}
		}
		
		UPDATE_TRACE_POINT();
		for (it = config->rackBaseURIs.begin(); it != config->rackBaseURIs.end(); it++) {
			const string &base(*it);
			if (  base == "/"
			 || ( uri_len == base.size() && memcmp(uri, base.c_str(), uri_len) == 0 )
			 || ( uri_len  > base.size() && memcmp(uri, base.c_str(), base.size()) == 0
			                             && uri[base.size()] == '/' )
			) {
				baseURIKnown = true;
				baseURI = base.c_str();
				appType = RACK;
				return baseURI;
			}
		}
		
		UPDATE_TRACE_POINT();
		if (shouldAutoDetectRack()
		 && verifyRackDir(config->getAppRoot(ap_document_root(r)), cstat, throttleRate)) {
			baseURIKnown = true;
			baseURI = "/";
			appType = RACK;
			return baseURI;
		}
		
		UPDATE_TRACE_POINT();
		if (shouldAutoDetectRails()
		 && verifyRailsDir(config->getAppRoot(ap_document_root(r)), cstat, throttleRate)) {
			baseURIKnown = true;
			baseURI = "/";
			appType = RAILS;
			return baseURI;
		}
		
		UPDATE_TRACE_POINT();
		if (shouldAutoDetectWSGI()
		 && verifyWSGIDir(config->getAppRoot(ap_document_root(r)), cstat, throttleRate)) {
			baseURIKnown = true;
			baseURI = "/";
			appType = WSGI;
			return baseURI;
		}
		
		baseURIKnown = true;
		return NULL;
	}
	
	/**
	 * Returns the filename of the 'public' directory of the Rails/Rack application
	 * that's associated with the HTTP request.
	 *
	 * Returns an empty string if the document root of the HTTP request
	 * cannot be determined, or if it isn't a valid folder.
	 *
	 * @throws FileSystemException An error occured while examening the filesystem.
	 */
	string getPublicDirectory() {
		if (!baseURIKnown) {
			getBaseURI();
		}
		if (baseURI == NULL) {
			return "";
		}
		
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
				/* Application is deployed in a sub-URI.
				 * This is probably a symlink, so let's resolve it.
				 */
				path.append(baseURI);
				path = resolveSymlink(path);
			}
			return path;
		} else {
			return "";
		}
	}
	
	/**
	 * Returns the application type that's associated with the HTTP request.
	 *
	 * @throws FileSystemException An error occured while examening the filesystem.
	 */
	ApplicationType getApplicationType() {
		if (!baseURIKnown) {
			getBaseURI();
		}
		return appType;
	}
	
	/**
	 * Returns the application type (as a string) that's associated
	 * with the HTTP request.
	 *
	 * @throws FileSystemException An error occured while examening the filesystem.
	 */
	const char *getApplicationTypeString() {
		if (!baseURIKnown) {
			getBaseURI();
		}
		switch (appType) {
		case RAILS:
			return "rails";
		case RACK:
			return "rack";
		case WSGI:
			return "wsgi";
		default:
			return NULL;
		};
	}
};

} // namespace Passenger

#endif /* _PASSENGER_DIRECTORY_MAPPER_H_ */

