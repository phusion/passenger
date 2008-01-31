#ifndef _DISPATCHER_BUCKET_H_
#define _DISPATCHER_BUCKET_H_

#include <apr_pools.h>
#include <apr_buckets.h>
#include "Application.h"

apr_bucket *dispatcher_bucket_create(apr_pool_t *pool, Passenger::ApplicationPtr app,
	apr_interval_time_t timeout, apr_bucket_alloc_t *list);

#endif /* _DISPATCHER_BUCKET_H_ */
