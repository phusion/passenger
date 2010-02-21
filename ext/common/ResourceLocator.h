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
#include "Utils.h"

namespace Passenger {

class ResourceLocator {
private:
	string root;
	bool nativelyPackaged;
	
public:
	ResourceLocator(const string &passengerRoot) {
		root = passengerRoot;
		nativelyPackaged = !fileExists(root + "/Rakefile") ||
			!fileExists(root + "/DEVELOPERS.TXT");
	}
	
	string getSourceRoot() const {
		if (nativelyPackaged) {
			return "/usr/lib/phusion-passenger/source";
		} else {
			return root;
		}
	}
	
	string getAgentsDir() const {
		if (nativelyPackaged) {
			return "/usr/lib/phusion-passenger/agents";
		} else {
			return root + "/agents";
		}
	}
	
	string getHelperScriptsDir() const {
		if (nativelyPackaged) {
			return "/usr/share/phusion-passenger/helper-scripts";
		} else {
			return root + "/helper-scripts";
		}
	}
	
	string getSpawnServerFilename() const {
		return getHelperScriptsDir() + "/passenger-spawn-server";
	}
};

}

#endif /* _PASSENGER_RESOURCE_LOCATOR_H_ */
