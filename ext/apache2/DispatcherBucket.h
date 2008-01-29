#ifndef _DISPATCHER_BUCKET_H_
#define _DISPATCHER_BUCKET_H_

#include <apr_pools.h>
#include <apr_buckets.h>

#ifdef __cplusplus
extern "C" {
#endif

apr_bucket *dispatcher_bucket_create(apr_pool_t *pool, int pipe, apr_interval_time_t timeout, apr_bucket_alloc_t *list);

#ifdef __cplusplus
}
#endif

#endif /* _DISPATCHER_BUCKET_H_ */
