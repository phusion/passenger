#ifdef INSIDE_MOD_RAILS

static int
file_exists(apr_pool_t *pool, const char *filename) {
	apr_finfo_t info;
	return apr_stat(&info, filename, APR_FINFO_NORM, pool) == APR_SUCCESS;
}

static INLINE bool
is_well_formed_uri(const char *uri) {
	return uri[0] == '/';
}

/**
 * Check whether config->base_uri is a base URI of the URI of the given request.
 */
static INLINE bool
inside_base_uri(request_rec *r, RailsConfig *config) {
	return strcmp(r->uri, config->base_uri) == 0
	    || strncmp(r->uri, config->base_uri_with_slash, strlen(config->base_uri_with_slash)) == 0;
}

static INLINE const char *
determine_rails_dir(request_rec *r, RailsConfig *config) {
	size_t len = strlen(r->filename) - strlen(r->uri);
	if (len <= 0) {
		return NULL;
	} else {
		return apr_pstrndup(r->pool, r->filename, len);
	}
}

static INLINE bool
verify_rails_dir(apr_pool_t *pool, const char *dir) {
	return file_exists(pool, apr_pstrcat(pool, dir, "/../config/environment.rb", NULL));
}

static int
mod_rails_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *base_server) {
	ap_add_version_component(p, "mod_rails/" MOD_RAILS_VERSION);
	return OK;
}

static int
mod_rails_handle_request(request_rec *r) {
	/* The main request handler hook function.
	 */

	RailsConfig *config = get_config(r);
	const char *rails_dir;
	
	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL
	 || !inside_base_uri(r, config) || r->filename == NULL || file_exists(r->pool, r->filename)) {
		return DECLINED;
	}
	
	rails_dir = determine_rails_dir(r, config);
	if (rails_dir == NULL) {
		ap_set_content_type(r, "text/html; charset=UTF-8");
		ap_rputs("<h1>mod_rails error #1</h1>\n", r);
		ap_rputs("Cannot determine the location of the Rails application's \"public\" directory.", r);
		return OK;
	} else if (!verify_rails_dir(r->pool, rails_dir)) {
		ap_set_content_type(r, "text/html; charset=UTF-8");
		ap_rputs("<h1>mod_rails error #2</h1>\n", r);
		ap_rputs("mod_rails thinks that the Rails application's \"public\" directory is \"", r);
		ap_rputs(ap_escape_html(r->pool, rails_dir), r);
		ap_rputs("\", but it doesn't seem to be valid.", r);
		return OK;
	}
	
	char message[1024];
	apr_snprintf(message, sizeof(message), "mod_rails: file=%s, uri=%s, root=%s", r->filename, r->uri, rails_dir);
	log_debug(APLOG_MARK, r, message);
	return DECLINED;
}

static int
mod_rails_map_to_storage(request_rec *r) {
	RailsConfig *config = get_config(r);
	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL || !inside_base_uri(r, config)) {
		return DECLINED;
	} else {
		char *html_file = apr_pstrcat(r->pool, r->filename, ".html", NULL);
		if (file_exists(r->pool, html_file)) {
			/* If a .html version of the URI exists, serve it directly.
			 * This is used by page caching.
			 */
			r->filename = html_file;
			r->canonical_filename = html_file;
			return DECLINED;
		} else {
			/* Apache's default map_to_storage process does strange things with the filename.
			 * Suppose that the DocumentRoot is /website, on server http://test.com/. If we access
			 * http://test.com/foo/bar, and /website/foo/bar does not exist, then Apache will
			 * change the filename to /website/foo instead of the expected /website/bar.
			 * We make sure that doesn't happen.
			 *
			 * Incidentally, this also disables mod_rewrite. That is a good thing because
			 * the default Rails .htaccess file interferes with mod_rails anyway.
			 */
			return OK;
		}
	}
}

static void mod_rails_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(mod_rails_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_map_to_storage(mod_rails_map_to_storage, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(mod_rails_handle_request, NULL, NULL, APR_HOOK_FIRST);
}

#endif /* INSIDE_MOD_RAILS */
