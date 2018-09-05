/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
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

#include <new>
#include <exception>

#include <WrapperRegistry/CBindings.h>
#include <WrapperRegistry/Entry.h>
#include <WrapperRegistry/Registry.h>

using namespace Passenger;
using namespace Passenger::WrapperRegistry;


int
psg_wrapper_registry_entry_is_null(const PsgWrapperRegistryEntry *entry) {
	const Entry *cxxEntry = static_cast<const Entry *>(entry);
	return (int) cxxEntry->isNull();
}

const char *
psg_wrapper_registry_entry_get_language(const PsgWrapperRegistryEntry *entry, size_t *len) {
	const Entry *cxxEntry = static_cast<const Entry *>(entry);
	if (len != NULL) {
		*len = cxxEntry->language.size();
	}
	return cxxEntry->language.data();
}


PsgWrapperRegistry *
psg_wrapper_registry_new() {
	try {
		return static_cast<PsgWrapperRegistry *>(new Registry());
	} catch (const std::bad_alloc &) {
		return NULL;
	}
}

void
psg_wrapper_registry_free(PsgWrapperRegistry *registry) {
	Registry *cxxRegistry = static_cast<Registry *>(registry);
	delete cxxRegistry;
}

void
psg_wrapper_registry_finalize(PsgWrapperRegistry *registry) {
	Registry *cxxRegistry = static_cast<Registry *>(registry);
	cxxRegistry->finalize();
}

const PsgWrapperRegistryEntry *
psg_wrapper_registry_lookup(PsgWrapperRegistry *registry,
	const char *name, size_t size)
{
	const Registry *cxxRegistry = static_cast<const Registry *>(registry);
	if (size == (size_t) -1) {
		size = strlen(name);
	}
	return static_cast<const PsgWrapperRegistryEntry *>(
		&cxxRegistry->lookup(StaticString(name, size)));
}
