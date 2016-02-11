/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_CONFIGURATION_H_
#define _PASSENGER_CONFIGURATION_H_

#include <apr_pools.h>
#include <httpd.h>
#include <http_config.h>

#ifdef __cplusplus
	extern "C" {
#endif


/** Configuration hook for per-directory configuration structure creation. */
void *passenger_config_create_dir(apr_pool_t *p, char *dirspec);

/** Configuration hook for per-directory configuration structure merging. */
void *passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv);

void passenger_postprocess_config(server_rec *s);

/** Apache module commands array. */
extern const command_rec passenger_commands[];


#ifdef __cplusplus
	}
#endif

#endif /* _PASSENGER_CONFIGURATION_H_ */
