/**
 * This file implements an APR bucket which understands the request handler's
 * (= the dispatcher's) protocol.
 * See http://www.apachetutor.org/dev/brigades for information on APR buckets.
 */
#ifndef _DISPATCHER_BUCKET_H_
#define _DISPATCHER_BUCKET_H_

#include <apr_pools.h>
#include <apr_buckets.h>
#include "Application.h"

apr_bucket *dispatcher_bucket_create(apr_pool_t *pool,
	Passenger::ApplicationPtr app, Passenger::Application::LockPtr lock,
	apr_interval_time_t timeout, apr_bucket_alloc_t *list);

#endif /* _DISPATCHER_BUCKET_H_ */
