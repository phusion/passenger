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

using namespace PhusionPassenger;

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

