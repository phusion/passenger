/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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

#include <JsonTools/CBindings.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <jsoncpp/json.h>
#include <JsonTools/Autocast.h>

using namespace std;
using namespace Passenger;

extern "C" {


PsgJsonValue *
psg_json_value_new_null() {
	return new Json::Value();
}

PsgJsonValue *
psg_json_value_new_with_type(PsgJsonValueType type) {
	Json::ValueType realType;
	switch (type) {
	case PSG_JSON_VALUE_TYPE_NULL:
		realType = Json::nullValue;
		break;
	case PSG_JSON_VALUE_TYPE_INT:
		realType = Json::intValue;
		break;
	case PSG_JSON_VALUE_TYPE_UINT:
		realType = Json::uintValue;
		break;
	case PSG_JSON_VALUE_TYPE_REAL:
		realType = Json::realValue;
		break;
	case PSG_JSON_VALUE_TYPE_STRING:
		realType = Json::stringValue;
		break;
	case PSG_JSON_VALUE_TYPE_BOOLEAN:
		realType = Json::booleanValue;
		break;
	case PSG_JSON_VALUE_TYPE_ARRAY:
		realType = Json::arrayValue;
		break;
	case PSG_JSON_VALUE_TYPE_OBJECT:
		realType = Json::objectValue;
		break;
	default:
		fprintf(stderr, "BUG: Unrecognized PsgJsonValueType %d\n", (int) type);
		abort();
		break;
	}
	return new Json::Value(realType);
}

PsgJsonValue *
psg_json_value_new_str(const char *val, size_t size) {
	return new Json::Value(val, val + size);
}

PsgJsonValue *
psg_json_value_new_int(int val) {
	return new Json::Value(val);
}

PsgJsonValue *
psg_json_value_new_uint(unsigned int val) {
	return new Json::Value(val);
}

PsgJsonValue *
psg_json_value_new_real(double val) {
	return new Json::Value(val);
}

PsgJsonValue *
psg_json_value_new_bool(int val) {
	return new Json::Value((bool) val);
}

void
psg_json_value_free(PsgJsonValue *val) {
	delete (Json::Value *) val;
}


PsgJsonValue *
psg_json_value_get_or_create_null(PsgJsonValue *doc, const char *name, size_t size) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	if (size == (size_t) -1) {
		size = strlen(name);
	}
	return &cxxdoc[string(name, size)];
}

PsgJsonValue *
psg_json_value_get(PsgJsonValue *doc, const char *name, size_t size) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	if (size == (size_t) -1) {
		size = strlen(name);
	}
	if (cxxdoc.isMember(name, name + size)) {
		return &cxxdoc[string(name, size)];
	} else {
		return NULL;
	}
}

PsgJsonValue *
psg_json_value_get_at_index(PsgJsonValue *doc, unsigned int index) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	if (index >= cxxdoc.size()) {
		return NULL;
	} else {
		return &cxxdoc[index];
	}
}

PsgJsonValueType
psg_json_value_type(const PsgJsonValue *doc) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	switch (cxxdoc.type()) {
	case Json::nullValue:
		return PSG_JSON_VALUE_TYPE_NULL;
	case Json::intValue:
		return PSG_JSON_VALUE_TYPE_INT;
	case Json::uintValue:
		return PSG_JSON_VALUE_TYPE_UINT;
	case Json::realValue:
		return PSG_JSON_VALUE_TYPE_REAL;
	case Json::stringValue:
		return PSG_JSON_VALUE_TYPE_STRING;
	case Json::booleanValue:
		return PSG_JSON_VALUE_TYPE_BOOLEAN;
	case Json::arrayValue:
		return PSG_JSON_VALUE_TYPE_ARRAY;
	case Json::objectValue:
		return PSG_JSON_VALUE_TYPE_OBJECT;
	default:
		fprintf(stderr, "BUG: Unrecognized Json::ValueType %d\n", (int) cxxdoc.type());
		abort();
		return PSG_JSON_VALUE_TYPE_NULL;
	}
}

int
psg_json_value_eq(const PsgJsonValue *doc, const PsgJsonValue *doc2) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	const Json::Value &cxxdoc2 = *static_cast<const Json::Value *>(doc2);
	return cxxdoc == cxxdoc2;
}

int
psg_json_value_is_member(const PsgJsonValue *doc, const char *name, size_t size) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	if (size == (size_t) -1) {
		size = strlen(name);
	}
	return cxxdoc.isMember(name, name + size);
}

