/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2015 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL2_BASIC_GROUP_INFO_H_
#define _PASSENGER_APPLICATION_POOL2_BASIC_GROUP_INFO_H_

#include <string>
#include <cstddef>
#include <ApplicationPool2/Context.h>

namespace Passenger {
namespace ApplicationPool2 {


class Group;

/**
 * Contains basic Group information. This information is set during the
 * initialization of a Group and never changed afterwards. This struct
 * encapsulates that information. It is contained inside `Group` as a const
 * object. Because of the immutable nature of the information, multithreaded
 * access is safe.
 *
 * Since Process and Session sometimes need to look up this basic group
 * information, this struct also serves to ensure that Process and Session do
 * not have a direct dependency on Group, but on GroupImmutableInfo instead.
 */
class BasicGroupInfo {
public:
	static const unsigned int SECRET_SIZE = 16;

	Context *context;

	/**
	 * A back pointer to the Group that this BasicGroupInfo is contained in.
	 * May be NULL in unit tests.
	 */
	Group *group;

	/**
	 * This name uniquely identifies this Group within its Pool. It can
	 * also be used as the display name.
	 */
	std::string name;

	/**
	 * A secret token that may be known among all processes in this Group. Used
	 * for securing intra-group process communication.
	 *
	 * On x86_64 the secret is as big as a StaticString,
	 * so might as well directly embed the data instead.
	 */
	char secret[SECRET_SIZE];

	BasicGroupInfo()
		: context(NULL),
		  group(NULL)
	{
		secret[0] = '\0';
	}

	StaticString getSecret() const {
		return StaticString(secret, SECRET_SIZE);
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_BASIC_GROUP_INFO_H_ */
