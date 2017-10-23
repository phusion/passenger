/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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

// This file MUST be named mod_passenger.c so so that
// <IfModule mod_passenger.c> works.

#include <httpd.h>
#include <http_config.h>

#ifdef VISIBILITY_ATTRIBUTE_SUPPORTED
	#define PUBLIC_SYMBOL __attribute__ ((visibility("default")))
#else
	#define PUBLIC_SYMBOL
#endif

extern const command_rec passenger_commands[];

void *passenger_create_dir_config(apr_pool_t *p, char *dirspec);
void *passenger_merge_dir_config(apr_pool_t *p, void *basev, void *addv);
void passenger_register_hooks(apr_pool_t *p);

PUBLIC_SYMBOL module AP_MODULE_DECLARE_DATA passenger_module = {
	STANDARD20_MODULE_STUFF,
	passenger_create_dir_config,  // create per-dir config structs
	passenger_merge_dir_config,   // merge per-dir config structs
	NULL,                         // create per-server config structs
	NULL,                         // merge per-server config structs
	passenger_commands,           // table of config file commands
	passenger_register_hooks      // register hooks
};