unsigned int
psg_json_value_size(const PsgJsonValue *doc) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	return cxxdoc.size();
}


PsgJsonValue *
psg_json_value_set_value(PsgJsonValue *doc, const char *name, size_t name_size, const PsgJsonValue *val) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	if (name_size == (size_t) -1) {
		name_size = strlen(name);
	}
	Json::Value &newVal = cxxdoc[string(name, name_size)];
	newVal = *static_cast<const Json::Value *>(val);
	return &newVal;
}

PsgJsonValue *
psg_json_value_set_str(PsgJsonValue *doc, const char *name, const char *val, size_t size) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value &newVal = cxxdoc[name];
	if (size == (size_t) -1) {
		size = strlen(val);
	}
	newVal = Json::Value(val, val + size);
	return &newVal;
}

PsgJsonValue *
psg_json_value_set_int(PsgJsonValue *doc, const char *name, int val) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value &newVal = cxxdoc[name];
	newVal = (Json::Int) val;
	return &newVal;
}

PsgJsonValue *
psg_json_value_set_uint(PsgJsonValue *doc, const char *name, unsigned int val) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value &newVal = cxxdoc[name];
	newVal = (Json::UInt) val;
	return &newVal;
}

PsgJsonValue *
psg_json_value_set_real(PsgJsonValue *doc, const char *name, double val) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value &newVal = cxxdoc[name];
	newVal = val;
	return &newVal;
}

PsgJsonValue *
psg_json_value_set_bool(PsgJsonValue *doc, const char *name, int val) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value &newVal = cxxdoc[name];
	newVal = (bool) val;
	return &newVal;
}


PsgJsonValue *
psg_json_value_append_val(PsgJsonValue *doc, const PsgJsonValue *val) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	return &cxxdoc.append(*static_cast<const Json::Value *>(val));
}

void
psg_json_value_swap(PsgJsonValue *doc, PsgJsonValue *doc2) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value &cxxdoc2 = *static_cast<Json::Value *>(doc2);
	cxxdoc.swap(cxxdoc2);
}


int
psg_json_value_is_null(const PsgJsonValue *doc) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	return cxxdoc.isNull();
}

int
psg_json_value_empty(const PsgJsonValue *doc) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	return cxxdoc.empty();
}

const char *
psg_json_value_as_cstr(const PsgJsonValue *doc) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	return cxxdoc.asCString();
}

const char *
psg_json_value_get_str(const PsgJsonValue *doc, size_t *size) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	const char *data, *end;
	if (cxxdoc.getString(&data, &end)) {
		if (size != NULL) {
			*size = end - data;
		}
		return data;
	} else {
		return NULL;
	}
}


void
psg_json_value_begin(PsgJsonValue *doc, PsgJsonValueIterator *it) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	cxxit = cxxdoc.begin();
}

void
psg_json_value_end(PsgJsonValue *doc, PsgJsonValueIterator *it) {
	Json::Value &cxxdoc = *static_cast<Json::Value *>(doc);
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	cxxit = cxxdoc.end();
}


char *
psg_json_value_to_styled_string(const PsgJsonValue *doc) {
	const Json::Value &cxxdoc = *static_cast<const Json::Value *>(doc);
	return strdup(cxxdoc.toStyledString().c_str());
}


PsgJsonValue *
psg_autocast_value_to_json(const char *data, size_t size, char **error) {
	return new Json::Value(autocastValueToJson(StaticString(data, size)));
}


PsgJsonValueIterator *
psg_json_value_iterator_new() {
	return new Json::Value::iterator();
}

void
psg_json_value_iterator_free(PsgJsonValueIterator *it) {
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	delete &cxxit;
}

void
psg_json_value_iterator_advance(PsgJsonValueIterator *it) {
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	cxxit++;
}

int
psg_json_value_iterator_eq(PsgJsonValueIterator *it, PsgJsonValueIterator *other) {
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	Json::Value::iterator &cxxOther = *static_cast<Json::Value::iterator *>(other);
	return cxxit == cxxOther;
}

const char *
psg_json_value_iterator_get_name(PsgJsonValueIterator *it, size_t *size) {
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	const char *result, *end;
	result = cxxit.memberName(&end);
	*size = end - result;
	return result;
}

PsgJsonValue *
psg_json_value_iterator_get_value(PsgJsonValueIterator *it) {
	Json::Value::iterator &cxxit = *static_cast<Json::Value::iterator *>(it);
	return &(*cxxit);
}


} // extern "C"
