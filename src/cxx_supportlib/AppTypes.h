/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2014 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_APP_TYPES_H_
#define _PASSENGER_APP_TYPES_H_

/**
 * Application type registry
 *
 * All supported application types (e.g. Rack, classic Rails, WSGI, etc)
 * are registered here. The AppTypeDetector is responsible for checking
 * what kind of application lives under the given directory.
 */

#include "Exceptions.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
	PAT_RACK,
	PAT_WSGI,
	PAT_NODE,
	PAT_METEOR,
	PAT_NONE,
	PAT_ERROR
} PassengerAppType;

typedef void PP_AppTypeDetector;

PP_AppTypeDetector *pp_app_type_detector_new(unsigned int throttleRate);
void pp_app_type_detector_free(PP_AppTypeDetector *detector);
void pp_app_type_detector_set_throttle_rate(PP_AppTypeDetector *detector,
	unsigned int throttleRate);
PassengerAppType pp_app_type_detector_check_document_root(PP_AppTypeDetector *detector,
	const char *documentRoot, unsigned int len, int resolveFirstSymlink,
	PP_Error *error);
PassengerAppType pp_app_type_detector_check_app_root(PP_AppTypeDetector *detector,
	const char *appRoot, unsigned int len, PP_Error *error);

const char *pp_get_app_type_name(PassengerAppType type);
PassengerAppType pp_get_app_type2(const char *name, unsigned int len);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#ifdef __cplusplus
#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>
#include <boost/thread.hpp>
#include <cstdlib>
#include <limits.h>
#include <string>
#include <Logging.h>
#include <StaticString.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/CachedFileStat.hpp>

namespace Passenger {

using namespace std;


struct AppTypeDefinition {
	const PassengerAppType type;
	const char * const name;
	const char * const startupFile;
	const char * const processTitle;
};

extern const AppTypeDefinition appTypeDefinitions[];


class AppTypeDetector {
private:
	CachedFileStat *cstat;
	boost::mutex *cstatMutex;
	unsigned int throttleRate;
	bool ownsCstat;

	bool check(char *buf, const char *end, const StaticString &appRoot, const char *name) {
		char *pos = buf;
		pos = appendData(pos, end, appRoot);
		pos = appendData(pos, end, "/");
		pos = appendData(pos, end, name);
		pos = appendData(pos, end, "\0", 1);
		if (OXT_UNLIKELY(pos == end)) {
			TRACE_POINT();
			throw RuntimeException("Not enough buffer space");
		}
		return getFileType(StaticString(buf, pos - buf - 1), cstat, cstatMutex, throttleRate) != FT_NONEXISTANT;
	}

public:
	AppTypeDetector(CachedFileStat *_cstat = NULL, boost::mutex *_cstatMutex = NULL, unsigned int _throttleRate = 1)
		: cstat(_cstat),
		  cstatMutex(_cstatMutex),
		  throttleRate(_throttleRate),
		  ownsCstat(false)
	{
		if (_cstat == NULL) {
			cstat = new CachedFileStat();
			ownsCstat = true;
		}
	}

	~AppTypeDetector() {
		if (ownsCstat) {
			delete cstat;
		}
	}

	void setThrottleRate(unsigned int val) {
		throttleRate = val;
	}

	/**
	 * Given a web server document root (that is, some subdirectory under the
	 * application root, e.g. "/webapps/foobar/public"), returns the type of
	 * application that lives there. Returns PAT_NONE if it wasn't able to detect
	 * a supported application type.
	 *
	 * If `resolveFirstSymlink` is given, and `documentRoot` is a symlink, then
	 * this function will check the parent directory
	 * of the directory that the symlink points to (i.e. `resolve(documentRoot) + "/.."`),
	 * instead of checking the directory that the symlink is located in (i.e.
	 * `dirname(documentRoot)`).
	 *
	 * If `appRoot` is non-NULL, then the inferred application root will be stored here.
	 *
	 * @throws FileSystemException Unable to check because of a filesystem error.
	 * @throws TimeRetrievalException
	 * @throws boost::thread_interrupted
	 */
	PassengerAppType checkDocumentRoot(const StaticString &documentRoot,
		bool resolveFirstSymlink = false,
		string *appRoot = NULL)
	{
		if (!resolveFirstSymlink) {
			if (appRoot != NULL) {
				*appRoot = extractDirNameStatic(documentRoot);
				return checkAppRoot(*appRoot);
			} else {
				return checkAppRoot(extractDirNameStatic(documentRoot));
			}
		} else {
			if (OXT_UNLIKELY(documentRoot.size() > PATH_MAX)) {
				TRACE_POINT();
				throw RuntimeException("Not enough buffer space");
			}

			char ntDocRoot[PATH_MAX + 1];
			memcpy(ntDocRoot, documentRoot.data(), documentRoot.size());
			ntDocRoot[documentRoot.size()] = '\0';
			string resolvedDocumentRoot = resolveSymlink(ntDocRoot);
			if (appRoot != NULL) {
				*appRoot = extractDirNameStatic(resolvedDocumentRoot);
				return checkAppRoot(*appRoot);
			} else {
				return checkAppRoot(extractDirNameStatic(resolvedDocumentRoot));
			}
		}
	}

	/**
	 * Returns the type of application that lives under the application
	 * directory `appRoot`. Returns PAT_NONE if it wasn't able to detect
	 * a supported application type.
	 *
	 * @throws FileSystemException Unable to check because of a filesystem error.
	 * @throws TimeRetrievalException
	 * @throws boost::thread_interrupted
	 */
	PassengerAppType checkAppRoot(const StaticString &appRoot) {
		char buf[PATH_MAX + 32];
		const char *end = buf + sizeof(buf) - 1;
		const AppTypeDefinition *definition = &appTypeDefinitions[0];

		while (definition->type != PAT_NONE) {
			if (check(buf, end, appRoot, definition->startupFile)) {
				return definition->type;
			}
			definition++;
		}
		return PAT_NONE;
	}
};


inline const char *
getAppTypeName(PassengerAppType type) {
	const AppTypeDefinition *definition = &appTypeDefinitions[0];

	while (definition->type != PAT_NONE) {
		if (definition->type == type) {
			return definition->name;
		}
		definition++;
	}
	return NULL;
}

inline PassengerAppType
getAppType(const StaticString &name) {
	const AppTypeDefinition *definition = &appTypeDefinitions[0];

	while (definition->type != PAT_NONE) {
		if (name == definition->name) {
			return definition->type;
		}
		definition++;
	}
	return PAT_NONE;
}

inline const char *
getAppTypeStartupFile(PassengerAppType type) {
	const AppTypeDefinition *definition = &appTypeDefinitions[0];

	while (definition->type != PAT_NONE) {
		if (definition->type == type) {
			return definition->startupFile;
		}
		definition++;
	}
	return NULL;
}

inline const char *
getAppTypeProcessTitle(PassengerAppType type) {
	const AppTypeDefinition *definition = &appTypeDefinitions[0];

	while (definition->type != PAT_NONE) {
		if (definition->type == type) {
			return definition->processTitle;
		}
		definition++;
	}
	return NULL;
}


} // namespace Passenger
#endif /* __cplusplus */

#endif /* _PASSENGER_APP_TYPES_H_ */
