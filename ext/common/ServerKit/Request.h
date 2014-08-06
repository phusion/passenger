/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_SERVER_KIT_REQUEST_H_
#define _PASSENGER_SERVER_KIT_REQUEST_H_

#include <ServerKit/HeaderTable.h>
#include <MemoryKit/palloc.h>
#include <DataStructures/LString.h>

namespace Passenger {
namespace ServerKit {


class Request {
public:
	psg_pool_t *pool;
	LString path;
	HeaderTable headers;
	const char *parseError;
	/** Whether the headers have been fully parsed. */
	bool headersComplete: 1;
	/** Whether keep-alive should be used for this connection. **/
	bool keepAlive: 1;

	void reinitialize() {
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		psg_lstr_init(&path);
		parseError = NULL;
		headersComplete = false;
		keepAlive = false;
	}

	void deinitialize() {
		psg_destroy_pool(pool);
		pool = NULL;
		psg_lstr_deinit(&path);

		HeaderTable::Iterator it(headers);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		headers.clear();
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_REQUEST_H_ */
