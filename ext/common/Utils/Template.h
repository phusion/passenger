#include <string>
#include <StaticString.h>
#include <Utils/IOUtils.h>
#include <Utils/StringMap.h>

namespace Passenger {

using namespace std;


inline string
applyTemplate(const StaticString &templateContent, const StringMap<StaticString> &substitutions) {
	string result = templateContent;
	string::size_type searchStart = 0;
	string::size_type beginPos, endPos;

	while (searchStart < result.size()) {
		beginPos = result.find("{{", searchStart);
		if (beginPos == string::npos) {
			searchStart = result.size();
		} else {
			endPos = result.find("}}", beginPos);
			if (endPos == string::npos) {
				searchStart = result.size();
			} else {
				string name = result.substr(beginPos + 2, endPos - beginPos - 2);
				string value = substitutions.get(name);
				result.replace(beginPos, endPos - beginPos + 2, value);
				searchStart = beginPos + value.size();
			}
		}
	}

	return result;
}


} // namespace Passenger
