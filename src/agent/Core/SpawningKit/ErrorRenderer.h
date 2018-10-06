/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_ERROR_RENDERER_H_
#define _PASSENGER_SPAWNING_KIT_ERROR_RENDERER_H_

#include <string>
#include <map>
#include <cctype>

#include <jsoncpp/json.h>

#include <Constants.h>
#include <StaticString.h>
#include <FileTools/FileManip.h>
#include <StrIntTools/Template.h>
#include <Core/SpawningKit/Context.h>
#include <Core/SpawningKit/Exceptions.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class ErrorRenderer {
private:
	string templatesDir;

public:
	ErrorRenderer(const Context &context) {
		templatesDir = context.resourceLocator->getResourcesDir() + "/templates/error_renderer";
	}

	string renderWithDetails(const SpawningKit::SpawnException &e) const {
		StringMap<StaticString> params;
		string htmlFile = templatesDir + "/with_details/src/index.html.template";
		string cssFile = templatesDir + "/with_details/dist/styles.css";
		string jsFile = templatesDir + "/with_details/dist/bundle.js";
		string cssContent = unsafeReadFile(cssFile);
		string jsContent = unsafeReadFile(jsFile);

		Json::Value spec;
		spec["program_name"] = PROGRAM_NAME;
		spec["short_program_name"] = SHORT_PROGRAM_NAME;
		spec["config"] = e.getConfig().getNonConfidentialFieldsToPassToApp();
		spec["journey"] = e.getJourney().inspectAsJson();
		spec["error"] = e.inspectBasicInfoAsJson();
		spec["diagnostics"]["system_wide"] = e.inspectSystemWideDetailsAsJson();
		spec["diagnostics"]["core_process"] = e.inspectParentProcessDetailsAsJson();
		if (e.getJourney().getType() == SPAWN_THROUGH_PRELOADER) {
			spec["diagnostics"]["preloader_process"] =
				e.inspectPreloaderProcessDetailsAsJson();
		}
		spec["diagnostics"]["subprocess"] = e.inspectSubprocessDetailsAsJson();

		string specContent = spec.toStyledString();

		params.set("CSS", cssContent);
		params.set("JS", jsContent);
		params.set("TITLE", "Web application could not be started");
		params.set("SPEC", specContent);

		return Template::apply(unsafeReadFile(htmlFile), params);
	}

	string renderWithoutDetails(const SpawningKit::SpawnException &e) const {
		StringMap<StaticString> params;
		string htmlFile = templatesDir + "/without_details/src/index.html.template";
		string cssFile = templatesDir + "/without_details/dist/styles.css";
		string jsFile = templatesDir + "/without_details/dist/bundle.js";
		string cssContent = unsafeReadFile(cssFile);
		string jsContent = unsafeReadFile(jsFile);

		params.set("CSS", cssContent);
		params.set("JS", jsContent);
		params.set("TITLE", "Web application could not be started");
		params.set("SUMMARY", e.getSummary());
		params.set("ERROR_ID", e.getId());
		params.set("PROGRAM_NAME", PROGRAM_NAME);
		params.set("SHORT_PROGRAM_NAME", SHORT_PROGRAM_NAME);
		params.set("PROGRAM_WEBSITE", PROGRAM_WEBSITE);
		params.set("PROGRAM_AUTHOR", PROGRAM_AUTHOR);

		return Template::apply(unsafeReadFile(htmlFile), params);
	}
};


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_ERROR_RENDERER_H_ */
