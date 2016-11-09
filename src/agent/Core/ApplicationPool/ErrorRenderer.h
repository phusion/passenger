/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL2_ERROR_RENDERER_H_
#define _PASSENGER_APPLICATION_POOL2_ERROR_RENDERER_H_

#include <string>
#include <map>
#include <cctype>

#include <Constants.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <Utils/StringMap.h>
#include <Utils/Template.h>
#include <Utils/IOUtils.h>
#include <Core/ApplicationPool/Options.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class ErrorRenderer {
private:
	string templatesDir, cssFile, errorLayoutFile;

public:
	ErrorRenderer(const ResourceLocator &resourceLocator) {
		templatesDir = resourceLocator.getResourcesDir() + "/templates";
		cssFile = templatesDir + "/error_layout.css";
		errorLayoutFile = templatesDir + "/error_layout.html.template";
	}

	string renderWithDetails(const StaticString &message,
		const Options &options,
		const SpawnException *e = NULL) const
	{
		string generalErrorFile =
			(e != NULL && e->isHTML())
			? templatesDir + "/general_error_with_html.html.template"
			: templatesDir + "/general_error.html.template";
		string css = readAll(cssFile);
		StringMap<StaticString> params;

		params.set("CSS", css);
		params.set("APP_ROOT", options.appRoot);
		params.set("RUBY", options.ruby);
		params.set("ENVIRONMENT", options.environment);
		params.set("MESSAGE", message);
		params.set("IS_RUBY_APP",
			(options.appType == "rack")
			? "true" : "false");
		if (e != NULL) {
			params.set("TITLE", "Web application could not be started");
			// Store all SpawnException annotations into 'params',
			// but convert its name to uppercase.
			const map<string, string> &annotations = e->getAnnotations();
			map<string, string>::const_iterator it, end = annotations.end();
			for (it = annotations.begin(); it != end; it++) {
				string name = it->first;
				for (string::size_type i = 0; i < name.size(); i++) {
					name[i] = toupper(name[i]);
				}
				params.set(name, it->second);
			}
		} else {
			params.set("TITLE", "Internal server error");
		}

		string content = Template::apply(readAll(generalErrorFile), params);
		params.set("CONTENT", content);

		return Template::apply(readAll(errorLayoutFile), params);
	}

	string renderWithoutDetails(const SpawnException *e = NULL) const {
		string templateFile = templatesDir + "/undisclosed_error.html.template";
		string css = readAll(cssFile);
		StringMap<StaticString> params;

		params.set("PROGRAM_NAME", PROGRAM_NAME);
		params.set("CSS", css);
		params.set("TITLE", "Web application could not be started");

		if (e != NULL) {
			const map<string, string> &annotations = e->getAnnotations();
			map<string, string>::const_iterator it = annotations.find("error_id");
			if (it != annotations.end()) {
				params.set("ERROR_ID", it->second);
			} else {
				params.set("ERROR_ID", "not available");
			}
		} else {
			params.set("ERROR_ID", "not available");
		}
		return Template::apply(readAll(templateFile), params);
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_ERROR_RENDERER_H_ */
