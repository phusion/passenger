#ifdef INSIDE_MOD_RAILS

static void
log_err(const char *file, int line, request_rec *r,
              apr_status_t status, const char *msg) {
	char buf[256] = "";
	apr_strerror(status, buf, sizeof(buf));
	ap_log_rerror(file, line, APLOG_ERR, status, r, "mod_rails: %s: %s", buf, msg);
}

static void
log_debug(const char *file, int line, request_rec *r, const
                      char *msg) {
	ap_log_rerror(file, line, APLOG_ERR, APR_SUCCESS, r, msg);
}

static RailsConfig *
get_config(request_rec *r) {
	return (RailsConfig *) ap_get_module_config(r->per_dir_config, &rails_module);
}

#endif /* INSIDE_MOD_RAILS */
