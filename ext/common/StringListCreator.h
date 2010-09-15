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
#ifndef _PASSENGER_STRING_LIST_CREATOR_H_
#define _PASSENGER_STRING_LIST_CREATOR_H_

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include "Utils.h"
#include "Utils/Base64.h"

namespace Passenger {

using namespace std;
using namespace boost;

typedef vector<string> StringList;
typedef shared_ptr<StringList> StringListPtr;


class StringListCreator {
public:
	virtual ~StringListCreator() {}
	
	/** May throw arbitrary exceptions. */
	virtual const StringListPtr getItems() const = 0;
};

typedef shared_ptr<StringListCreator> StringListCreatorPtr;

class SimpleStringListCreator: public StringListCreator {
public:
	StringListPtr items;
	
	SimpleStringListCreator() {
		items = ptr(new StringList());
	}
	
	SimpleStringListCreator(const StaticString &data) {
		items = ptr(new StringList());
		string buffer = Base64::decode(data);
		if (!buffer.empty()) {
			string::size_type start = 0, pos;
			const string &const_buffer(buffer);
			while ((pos = const_buffer.find('\0', start)) != string::npos) {
				items->push_back(const_buffer.substr(start, pos - start));
				start = pos + 1;
			}
		}
	}
	
	virtual const StringListPtr getItems() const {
		return items;
	}
};

typedef shared_ptr<SimpleStringListCreator> SimpleStringListCreatorPtr;

} // namespace Passenger

#endif /* _PASSENGER_STRING_LIST_CREATOR_H_ */
