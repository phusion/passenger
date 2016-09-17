#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2016 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

# This file defines all supported Nginx per-location configuration options. The
# build system automatically generates the corresponding Nginx module boilerplate
# code from the definitions in this file.
#
# Main configuration options are not defined in this file, but are defined in
# src/nginx_module/Configuration.c instead.
#
# The following boilerplate code is generated:
#
#  * ngx_command_t array members (ConfigurationCommands.c.erb)
#  * Location configuration structure definition (ConfigurationFields.h.erb)
#  * Location configuration structure initialization (CreateLocationConfig.c.erb)
#  * Location configuration merging (MergeLocationConfig.c.erb)
#  * Conversion of configuration options to CGI headers (CacheLocationConfig.c.erb)
#
# Options:
#
#  * name - The configuration option name. Required.
#  * context - The context in which this configuration option is valid.
#              Defaults to [:main, :srv, :loc, :lif]
#  * type - This configuration option's value type. Allowed types:
#           :string, :integer, :uinteger, :flag, :string_array, :string_keyval,
#           :path
#  * take - Tells Nginx how many parameters and what kind of parameter
#           this configuration option takes. It should be set to a string
#           such as "NGX_CONF_FLAG".
#           By default this is automatically inferred from `type`: for
#           example if `type` is :string then ConfigurationCommands.c.erb
#           will infer that `NGX_CONF_TAKE1` should be used.
#  * function - The name of the function that should be used to store the
#               configuration value into the corresponding structure. This function
#               is not auto-generated, so it must be the name of an existing
#               function. By default, the function name is automatically inferred
#               from `type`. For example if `type` is string then `function` is
#               inferred to be `ngx_conf_set_str_slot`.
#               If you set this to a string then you are responsible for defining
#               said function in Configuration.c.
#  * struct - The type of the struct that the field is contained in. Something like
#             "NGX_HTTP_LOC_CONF_OFFSET" (which is also the default).
#  * field - The name that should be used for the auto-generated field in
#            the location configuration structure. Defaults to the configuration
#            name without the 'passenger_' prefix. Set this to nil if you do not
#            want a structure field to be auto-generated. If the field name contains
#            a dot (.e.g `upstream_config.pass_headers`) then the structure field will
#            also not be auto-generated, because it is assumed to belong to an existing
#            structure field.
#  * post - The extra information needed by function for post-processing.
#  * header - The name of the corresponding CGI header. By default CGI header
#             generation code is automatically generated, using the configuration
#             option's name in uppercase as the CGI header name.
#             Setting this to nil, or setting `field` to a value containing a dot,
#             will disable auto-generation of CGI header generation code. You are
#             then responsible for writing CGI header passing code yourself in
#             ContentHandler.c.
#  * auto_generate_nginx_merge_code - Whether location configuration merging
#            code should be automatically generated. Defaults to true. If you set
#            this to false then you are responsible for writing merging code
#            yourself in Configuration.c.
#  * alias_for - Set this if this configuration option is an alias for another
#                option. Alias definitions must only have the `name` and `alias_for`
#                fields, nothing else.


