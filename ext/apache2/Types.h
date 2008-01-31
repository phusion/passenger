#ifndef _PASSENGER_TYPES_H_
#define _PASSENGER_TYPES_H_

#define PASSENGER_VERSION "1.0.0"

struct RailsConfig {
	const char *base_uri;
	char *base_uri_with_slash;
	const char *env;
};

#endif /* _PASSENGER_TYPES_H_ */
