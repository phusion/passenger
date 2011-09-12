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
#ifndef _PASSENGER_RESOURCE_LOCATOR_H_
#define _PASSENGER_RESOURCE_LOCATOR_H_

#include <string>
#include <IniFile.h>
#include <Exceptions.h>
#include <Utils.h>

namespace Passenger {

using namespace boost;


/**
 * Locates various Phusion Passenger resources on the filesystem.
 */
class ResourceLocator {
private:
	string binDir;
	string agentsDir;
	string helperScriptsDir;
	string resourcesDir;
	string docDir;
	string rubyLibDir;
	string compilableSourceDir;
	string headerDir;
	
	static string getOption(const string &file, const IniFileSectionPtr &section, const string &key) {
		if (section->hasKey(key)) {
			return section->get(key);
		} else {
			throw RuntimeException("Option '" + key + "' missing in file '" + file + "'");
		}
	}
	
public:
	ResourceLocator(const string &rootOrFile) {
		if (getFileType(rootOrFile) == FT_REGULAR) {
			string file = rootOrFile;
			IniFileSectionPtr options = IniFile(file).section("locations");
			binDir              = getOption(file, options, "bin");
			agentsDir           = getOption(file, options, "agents");
			helperScriptsDir    = getOption(file, options, "helper_scripts");
			resourcesDir        = getOption(file, options, "resources");
			docDir              = getOption(file, options, "doc");
			rubyLibDir          = getOption(file, options, "rubylib");
			compilableSourceDir = getOption(file, options, "compilable_source");
			headerDir           = getOption(file, options, "headers");
		} else {
			string root = rootOrFile;
			bool originallyPackaged = fileExists(root + "/Rakefile")
				&& fileExists(root + "/DEVELOPERS.TXT");
			
			if (originallyPackaged) {
				binDir              = root + "/bin";
				agentsDir           = root + "/agents";
				helperScriptsDir    = root + "/helper-scripts";
				resourcesDir        = root + "/resources";
				docDir              = root + "/doc";
				rubyLibDir          = root + "/lib";
				compilableSourceDir = root;
				headerDir           = root + "/ext";
			} else {
				string NAMESPACE_DIR = "phusion-passenger";
				
				binDir              = "/usr/bin";
				agentsDir           = "/usr/lib/" + NAMESPACE_DIR + "/agents";
				helperScriptsDir    = "/usr/share/" + NAMESPACE_DIR + "/helper-scripts";
				resourcesDir        = "/usr/share/" + NAMESPACE_DIR;
				docDir              = "/usr/share/doc/" + NAMESPACE_DIR;
				rubyLibDir          = "";
				compilableSourceDir = "";
				headerDir           = "/usr/include/" + NAMESPACE_DIR;
			}
		}
	}
	
	string getAgentsDir() const {
		return agentsDir;
	}
	
	string getHelperScriptsDir() const {
		return helperScriptsDir;
	}
	
	string getSpawnServerFilename() const {
		return getHelperScriptsDir() + "/passenger-spawn-server";
	}
	
	string getResourcesDir() const {
		return resourcesDir;
	}
	
	string getDocDir() const {
		return docDir;
	}
	
	// Can be empty.
	string getRubyLibDir() const {
		return rubyLibDir;
	}
	
	// Directory must contain native_support and Nginx module.
	string getCompilableSourceDir() const {
		return compilableSourceDir;
	}
	
	// Directory must contain ext/boost, ext/oxt and ext/common headers.
	string getHeaderDir() const {
		return headerDir;
	}
};


}

#endif /* _PASSENGER_RESOURCE_LOCATOR_H_ */
