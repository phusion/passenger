#ifndef _PASSENGER_HOOKS_H_
#define _PASSENGER_HOOKS_H_

#include <apr_pools.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void passenger_register_hooks(apr_pool_t *p);

#ifdef __cplusplus
}
#endif

#endif /* _PASSENGER_HOOKS_H_ */
