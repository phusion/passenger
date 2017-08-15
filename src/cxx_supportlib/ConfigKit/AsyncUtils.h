/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_CONFIG_KIT_ASYNC_UTILS_H_
#define _PASSENGER_CONFIG_KIT_ASYNC_UTILS_H_

#include <vector>
#include <boost/function.hpp>
#include <ConfigKit/Common.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


template<typename Component>
struct CallbackTypes {
	typedef
		boost::function<void (const vector<Error> &errors, typename Component::ConfigChangeRequest &req)>
		PrepareConfigChange;
	typedef
		boost::function<void (typename Component::ConfigChangeRequest &req)>
		CommitConfigChange;
	typedef
		boost::function<void (const Json::Value &config)>
		InspectConfig;
};


template<typename Component>
inline void
callPrepareConfigChangeAndCallback(Component *component, Json::Value updates,
	typename Component::ConfigChangeRequest *req,
	const typename CallbackTypes<Component>::PrepareConfigChange &callback)
{
	vector<Error> errors;
	component->prepareConfigChange(updates, errors, *req);
	callback(errors, *req);
}

template<typename Component>
inline void
callCommitConfigChangeAndCallback(Component *component,
	typename Component::ConfigChangeRequest *req,
	const typename CallbackTypes<Component>::CommitConfigChange &callback)
{
	component->commitConfigChange(*req);
	callback(*req);
}

template<typename Component>
inline void
callInspectConfigAndCallback(Component *component,
	const typename CallbackTypes<Component>::InspectConfig &callback)
{
	callback(component->inspectConfig());
}


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_ASYNC_UTILS_H_ */
