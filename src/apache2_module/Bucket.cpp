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

#include <boost/make_shared.hpp>
#include "Bucket.h"

namespace Passenger {
namespace Apache2Module {


static void bucket_destroy(void *data);
static apr_status_t bucket_read(apr_bucket *a, const char **str, apr_size_t *len, apr_read_type_e block);

static const apr_bucket_type_t apr_bucket_type_passenger_pipe = {
	"PASSENGER_PIPE",
	5,
	apr_bucket_type_t::APR_BUCKET_DATA,
	bucket_destroy,
	bucket_read,
	apr_bucket_setaside_notimpl,
	apr_bucket_split_notimpl,
	apr_bucket_copy_notimpl
};

struct BucketData {
	FileDescriptor fd;
	PassengerBucketStatePtr state;
	bool bufferResponse;
};

static void
bucket_destroy(void *data) {
	BucketData *bucket_data = (BucketData *) data;
	if (data != NULL) {
		delete bucket_data;
	}
}

static apr_status_t
bucket_read(apr_bucket *bucket, const char **str, apr_size_t *len, apr_read_type_e block) {
	char *buf;
	ssize_t ret;
	BucketData *data;

	data = (BucketData *) bucket->data;
	*str = NULL;
	*len = 0;

	if (!data->bufferResponse && block == APR_NONBLOCK_READ) {
		/*
		 * The bucket brigade that Hooks::handleRequest() passes using
		 * ap_pass_brigade() is always passed through ap_content_length_filter,
		 * which is a filter which attempts to read all data from the
		 * bucket brigade and computes the Content-Length header from
		 * that. We don't want this to happen; because suppose that the
		 * Rails application sends back 1 GB of data, then
		 * ap_content_length_filter will buffer this entire 1 GB of data
		 * in memory before passing it to the HTTP client.
		 *
		 * ap_content_length_filter aborts and passes the bucket brigade
		 * down the filter chain when it encounters an APR_EAGAIN, except
		 * for the first read. So by returning APR_EAGAIN on every
		 * non-blocking read request, we can prevent ap_content_length_filter
		 * from buffering all data.
		 */
		return APR_EAGAIN;
	}

	buf = (char *) apr_bucket_alloc(APR_BUCKET_BUFF_SIZE, bucket->list);
	if (buf == NULL) {
		return APR_ENOMEM;
	}

	do {
		ret = read(data->state->connection, buf, APR_BUCKET_BUFF_SIZE);
	} while (ret == -1 && errno == EINTR);

	if (ret > 0) {
		apr_bucket_heap *h;

		data->state->bytesRead += ret;

		*str = buf;
		*len = ret;
		bucket->data = NULL;

		/* Change the current bucket (which is a Passenger Bucket) into a heap bucket
		 * that contains the data that we just read. This newly created heap bucket
		 * will be the first in the bucket list.
		 */
		bucket = apr_bucket_heap_make(bucket, buf, *len, apr_bucket_free);
		h = (apr_bucket_heap *) bucket->data;
		h->alloc_len = APR_BUCKET_BUFF_SIZE; /* note the real buffer size */

		/* And after this newly created bucket we insert a new Passenger Bucket
		 * which can read the next chunk from the stream.
		 */
		APR_BUCKET_INSERT_AFTER(bucket, passenger_bucket_create(
			data->state, bucket->list, data->bufferResponse));

		/* The newly created Passenger Bucket has a reference to the session
		 * object, so we can delete data here.
		 */
		delete data;

		return APR_SUCCESS;

	} else if (ret == 0) {
		data->state->completed = true;
		delete data;
		bucket->data = NULL;

		apr_bucket_free(buf);

		bucket = apr_bucket_immortal_make(bucket, "", 0);
		*str = (const char *) bucket->data;
		*len = 0;
		return APR_SUCCESS;

	} else /* ret == -1 */ {
		int e = errno;
		data->state->completed = true;
		data->state->errorCode = e;
		delete data;
		bucket->data = NULL;
		apr_bucket_free(buf);
		return APR_FROM_OS_ERROR(e);
	}
}

static apr_bucket *
passenger_bucket_make(apr_bucket *bucket, const PassengerBucketStatePtr &state, bool bufferResponse) {
	BucketData *data = new BucketData();
	data->state    = state;
	data->bufferResponse = bufferResponse;

	bucket->type   = &apr_bucket_type_passenger_pipe;
	bucket->length = (apr_size_t)(-1);
	bucket->start  = -1;
	bucket->data   = data;
	return bucket;
}

apr_bucket *
passenger_bucket_create(const PassengerBucketStatePtr &state, apr_bucket_alloc_t *list, bool bufferResponse) {
	apr_bucket *bucket;

	bucket = (apr_bucket *) apr_bucket_alloc(sizeof(*bucket), list);
	APR_BUCKET_INIT(bucket);
	bucket->free = apr_bucket_free;
	bucket->list = list;
	return passenger_bucket_make(bucket, state, bufferResponse);
}


} // namespace Apache2Module
} // namespace Passenger
