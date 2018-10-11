/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  See LICENSE file for license information.
 */
#ifndef _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_
#define _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_

#include <boost/predef.h>
#include <FileTools/FileManip.h>

namespace Passenger {

using namespace std;


inline bool
autoDetectInContainer() {
	#if BOOST_OS_LINUX
		// https://github.com/moby/moby/issues/26102#issuecomment-253621560
		return fileExists("/.dockerenv");
	#else
		return false;
	#endif
}


} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_ */
