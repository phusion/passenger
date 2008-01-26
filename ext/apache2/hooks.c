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
determine_rails_root(request_rec *r, RailsConfig *config) {
	size_t len = strlen(r->filename) - strlen(r->uri);
	if (len <= 0) {
		return NULL;
	} else {
		return apr_pstrndup(r->pool, r->filename, len);
	}
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
	const char *rails_root;
	
	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL) {
		return DECLINED;
	} else if (strcmp(r->uri, "mod_rails:htaccess_is_interfering") == 0) {
		ap_rputs("<b>Error:</b> The \".htaccess\" file in your Ruby on Rails application's "
			"\"public\" directory is interfering with Apache. Please remove it.", r);
		return OK;
	} else if (!inside_base_uri(r, config) || r->filename == NULL || file_exists(r->pool, r->filename)) {
		return DECLINED;
	}
	
	rails_root = determine_rails_root(r, config);
	if (rails_root) {
		ap_rputs("", r);
		return OK;
	}
	
	char message[1024];
	apr_snprintf(message, sizeof(message), "mod_rails: file=%s, uri=%s, root=%s", r->filename, r->uri, determine_rails_root(r, config));
	log_debug(APLOG_MARK, r, message);
	return DECLINED;
}

static int
mod_rails_map_to_storage(request_rec *r) {
	/* Apache's default map_to_storage process does strange things with the filename.
	 * Suppose that the DocumentRoot is /website, on server http://test.com/. If we access
	 * http://test.com/foo/bar, and /website/foo/bar does not exist, then Apache will
	 * change the filename to /website/foo instead of the expected /website/bar.
	 * Here, we make sure that doesn't happen.
	 *
	 * Incidentally, it also disables mod_rewrite. That is a good thing because the default
	 * Rails .htaccess file interferes with mod_rails anyway.
	 */
	RailsConfig *config = get_config(r);
	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL || !inside_base_uri(r, config)) {
		return DECLINED;
	} else {
		return OK;
	}
}

static int
mod_rails_check_legacy(request_rec *r) {
	/* The default Rails .htaccess file interferes with mod_rails because it tries to
	 * dispatch requests to dispatch.cgi. Here we make sure that mod_rails_handle_request()
	 * will be able to warn the user if that is the case.
	 */
	
	RailsConfig *config = get_config(r);

	if (!is_well_formed_uri(r->uri) || config->base_uri == NULL || !inside_base_uri(r, config)) {
		return DECLINED;
	} else if (r->filename != NULL
	       && (
	             strcmp(r->filename, "redirect:/dispatch.cgi") == 0
	          || strcmp(r->filename, "redirect:/dispatch.fcgi") == 0
	       )) {
		r->uri = "mod_rails:htaccess_is_interfering";
		return OK;
	} else {
		return DECLINED;
	}
}

static void mod_rails_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(mod_rails_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_fixups(mod_rails_check_legacy, NULL, NULL, APR_HOOK_LAST);
	ap_hook_map_to_storage(mod_rails_map_to_storage, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_handler(mod_rails_handle_request, NULL, NULL, APR_HOOK_FIRST);
}

#endif /* INSIDE_MOD_RAILS */
