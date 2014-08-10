/*
 * Base64 encoding and decoding routines
 *
 * Copyright (C) 2004-2008 René Nyffenegger
 * Modified by Phusion for inclusion in Phusion Passenger.
 *
 * This source code is provided 'as-is', without any express or implied
 * warranty. In no event will the author be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this source code must not be misrepresented; you must not
 *    claim that you wrote the original source code. If you use this source code
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original source code.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * René Nyffenegger rene.nyffenegger@adp-gmbh.ch
 *
 */

#ifndef _PASSENGER_BASE64_H_
#define _PASSENGER_BASE64_H_

#include <iostream>
#include <string>
#include "../StaticString.h"

namespace Passenger {

using namespace std;

class Base64 {
public:
	static string encode(const StaticString &data) {
		return encode((const unsigned char *) data.data(), data.size());
	}

	/** Encode using a modified Base64 format, suitable for inclusion in URLs without
	 * needing escaping.
	 */
	static string encodeForUrl(const StaticString &data) {
		string result = encode(data);
		string::size_type i;
		int paddingSize = 0;

		for (i = 0; i < result.size(); i++) {
			char c = result[i];
			if (c == '+') {
				result[i] = '-';
			} else if (c == '/') {
				result[i] = '_';
			} else if (c == '=') {
				paddingSize++;
			}
		}

		if (paddingSize > 0) {
			result.resize(result.size() - paddingSize);
		}

		return result;
	}

	static string decode(const StaticString &base64_data) {
		return decode((const unsigned char *) base64_data.data(), base64_data.size());
	}

	static string encode(const unsigned char *data, unsigned int len);

	static string decode(const unsigned char *base64_data, unsigned int len);
};

} // namespace Passenger

#endif /* _PASSENGER_BASE64_H_ */
