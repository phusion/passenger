#ifdef INSIDE_MOD_RAILS

static void *
create_dir_config(apr_pool_t *p, char *dirspec) {
	RailsConfig *config = apr_palloc(p, sizeof(RailsConfig));
	config->base_uri = NULL;
	config->base_uri_with_slash = NULL;
	config->env = NULL;
	return config;
}

static void *
merge_dir_config(apr_pool_t *p, void *basev, void *addv) {
	RailsConfig *config = apr_palloc(p, sizeof(RailsConfig));
	RailsConfig *base = (RailsConfig *) basev;
	RailsConfig *add = (RailsConfig *) addv;
	
	config->base_uri = (add->base_uri == NULL) ? base->base_uri : add->base_uri;
	config->base_uri_with_slash = (add->base_uri_with_slash == NULL) ? base->base_uri_with_slash : add->base_uri_with_slash;
	config->env = (add->env == NULL) ? base->env : add->env;
	return config;
}

static void *
create_server_config(apr_pool_t *p, server_rec *s) {
	return create_dir_config(p, NULL);
}

static void *
merge_server_config(apr_pool_t *p, void *basev, void *overridesv) {
	return merge_dir_config(p, basev, overridesv);
}

static const char *
cmd_rails_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	RailsConfig *config = (RailsConfig *) pcfg;
	config->base_uri = arg;
	if (strcmp(arg, "/") == 0) {
		config->base_uri_with_slash = "/";
	} else {
		config->base_uri_with_slash = apr_pstrcat(cmd->pool, arg, "/", NULL);
	}
	return NULL;
}

static const command_rec mod_rails_cmds[] = {
	AP_INIT_TAKE1("RailsBaseURI", cmd_rails_base_uri, NULL, OR_OPTIONS,
		"Reserve the given URI to a Rails application."),
	AP_INIT_TAKE1("RailsEnv", ap_set_string_slot, (void*) APR_OFFSETOF(RailsConfig, env), OR_OPTIONS,
		"The environment under which a Rails app must run."),
	{NULL}
};

#endif /* INSIDE_MOD_RAILS */
