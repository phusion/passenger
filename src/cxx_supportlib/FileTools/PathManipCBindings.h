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
#ifndef _PASSENGER_FILE_TOOLS_PATH_MANIP_C_BINDINGS_H_
#define _PASSENGER_FILE_TOOLS_PATH_MANIP_C_BINDINGS_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


char *psg_absolutize_path(const char *path, size_t path_len,
	const char *working_dir, size_t working_dir_len,
	size_t *result_len);
char *psg_resolve_symlink(const char *path, size_t path_len,
	size_t *result_len);
const char *psg_extract_dir_name_static(const char *path,
	size_t path_len, size_t *result_len);


#ifdef __cplusplus
}
#endif

#endif /* _PASSENGER_FILE_TOOLS_PATH_MANIP_C_BINDINGS_H_ */
