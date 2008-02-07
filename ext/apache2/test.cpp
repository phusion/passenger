#include <apr_tables.h>
#include <ext/hash_map>
#include <string>

using namespace std;
using namespace __gnu_cxx;

inline void
addHeader(apr_table_t *table, const char *name, const char *value) {
	if (name != NULL && value != NULL) {
		apr_table_addn(table, name, value);
	}
}

inline void
clear(apr_table_t *table) {
	apr_table_clear(table);
}

apr_table_t *
createTable() {
	apr_pool_t *pool;
	
	apr_initialize();
	apr_pool_create(&pool, NULL);
	return apr_table_make(pool, 40);
}

typedef hash_map<const char *, const char *> Table;

inline void
addHeader(Table &table, const char *name, const char *value) {
	if (name != NULL && value != NULL) {
		table[name] = value;
	}
}

inline void
clear(Table &table) {
	table.clear();
}

int
main() {
	//apr_table_t *headers = createTable();
	Table headers;
	
	for (int i = 0; i < 5000000; i++) {
		addHeader(headers, "SERVER_SOFTWARE", "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "SERVER_PROTOCOL", "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "SERVER_NAME",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "SERVER_ADMIN",    "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "SERVER_ADDR",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "SERVER_PORT",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "REMOTE_ADDR",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "REMOTE_PORT",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "REMOTE_USER",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "REQUEST_METHOD",  "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "REQUEST_URI",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "QUERY_STRING",    "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "HTTPS",           "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "CONTENT_TYPE",    "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "DOCUMENT_ROOT",   "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_REMOTE_PORT",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_REMOTE_USER",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_REQUEST_METHOD",  "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_REQUEST_URI",     "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_QUERY_STRING",    "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_HTTPS",           "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_CONTENT_TYPE",    "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		addHeader(headers, "X_DOCUMENT_ROOT",   "aaaaaaaaaaaaaaaaaaaaaaaaaa");
		clear(headers);
	}
	return 0;
}
