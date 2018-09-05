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

#include <cstddef>
#include <new>
#include <exception>

#include <StaticString.h>
#include <AppTypeDetector/CBindings.h>
#include <AppTypeDetector/Detector.h>

using namespace Passenger;
using namespace Passenger::WrapperRegistry;
using namespace Passenger::AppTypeDetector;


size_t
psg_app_type_detector_result_get_object_size() {
	return sizeof(Detector::Result);
}

PsgAppTypeDetectorResult *
psg_app_type_detector_result_init(void *memory) {
	Detector::Result *cxxResult = new (memory) Detector::Result();
	return static_cast<PsgAppTypeDetectorResult *>(cxxResult);
}

void
psg_app_type_detector_result_deinit(PsgAppTypeDetectorResult *result) {
	Detector::Result *cxxResult = static_cast<Detector::Result *>(result);
	cxxResult->~Result();
}

int
psg_app_type_detector_result_is_null(const PsgAppTypeDetectorResult *result) {
	const Detector::Result *cxxResult = static_cast<const Detector::Result *>(result);
	return (int) cxxResult->isNull();
}

const PsgWrapperRegistryEntry *
psg_app_type_detector_result_get_wrapper_registry_entry(const PsgAppTypeDetectorResult *result) {
	const Detector::Result *cxxResult = static_cast<const Detector::Result *>(result);
	return static_cast<const PsgWrapperRegistryEntry *>(cxxResult->wrapperRegistryEntry);
}

void
psg_app_type_detector_result_set_wrapper_registry_entry(PsgAppTypeDetectorResult *result,
	const PsgWrapperRegistryEntry *entry)
{
	Detector::Result *cxxResult = static_cast<Detector::Result *>(result);
	cxxResult->wrapperRegistryEntry = static_cast<const WrapperRegistry::Entry *>(entry);
}


PsgAppTypeDetector *
psg_app_type_detector_new(const PsgWrapperRegistry *registry,
	unsigned int throttleRate)
{
	const Registry *cxxRegistry = static_cast<const Registry *>(registry);
	try {
		Detector *detector = new Detector(*cxxRegistry, NULL, NULL, throttleRate);
		return static_cast<PsgAppTypeDetector *>(detector);
	} catch (const std::bad_alloc &) {
		return NULL;
	}
}

void
psg_app_type_detector_free(PsgAppTypeDetector *detector) {
	Detector *cxxDetector = static_cast<Detector *>(detector);
	delete cxxDetector;
}

void
psg_app_type_detector_set_throttle_rate(PsgAppTypeDetector *detector,
	unsigned int throttleRate)
{
	Detector *cxxDetector = static_cast<Detector *>(detector);
	cxxDetector->setThrottleRate(throttleRate);
}

void
psg_app_type_detector_check_document_root(
	PsgAppTypeDetector *detector, PsgAppTypeDetectorResult *result,
	const char *documentRoot, unsigned int len, int resolveFirstSymlink,
	PP_Error *error)
{
	Detector *cxxDetector = static_cast<Detector *>(detector);
	Detector::Result *cxxResult = static_cast<Detector::Result *>(result);
	try {
		*cxxResult = cxxDetector->checkDocumentRoot(
			StaticString(documentRoot, len), resolveFirstSymlink);
	} catch (const std::exception &e) {
		pp_error_set(e, error);
		*cxxResult = Detector::Result();
	}
}

void
psg_app_type_detector_check_app_root(
	PsgAppTypeDetector *detector, PsgAppTypeDetectorResult *result,
	const char *appRoot, unsigned int len, PP_Error *error)
{
	Detector *cxxDetector = static_cast<Detector *>(detector);
	Detector::Result *cxxResult = static_cast<Detector::Result *>(result);
	try {
		*cxxResult = cxxDetector->checkAppRoot(
			StaticString(appRoot, len));
	} catch (const std::exception &e) {
		pp_error_set(e, error);
		*cxxResult = Detector::Result();
	}
}
