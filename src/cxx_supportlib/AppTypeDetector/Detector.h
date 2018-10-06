/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_APP_TYPE_DECTECTOR_H_
#define _PASSENGER_APP_TYPE_DECTECTOR_H_

#include <limits.h>

#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>

#include <cassert>
#include <cstddef>
#include <string>

#include <Exceptions.h>
#include <WrapperRegistry/Registry.h>
#include <FileTools/PathManip.h>
#include <FileTools/FileManip.h>
#include <Utils.h>
#include <Utils/CachedFileStat.hpp>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace AppTypeDetector {

using namespace std;


class Detector {
public:
	struct Result {
		const WrapperRegistry::Entry *wrapperRegistryEntry;

		Result()
			: wrapperRegistryEntry(NULL)
			{ }

		bool isNull() const {
			return wrapperRegistryEntry == NULL;
		}
	};

private:
	const WrapperRegistry::Registry &registry;
	CachedFileStat *cstat;
	boost::mutex *cstatMutex;
	unsigned int throttleRate;
	bool ownsCstat;

	bool check(char *buf, const char *end, const StaticString &appRoot,
		const StaticString &name)
	{
		char *pos = buf;
		pos = appendData(pos, end, appRoot);
		pos = appendData(pos, end, "/", 1);
		pos = appendData(pos, end, name);
		pos = appendData(pos, end, "\0", 1);
		if (OXT_UNLIKELY(pos == end)) {
			TRACE_POINT();
			throw RuntimeException("Not enough buffer space");
		}
		return getFileType(StaticString(buf, pos - buf - 1),
			cstat, cstatMutex, throttleRate) != FT_NONEXISTANT;
	}

public:
	Detector(const WrapperRegistry::Registry &_registry,
		CachedFileStat *_cstat = NULL, boost::mutex *_cstatMutex = NULL,
		unsigned int _throttleRate = 1)
		: registry(_registry),
		  cstat(_cstat),
		  cstatMutex(_cstatMutex),
		  throttleRate(_throttleRate),
		  ownsCstat(false)
	{
		assert(_registry.isFinalized());
		if (_cstat == NULL) {
			cstat = new CachedFileStat();
			ownsCstat = true;
		}
	}

	~Detector() {
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
	 * application that lives there. Returns a null result if it wasn't able to detect
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
	const Result checkDocumentRoot(const StaticString &documentRoot,
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
	 * directory `appRoot`. Returns a null result if it wasn't able to detect
	 * a supported application type.
	 *
	 * @throws FileSystemException Unable to check because of a filesystem error.
	 * @throws TimeRetrievalException
	 * @throws boost::thread_interrupted
	 */
	const Result checkAppRoot(const StaticString &appRoot) {
		char buf[PATH_MAX + 32];
		const char *end = buf + sizeof(buf) - 1;

		WrapperRegistry::Registry::ConstIterator it(registry.getIterator());
		while (*it != NULL) {
			const WrapperRegistry::Entry &entry = it.getValue();
			foreach (const StaticString &defaultStartupFile,
				entry.defaultStartupFiles)
			{
				if (check(buf, end, appRoot, defaultStartupFile)) {
					Result result;
					result.wrapperRegistryEntry = &entry;
					return result;
				}
			}
			it.next();
		}

		return Result();
	}
};


} // namespace AppTypeDetector
} // namespace Passenger

#endif /* _PASSENGER_APP_TYPE_DECTECTOR_H_ */
