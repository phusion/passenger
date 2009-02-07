/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "Bucket.h"

static apr_status_t bucket_read(apr_bucket *a, const char **str, apr_size_t *len, apr_read_type_e block);

static const apr_bucket_type_t apr_bucket_type_passenger_pipe = {
	"PASSENGER_PIPE",
	5,
	apr_bucket_type_t::APR_BUCKET_DATA, 
	apr_bucket_destroy_noop,
	bucket_read,
	apr_bucket_setaside_notimpl,
	apr_bucket_split_notimpl,
	apr_bucket_copy_notimpl
};

static apr_status_t
bucket_read(apr_bucket *bucket, const char **str, apr_size_t *len, apr_read_type_e block) {
	apr_file_t *pipe;
	char *buf;
	apr_status_t ret;

	pipe = (apr_file_t *) bucket->data;

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
		apr_bucket_free(buf);
		return ret;
	}
	/*
	 * If there's more to read we have to keep the rest of the pipe
	 * for later.
	 */
	if (*len > 0) {
		apr_bucket_heap *h;
		/* Change the current bucket to refer to what we read */
		bucket = apr_bucket_heap_make(bucket, buf, *len, apr_bucket_free);
		h = (apr_bucket_heap *) bucket->data;
		h->alloc_len = APR_BUCKET_BUFF_SIZE; /* note the real buffer size */
		*str = buf;
		APR_BUCKET_INSERT_AFTER(bucket, passenger_bucket_create(pipe, bucket->list));
	} else {
		apr_bucket_free(buf);
		bucket = apr_bucket_immortal_make(bucket, "", 0);
		*str = (const char *) bucket->data;
		// if (rv != APR_EOF) {
		//     ... we might want to set an error flag here ...
		// }
	}
	return APR_SUCCESS;
}

apr_bucket *
passenger_bucket_make(apr_bucket *bucket, apr_file_t *pipe) {
	bucket->type   = &apr_bucket_type_passenger_pipe;
	bucket->length = (apr_size_t)(-1);
	bucket->start  = -1;
	bucket->data   = pipe;
	return bucket;
}

apr_bucket *
passenger_bucket_create(apr_file_t *pipe, apr_bucket_alloc_t *list) {
	apr_bucket *bucket;
	
	bucket = (apr_bucket *) apr_bucket_alloc(sizeof(*bucket), list);
	APR_BUCKET_INIT(bucket);
	bucket->free = apr_bucket_free;
	bucket->list = list;
	return passenger_bucket_make(bucket, pipe);
}

