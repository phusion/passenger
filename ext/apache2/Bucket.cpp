/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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
#include "Bucket.h"

using namespace Passenger;

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
	Application::SessionPtr session;
	apr_file_t *pipe;
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
	apr_file_t *pipe;
	char *buf;
	apr_status_t ret;

	BucketData *data = (BucketData *) bucket->data;
	pipe = data->pipe;

	*str = NULL;
	*len = APR_BUCKET_BUFF_SIZE;
	
	if (block == APR_NONBLOCK_READ) {
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
	
	buf = (char *) apr_bucket_alloc(*len, bucket->list); // TODO: check for failure?

	do {
		ret = apr_file_read(pipe, buf, len);
	} while (APR_STATUS_IS_EAGAIN(ret));

	if (ret != APR_SUCCESS && ret != APR_EOF) {
		// ... we might want to set an error flag here ...
		delete data;
		bucket->data = NULL;
		apr_bucket_free(buf);
		return ret;
	}
	/*
	 * If there's more to read we have to keep the rest of the pipe
	 * for later.
	 */
	if (*len > 0) {
		apr_bucket_heap *h;
		
		*str = buf;
		bucket->data = NULL;
		
		/* Change the current bucket to refer to what we read */
		bucket = apr_bucket_heap_make(bucket, buf, *len, apr_bucket_free);
		h = (apr_bucket_heap *) bucket->data;
		h->alloc_len = APR_BUCKET_BUFF_SIZE; /* note the real buffer size */
		APR_BUCKET_INSERT_AFTER(bucket, passenger_bucket_create(
			data->session, pipe, bucket->list));
		delete data;
	} else {
		delete data;
		bucket->data = NULL;
		
		apr_bucket_free(buf);
		bucket = apr_bucket_immortal_make(bucket, "", 0);
		*str = (const char *) bucket->data;
		// if (rv != APR_EOF) {
		//     ... we might want to set an error flag here ...
		// }
	}
	return APR_SUCCESS;
}

static apr_bucket *
passenger_bucket_make(apr_bucket *bucket, Application::SessionPtr session, apr_file_t *pipe) {
	BucketData *data = new BucketData();
	data->session  = session;
	data->pipe     = pipe;
	
	bucket->type   = &apr_bucket_type_passenger_pipe;
	bucket->length = (apr_size_t)(-1);
	bucket->start  = -1;
	bucket->data   = data;
	return bucket;
}

apr_bucket *
passenger_bucket_create(Application::SessionPtr session, apr_file_t *pipe, apr_bucket_alloc_t *list) {
	apr_bucket *bucket;
	
	bucket = (apr_bucket *) apr_bucket_alloc(sizeof(*bucket), list);
	APR_BUCKET_INIT(bucket);
	bucket->free = apr_bucket_free;
	bucket->list = list;
	return passenger_bucket_make(bucket, session, pipe);
}

