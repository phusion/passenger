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
#include <Core/Controller.h>

namespace Passenger {
namespace Core {

using namespace std;


/****************************
 *
 * Private methods
 *
 ****************************/


bool
Controller::prepareConfigChange(const Json::Value &updates,
	vector<ConfigKit::Error> &errors,
	ControllerConfigChangeRequest &req)
{
	if (ParentClass::prepareConfigChange(updates, errors, req.forParent)) {
		req.mainConfig.reset(new ControllerMainConfig(
			*req.forParent.forParent.config));
		req.requestConfig.reset(new ControllerRequestConfig(
			*req.forParent.forParent.config));
	}
	return errors.empty();
}

void
Controller::commitConfigChange(ControllerConfigChangeRequest &req)
	BOOST_NOEXCEPT_OR_NOTHROW
{
	ParentClass::commitConfigChange(req.forParent);
	mainConfig.swap(*req.mainConfig);
	requestConfig.swap(req.requestConfig);
}


} // namespace Core
} // namespace Passenger