LOCATION_CONFIGURATION_OPTIONS = [
  {
    :name     => 'passenger_socket_backlog',
    :type     => :integer,
    :context  => [:main],
    :struct   => "NGX_HTTP_MAIN_CONF_OFFSET"
  },
  {
    :name     => 'passenger_core_file_descriptor_ulimit',
    :type     => :uinteger,
    :context  => [:main],
    :struct   => 'NGX_HTTP_MAIN_CONF_OFFSET'
  },
  {
    :name     => 'disable_security_update_check',
    :type     => :flag,
    :context  => [:main],
    :struct   => "NGX_HTTP_MAIN_CONF_OFFSET"
  },
  {
    :name     => 'security_update_check_proxy',
    :type     => :string,
    :context  => [:main],
    :struct   => "NGX_HTTP_MAIN_CONF_OFFSET"
  },
  {
    :name     => 'passenger_app_file_descriptor_ulimit',
    :type     => :uinteger
  },
  {
    :name     => 'passenger_enabled',
    :context  => [:srv, :loc, :lif],
    :type     => :flag,
    :function => 'passenger_enabled',
    :field    => 'enabled',
    :header   => nil
  },
  {
    :name    => 'passenger_ruby',
    :context => [:srv, :loc, :lif],
    :type    => :string
  },
  {
    :name  => 'passenger_python',
    :type  => :string
  },
  {
    :name  => 'passenger_nodejs',
    :type  => :string
  },
  {
    :name  => 'passenger_meteor_app_settings',
    :type  => :string
  },
  {
    :name  => 'passenger_app_env',
    :type  => :string,
    :field => 'environment'
  },
  {
    :name  => 'passenger_friendly_error_pages',
    :type  => :flag
  },
  {
    :name   => 'passenger_min_instances',
    :type   => :integer,
    :header => 'PASSENGER_MIN_PROCESSES'
  },
  {
    :name     => 'passenger_max_instances_per_app',
    :context  => [:main],
    :type     => :integer,
    :header   => 'PASSENGER_MAX_PROCESSES'
  },
  {
    :name  => 'passenger_max_requests',
    :type  => :integer
  },
  {
    :name  => 'passenger_start_timeout',
    :type  => :integer
  },
  {
    :name   => 'passenger_base_uri',
    :type   => :string_array,
    :field  => 'base_uris',
    :header => nil
  },
  {
    :name   => 'passenger_document_root',
    :type   => :string,
    :header => nil
  },
  {
    :name  => 'passenger_user',
    :type  => :string
  },
  {
    :name  => 'passenger_group',
    :type  => :string
  },
  {
    :name  => 'passenger_app_group_name',
    :type  => :string
  },
  {
    :name  => 'passenger_app_root',
    :type  => :string
  },
  {
    :name => 'passenger_app_rights',
    :type => :string
  },
  {
    :name  => 'union_station_support',
    :type  => :flag
  },
  {
    :name     => 'union_station_filter',
    :take     => 'NGX_CONF_TAKE1',
    :type     => :string_array,
    :function => 'union_station_filter',
    :field    => 'union_station_filters',
    :header   => nil
  },
  {
    :name  => 'passenger_debugger',
    :type  => :flag
  },
  {
    :name  => 'passenger_max_preloader_idle_time',
    :type  => :integer
  },
  {
    :name     => 'passenger_ignore_headers',
    :take     => 'NGX_CONF_1MORE',
    :function => 'ngx_conf_set_bitmask_slot',
    :field    => 'upstream_config.ignore_headers',
    :post     => '&ngx_http_upstream_ignore_headers_masks'
  },
  {
    :name   => 'passenger_env_var',
    :type   => :string_keyval,
    :field  => 'env_vars',
    :header => nil
  },
  {
    :name   => 'passenger_set_header',
    :type   => :string_keyval,
    :field  => 'headers_source',
    :header => nil,
    :auto_generate_nginx_create_code => false,
    :auto_generate_nginx_merge_code  => false
  },
  {
    :name  => 'passenger_pass_header',
    :type  => :string_array,
    :field => 'upstream_config.pass_headers'
  },
  {
    :name    => 'passenger_headers_hash_max_size',
    :type    => :uinteger,
    :header  => nil,
    :default => 512
  },
  {
    :name    => 'passenger_headers_hash_bucket_size',
    :type    => :uinteger,
    :header  => nil,
    :default => 64
  },
  {
    :name  => 'passenger_ignore_client_abort',
    :type  => :flag,
    :field => 'upstream_config.ignore_client_abort'
  },
  {
    :name  => 'passenger_buffer_response',
    :type  => :flag,
    :field => 'upstream_config.buffering'
  },
  {
    :name     => 'passenger_buffer_size',
    :take     => 'NGX_CONF_TAKE1',
    :function => 'ngx_conf_set_size_slot',
    :field    => 'upstream_config.buffer_size'
  },
  {
    :name     => 'passenger_buffers',
    :take     => 'NGX_CONF_TAKE2',
    :function => 'ngx_conf_set_bufs_slot',
    :field    => 'upstream_config.bufs'
  },
  {
    :name     => 'passenger_busy_buffers_size',
    :take     => 'NGX_CONF_TAKE1',
    :function => 'ngx_conf_set_size_slot',
    :field    => 'upstream_config.busy_buffers_size_conf'
  },
  {
    :name     => 'passenger_intercept_errors',
    :type     => :flag,
    :field    => 'upstream_config.intercept_errors'
  },
  {
    :name  => 'passenger_spawn_method',
    :type  => :string
  },
  {
    :name  => 'passenger_load_shell_envvars',
    :type  => :flag
  },
  {
    :name  => 'union_station_key',
    :type  => :string
  },
  {
    :name  => 'passenger_max_request_queue_size',
    :type  => :integer
  },
  {
    :name  => 'passenger_request_queue_overflow_status_code',
    :type  => :integer
  },
  {
    :name  => 'passenger_restart_dir',
    :type  => :string
  },
  {
    :name   => 'passenger_app_type',
    :type   => :string,
    :header => nil
  },
  {
    :name   => 'passenger_startup_file',
    :type   => :string
  },
  {
    :name   => 'passenger_sticky_sessions',
    :type   => :flag
  },
  {
    :name   => 'passenger_sticky_sessions_cookie_name',
    :type   => :string
  },
  {
    :name   => 'passenger_vary_turbocache_by_cookie',
    :type   => :string
  },
  {
    :name   => 'passenger_abort_websockets_on_process_shutdown',
    :type   => :flag
  },
  {
    :name   => 'passenger_force_max_concurrent_requests_per_process',
    :type   => :integer
  },

  ###### Enterprise features ######
  {
    :context  => [:main],
    :name     => 'passenger_fly_with',
    :type     => :string,
    :struct   => "NGX_HTTP_MAIN_CONF_OFFSET",
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_max_instances',
    :type     => :integer,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_max_request_time',
    :type     => :integer,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_memory_limit',
    :type     => :integer,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_concurrency_model',
    :type     => :string,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_thread_count',
    :type     => :integer,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_rolling_restarts',
    :type     => :flag,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },
  {
    :name     => 'passenger_resist_deployment_errors',
    :type     => :flag,
    :function => 'passenger_enterprise_only',
    :field    => nil
  },

  ###### Aliases for backwards compatibility ######
  {
    :name      => 'rails_spawn_method',
    :alias_for => 'passenger_spawn_method'
  },
  {
    :name      => 'rails_env',
    :alias_for => 'passenger_app_env'
  },
  {
    :name      => 'rack_env',
    :alias_for => 'passenger_app_env'
  },
  {
    :name      => 'rails_app_spawner_idle_time',
    :alias_for => 'passenger_max_preloader_idle_time'
  },

  ###### Obsolete options ######
  {
    :name     => 'rails_framework_spawner_idle_time',
    :take     => 'NGX_CONF_TAKE1',
    :function => 'rails_framework_spawner_idle_time',
    :field    => nil
  },
  {
    :name     => 'passenger_use_global_queue',
    :take     => 'NGX_CONF_FLAG',
    :function => 'passenger_use_global_queue',
    :field    => nil
  }
]
