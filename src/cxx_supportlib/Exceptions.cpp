/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2017 Phusion Holding B.V.
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

#include <Exceptions.h>
#include <stdlib.h>
#include <string.h>


void
pp_error_init(PP_Error *error) {
	error->message = NULL;
	error->errnoCode = PP_NO_ERRNO;
	error->messageIsStatic = 0;
}

void
pp_error_destroy(PP_Error *error) {
	if (!error->messageIsStatic) {
		free(static_cast<void *>(const_cast<char *>(error->message)));
		error->message = NULL;
		error->messageIsStatic = 0;
	}
}

void
pp_error_set(const std::exception &ex, PP_Error *error) {
	const Passenger::SystemException *sys_e;

	if (error == NULL) {
		return;
	}

	if (error->message != NULL && !error->messageIsStatic) {
		free(static_cast<void *>(const_cast<char *>(error->message)));
	}

	error->message = strdup(ex.what());
	error->messageIsStatic = error->message == NULL;
	if (error->message == NULL) {
		error->message = "Unknown error message (unable to allocate memory for the message)";
	}

	sys_e = dynamic_cast<const Passenger::SystemException *>(&ex);
	if (sys_e != NULL) {
		error->errnoCode = sys_e->code();
	} else {
		error->errnoCode = PP_NO_ERRNO;
	}
}
