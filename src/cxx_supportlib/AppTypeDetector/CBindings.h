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
#ifndef _PASSENGER_APP_TYPE_DETECTOR_CBINDINGS_H_
#define _PASSENGER_APP_TYPE_DETECTOR_CBINDINGS_H_

#include <stddef.h>
#include "../WrapperRegistry/CBindings.h"
#include "../Exceptions.h"

#ifdef __cplusplus
	extern "C" {
#endif


typedef void PsgAppTypeDetectorResult;

size_t psg_app_type_detector_result_get_object_size();
PsgAppTypeDetectorResult *psg_app_type_detector_result_init(void *memory);
void psg_app_type_detector_result_deinit(PsgAppTypeDetectorResult *result);

int psg_app_type_detector_result_is_null(const PsgAppTypeDetectorResult *result);
const PsgWrapperRegistryEntry *psg_app_type_detector_result_get_wrapper_registry_entry(
	const PsgAppTypeDetectorResult *result);
void psg_app_type_detector_result_set_wrapper_registry_entry(PsgAppTypeDetectorResult *result,
	const PsgWrapperRegistryEntry *entry);


typedef void PsgAppTypeDetector;

PsgAppTypeDetector *psg_app_type_detector_new(const PsgWrapperRegistry *registry,
	unsigned int throttleRate);
void psg_app_type_detector_free(PsgAppTypeDetector *detector);
void psg_app_type_detector_set_throttle_rate(PsgAppTypeDetector *detector,
	unsigned int throttleRate);
void psg_app_type_detector_check_document_root(
	PsgAppTypeDetector *detector,
	PsgAppTypeDetectorResult *result,
	const char *documentRoot, unsigned int len, int resolveFirstSymlink,
	PP_Error *error);
void psg_app_type_detector_check_app_root(
	PsgAppTypeDetector *detector,
	PsgAppTypeDetectorResult *result,
	const char *appRoot, unsigned int len, PP_Error *error);


#ifdef __cplusplus
	} // extern "C"
#endif

#endif /* _PASSENGER_APP_TYPE_DETECTOR_CBINDINGS_H_ */
