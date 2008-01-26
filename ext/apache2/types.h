#ifdef INSIDE_MOD_RAILS

#define INLINE inline

typedef int bool;
#define true 1
#define false 0

typedef enum {
	UNSET,
	ENABLED,
	DISABLED
} Threeway;

typedef struct {
	const char *base_uri;
	char *base_uri_with_slash;
	const char *env;
} RailsConfig;

#endif /* INSIDE_MOD_RAILS */
