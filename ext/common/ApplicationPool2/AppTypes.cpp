/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013 Phusion
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

#include <ApplicationPool2/AppTypes.h>
#include <exception>
#include <string.h>

namespace Passenger {
namespace ApplicationPool2 {

/* If you update this structure, also update the following:
 * - ApplicationPool2::Options::getStartCommand()
 * - lib/phusion_passenger/standalone/app_finder.rb
 * - The documentation for `PassengerAppEnv` (Apache) and `passenger_app_env` (Nginx)
 * - The Developer Guide, section "Executing the loader or preloader"
 */
const AppTypeDefinition appTypeDefinitions[] = {
	{ PAT_RACK, "rack", "config.ru", "Passenger RackApp" },
	{ PAT_WSGI, "wsgi", "passenger_wsgi.py", "Passenger WsgiApp" },
	{ PAT_CLASSIC_RAILS, "classic-rails", "config/environment.rb", "Passenger ClassicRailsApp" },
	{ PAT_NODE, "node", "app.js", "Passenger NodeApp" },
	{ PAT_METEOR, "meteor", ".meteor", "Passenger MeteorApp" },
	{ PAT_NONE, NULL, NULL, NULL }
};

} // namespace ApplicationPool2
} // namespace Passenger


using namespace Passenger;
using namespace Passenger::ApplicationPool2;

PP_AppTypeDetector *
pp_app_type_detector_new() {
	try {
		return new AppTypeDetector();
	} catch (const std::bad_alloc &) {
		return 0;
	}
}

void
pp_app_type_detector_free(PP_AppTypeDetector *detector) {
	delete (AppTypeDetector *) detector;
}

PassengerAppType
pp_app_type_detector_check_document_root(PP_AppTypeDetector *_detector,
	const char *documentRoot, unsigned int len, int resolveFirstSymlink,
	PP_Error *error)
{
	AppTypeDetector *detector = (AppTypeDetector *) _detector;
	try {
		return detector->checkDocumentRoot(StaticString(documentRoot, len), resolveFirstSymlink);
	} catch (const std::exception &e) {
		pp_error_set(e, error);
		return PAT_ERROR;
	}
}

PassengerAppType
pp_app_type_detector_check_app_root(PP_AppTypeDetector *_detector,
	const char *appRoot, unsigned int len, PP_Error *error)
{
	AppTypeDetector *detector = (AppTypeDetector *) _detector;
	try {
		return detector->checkAppRoot(StaticString(appRoot, len));
	} catch (const std::exception &e) {
		pp_error_set(e, error);
		return PAT_ERROR;
	}
}

const char *
pp_get_app_type_name(PassengerAppType type) {
	return getAppTypeName(type);
}

PassengerAppType
pp_get_app_type2(const char *name, unsigned int len) {
	return getAppType(StaticString(name, len));
}
