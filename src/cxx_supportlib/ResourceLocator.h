/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_RESOURCE_LOCATOR_H_
#define _PASSENGER_RESOURCE_LOCATOR_H_

#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <algorithm>
#include <pwd.h>
#include <Constants.h>
#include <Exceptions.h>
#include <FileTools/FileManip.h>
#include <SystemTools/UserDatabase.h>
#include <Utils.h>
#include <Utils/IniFile.h>

namespace Passenger {

using namespace std;
using namespace boost;


/**
 * Locates various Phusion Passenger resources on the filesystem. All Phusion Passenger
 * files are located through this class. There's similar code in src/ruby_supportlib/phusion_passenger.rb.
 * See doc/Packaging.txt.md for an introduction about where Phusion Passenger expects its
 * files to be located.
 */
class ResourceLocator {
private:
	string installSpec;
	string packagingMethod;
	string binDir;
	string supportBinariesDir;
	string helperScriptsDir;
	string resourcesDir;
	string docDir;
	string rubyLibDir;
	string nodeLibDir;
	string buildSystemDir;
	bool originallyPackaged;

	static string getOption(const string &file, const IniFileSectionPtr &section, const string &key) {
		if (section->hasKey(key)) {
			return section->get(key);
		} else {
			throw RuntimeException("Option '" + key + "' missing in file '" + file + "'");
		}
	}

	static string getOptionalSection(const string &file, const IniFileSectionPtr &section, const string &key) {
		if (section->hasKey(key)) {
			return section->get(key);
		} else {
			return string();
		}
	}

public:
	ResourceLocator() { }

	ResourceLocator(const string &_installSpec)
		: installSpec(_installSpec)
	{
		if (getFileType(_installSpec) == FT_REGULAR) {
			const string &file = _installSpec;
			originallyPackaged = false;
			IniFileSectionPtr options = IniFile(file).section("locations");
			packagingMethod     = getOption(file, options, "packaging_method");
			binDir              = getOption(file, options, "bin_dir");
			supportBinariesDir  = getOption(file, options, "support_binaries_dir");
			helperScriptsDir    = getOption(file, options, "helper_scripts_dir");
			resourcesDir        = getOption(file, options, "resources_dir");
			docDir              = getOption(file, options, "doc_dir");
			rubyLibDir          = getOption(file, options, "ruby_libdir");
			nodeLibDir          = getOption(file, options, "node_libdir");
			buildSystemDir      = getOptionalSection(file, options, "node_libdir");
		} else {
			const string &root = _installSpec;
			originallyPackaged  = true;
			packagingMethod     = "unknown";
			binDir              = root + "/bin";
			supportBinariesDir  = root + "/buildout/support-binaries";
			helperScriptsDir    = root + "/src/helper-scripts";
			resourcesDir        = root + "/resources";
			docDir              = root + "/doc";
			rubyLibDir          = root + "/src/ruby_supportlib";
			nodeLibDir          = root + "/src/nodejs_supportlib";
			buildSystemDir      = root;
		}
	}

	bool isOriginallyPackaged() const {
		return originallyPackaged;
	}

	const string &getInstallSpec() const {
		return installSpec;
	}

	const string &getPackagingMethod() const {
		return packagingMethod;
	}

	const string &getBinDir() const {
		return binDir;
	}

	const string &getSupportBinariesDir() const {
		return supportBinariesDir;
	}

	string getUserSupportBinariesDir() const {
		string result(getHomeDir());
		result.append("/");
		result.append(USER_NAMESPACE_DIRNAME);
		result.append("/support-binaries/");
		result.append(PASSENGER_VERSION);
		return result;
	}

	const string &getHelperScriptsDir() const {
		return helperScriptsDir;
	}

	const string &getResourcesDir() const {
		return resourcesDir;
	}

	const string &getDocDir() const {
		return docDir;
	}

	const string &getRubyLibDir() const {
		return rubyLibDir;
	}

	const string &getNodeLibDir() const {
		return nodeLibDir;
	}

	// Can be empty.
	const string &getBuildSystemDir() const {
		return buildSystemDir;
	}

	string findSupportBinary(const string &name) const {
		string path = getSupportBinariesDir() + "/" + name;
		bool found;
		try {
			found = fileExists(path);
		} catch (const SystemException &e) {
			found = false;
		}
		if (found) {
			return path;
		}

		path = getUserSupportBinariesDir() + "/" + name;
		if (fileExists(path)) {
			return path;
		}

		throw RuntimeException("Support binary " + name + " not found (tried: "
			+ getSupportBinariesDir() + "/" + name + " and " + path + ")");
	}
};

typedef boost::shared_ptr<ResourceLocator> ResourceLocatorPtr;


}

#endif /* _PASSENGER_RESOURCE_LOCATOR_H_ */
