/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_DIRECTORY_MAPPER_H_
#define _PASSENGER_DIRECTORY_MAPPER_H_

#include <string>
#include <set>
#include <cstring>

#include <oxt/backtrace.hpp>

#include "CachedFileStat.h"
#include "Configuration.h"
#include "Utils.h"

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
	CachedMultiFileStat *mstat;
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
	 * @param mstat A CachedMultiFileStat object used for statting files.
	 * @param throttleRate A throttling rate for mstat.
	 * @warning Do not use this object after the destruction of <tt>r</tt>,
	 *          <tt>config</tt> or <tt>mstat</tt>.
	 */
	DirectoryMapper(request_rec *r, DirConfig *config,
	                CachedMultiFileStat *mstat, unsigned int throttleRate) {
		this->r = r;
		this->config = config;
		this->mstat = mstat;
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
		if (shouldAutoDetectRails()
		 && verifyRailsDir(config->getAppRoot(ap_document_root(r)), mstat, throttleRate)) {
			baseURIKnown = true;
			baseURI = "/";
			appType = RAILS;
			return baseURI;
		}
		
		UPDATE_TRACE_POINT();
		if (shouldAutoDetectRack()
		 && verifyRackDir(config->getAppRoot(ap_document_root(r)), mstat, throttleRate)) {
			baseURIKnown = true;
			baseURI = "/";
			appType = RACK;
			return baseURI;
		}
		
		UPDATE_TRACE_POINT();
		if (shouldAutoDetectWSGI()
		 && verifyWSGIDir(config->getAppRoot(ap_document_root(r)), mstat, throttleRate)) {
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
	
	/**
	 * Returns the environment under which the application should be spawned.
	 *
	 * @throws FileSystemException An error occured while examening the filesystem.
	 */
	const char *getEnvironment() {
		switch (getApplicationType()) {
		case RAILS:
			return config->getRailsEnv();
		case RACK:
			return config->getRackEnv();
		default:
			return "production";
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_DIRECTORY_MAPPER_H_ */

