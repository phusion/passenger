/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2015-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_CLASS_UTILS_H_
#define _PASSENGER_CLASS_UTILS_H_

#define P_DEFINE_GETTER(type, name) \
	type get ## name() const { \
		return m ## name; \
	}

#define P_DEFINE_GETTER_CONST_REF(type, name) \
	const type &get ## name() const { \
		return m ## name; \
	}

#define P_DEFINE_GETTER_REF(type, name) \
	type &get ## name() { \
		return m ## name; \
	}

#define P_DEFINE_SETTER(type, name) \
	void set ## name(const type &value) { \
		m ## name = value; \
	}

#define P_RO_PROPERTY(visibility, type, name) \
	public: \
		P_DEFINE_GETTER(type, name) \
	visibility: \
		type m ## name

#define P_PROPERTY_CONST_REF(visibility, type, name) \
	public: \
		P_DEFINE_GETTER_CONST_REF(type, name) \
		P_DEFINE_SETTER(type, name) \
	visibility: \
		type m ## name

#define P_RO_PROPERTY_REF(visibility, type, name) \
	public: \
		P_DEFINE_GETTER_REF(type, name) \
	visibility: \
		type m ## name

#define P_RO_PROPERTY_CONST_REF(visibility, type, name) \
	public: \
		P_DEFINE_GETTER_CONST_REF(type, name) \
	visibility: \
		type m ## name

#endif /* _PASSENGER_CLASS_UTILS_H_ */
