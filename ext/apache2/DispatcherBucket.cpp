#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <apr_time.h>
#define APR_WANT_BYTEFUNC
#include <apr_want.h>

#define DEBUG
#ifdef DEBUG
	#include <cstdarg>
	#include <cstdio>
	#include <apr_strings.h>
#endif

#include "DispatcherBucket.h"

static apr_status_t dispatcher_bucket_read(apr_bucket *b, const char **str, apr_size_t *len, apr_read_type_e block);
static void dispatcher_bucket_destroy(void *data);

static const apr_bucket_type_t bucket_type_dispatcher = {
	"Dispatcher",
	5,
	apr_bucket_type_t::APR_BUCKET_DATA,
	dispatcher_bucket_destroy,
	dispatcher_bucket_read,
	apr_bucket_setaside_notimpl,
	apr_bucket_split_notimpl,
	apr_bucket_copy_notimpl
};


#ifdef DEBUG
static void
debug(const char *format, ...) {
	va_list ap;
	char message[1024];
	
	va_start(ap, format);
	int size = apr_vsnprintf(message, sizeof(message), format, ap);
	FILE *f = fopen("/dev/pts/2", "w");
	if (f != NULL) {
		fwrite(message, 1, size, f);
		fclose(f);
	}
	va_end(ap);
}
#endif


class DispatcherBucket {
private:
	apr_status_t
	errno_to_apr_status(int e) {
		switch (e) {
		case EBADF:
			return APR_EBADF;
		case EAGAIN:
			return APR_EAGAIN;
		case EINTR:
			return APR_EINTR;
		case EINVAL:
			return APR_EINVAL;
		case ENOMEM:
			return APR_ENOMEM;
		default:
			return APR_EBADF;
		}
	}
	
	apr_status_t
	read_block(void *buffer, unsigned int size, apr_interval_time_t &timeout) {
		int tmp;
		unsigned int already_read = 0;
		while (true) {
			if (timeout != 0) {
				struct pollfd fd;
	
				fd.fd = pipe;
				fd.events = POLLIN;
				tmp = poll(&fd, 1, timeout / 1000);
				if (tmp == 0) {
					// Timeout
					return APR_TIMEUP;
				} else if (tmp == -1) {
					// Error
					return errno_to_apr_status(errno);
				}
			}
			
			apr_time_t begin = apr_time_now();
			tmp = ::read(pipe, (char *) buffer + already_read, size - already_read);
			timeout -= apr_time_now() - begin;
			if (tmp > 0) {
				// Data has been read.
				already_read += tmp;
				if (already_read == size) {
					return APR_SUCCESS;
				}
			} else if (tmp == -1) {
				// Error
				if (errno == EINTR) {
					// Interrupted system call; try again.
					continue;
				} else {
					return errno_to_apr_status(errno);
				}
			} else {
				// TODO: return what is already in the buffer
				return APR_EOF;
			}
		}
	}
	
	apr_status_t
	read_chunk_size(unsigned int &chunk_size, apr_interval_time_t &timeout) {
		uint16_t tmp;
		apr_status_t result = read_block(&tmp, sizeof(tmp), timeout);
		if (result == APR_SUCCESS) {
			chunk_size = ntohs(tmp);
		}
		return result;
	}
	
	apr_bucket *
	dup_bucket(apr_bucket_alloc_t *list) {
		apr_bucket *b = (apr_bucket *) apr_bucket_alloc(sizeof(apr_bucket), list);
		APR_BUCKET_INIT(b);
		b->free = apr_bucket_free;
		b->list = list;
		b->type = &bucket_type_dispatcher;
		b->length = (apr_size_t) -1;
		b->start = -1;
		b->data = this;
		return b;
	}
	
public:
	int pipe;
	apr_interval_time_t timeout;
	bool closed;
	
	apr_status_t
	read(apr_bucket *b, const char **str, apr_size_t *len, apr_read_type_e block) {
		apr_interval_time_t current_timeout;
		apr_status_t result;
		unsigned int chunk_size;
		
		*str = NULL;
		*len = 0;
		if (block == APR_NONBLOCK_READ) {
			current_timeout = 0;
		} else {
			current_timeout = timeout;
		}
		
		result = read_chunk_size(chunk_size, current_timeout);
		if (result == APR_EOF || (result == APR_SUCCESS && chunk_size == 0)) {
			b = apr_bucket_immortal_make(b, "", 0);
			*str = (const char *) b->data;
			close(pipe);
			closed = true;
			return APR_SUCCESS;
		} else if (result != APR_SUCCESS) {
			return result;
		}

		char *chunk = (char *) apr_bucket_alloc(chunk_size, b->list);
		result = read_block(chunk, chunk_size, current_timeout);
		if (result == APR_SUCCESS) {
			apr_bucket_heap *h;
			
			h = (apr_bucket_heap *) apr_bucket_heap_make(b, chunk, chunk_size, apr_bucket_free)->data;
			h->alloc_len = APR_BUCKET_BUFF_SIZE; /* note the real buffer size */
			*str = chunk;
			*len = chunk_size;
			APR_BUCKET_INSERT_AFTER(b, dup_bucket(b->list));
			return APR_SUCCESS;
		} else if (result == APR_EOF) {
			b = apr_bucket_immortal_make(b, "", 0);
			*str = (const char *) b->data;
			close(pipe);
			closed = true;
			return APR_SUCCESS;
		} else {
			return result;
		}
	}
};

extern "C" apr_bucket *
dispatcher_bucket_create(apr_pool_t *pool, int pipe, apr_interval_time_t timeout, apr_bucket_alloc_t *list) {
	apr_bucket *b;
	DispatcherBucket *data;

	b = (apr_bucket *) apr_bucket_alloc(sizeof(apr_bucket), list);
	if (b == NULL) {
		return NULL;
	}
	APR_BUCKET_INIT(b);
	b->free = apr_bucket_free;
	b->list = list;
	b->type = &bucket_type_dispatcher;
	b->length = (apr_size_t) -1;
	b->start = -1;
	
	data = (DispatcherBucket *) apr_palloc(pool, sizeof(DispatcherBucket));
	if (data == NULL) {
		apr_bucket_free(b);
		return NULL;
	}
	data->pipe = pipe;
	data->timeout = timeout;
	data->closed = false;
	b->data = data;
	return b;
}

static apr_status_t
dispatcher_bucket_read(apr_bucket *b, const char **str, apr_size_t *len, apr_read_type_e block) {
	return ((DispatcherBucket *) b->data)->read(b, str, len, block);
}

static void
dispatcher_bucket_destroy(void *d) {
	DispatcherBucket *data = (DispatcherBucket *) d;
	if (!data->closed) {
		close(data->pipe);
	}
}
