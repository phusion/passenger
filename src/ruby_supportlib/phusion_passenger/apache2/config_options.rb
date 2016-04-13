#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2015 Phusion Holding B.V.
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

# This file defines all supported Apache per-directory configuration options. The
# build system automatically generates the corresponding Apache module boilerplate
# code from the definitions in this file.
#
# Main configuration options are not defined in this file, but are defined in
# src/apache2_module/Configuration.cpp instead.
#
# The following boilerplate code is generated:
#
#  * command_rec array members (ConfigurationCommands.cpp.erb)
#
# Options:
#
#  * name - The configuration option name. Required.
#  * context - The context in which this configuration option is valid.
#              Defaults to ["OR_OPTIONS", "ACCESS_CONF", "RSRC_CONF"]
#  * type - This configuration option's value type. Allowed types:
#           :string, :integer, :flag
#  * min_value - If `type` is :integer, then this specifies the minimum
#                allowed value. When nil (the default), there is no minimum.
#  * desc - A description for this configuration option. Required.
#  * header - The name of the corresponding CGI header. By default CGI header
#             generation code is automatically generated, using the configuration
#             option's name in uppercase as the CGI header name.
#             Setting this to nil will disable auto-generation of CGI header
#             generation code. You are then responsible for writing CGI header
#             passing code yourself in Hooks.cpp.
#  * header_expression - The expression to be passed to `addHeader()`.
#  * function - If nil, a setter function will be automatically generated. If
#               non-nil, must be the name of the setter function.

PhusionPassenger.require_passenger_lib 'constants'

APACHE2_DIRECTORY_CONFIGURATION_OPTIONS = [
  {
    :name => "PassengerRuby",
    :type => :string,
    :desc => "The Ruby interpreter to use.",
    :header_expression => "config->ruby ? config->ruby : serverConfig.defaultRuby"
  },
  {
    :name => "PassengerPython",
    :type => :string,
    :desc => "The Python interpreter to use."
  },
  {
    :name => "PassengerNodejs",
    :type => :string,
    :desc => "The Node.js command to use."
  },
  {
    :name => "PassengerMeteorAppSettings",
    :type => :string,
    :desc => "Settings file for (non-bundled) Meteor apps."
  },
  {
    :name => "PassengerAppEnv",
    :type => :string,
    :desc => "The environment under which applications are run."
  },
  {
    :name => "PassengerMinInstances",
    :type => :integer,
    :context => ["OR_LIMIT", "ACCESS_CONF", "RSRC_CONF"],
    :min_value => 0,
    :header  => "PASSENGER_MIN_PROCESSES",
    :desc => "The minimum number of application instances to keep when cleaning idle instances."
  },
  {
    :name => "PassengerMaxInstancesPerApp",
    :type => :integer,
    :context => ["RSRC_CONF"],
    :header  => "PASSENGER_MAX_PROCESSES",
    :desc => "The maximum number of simultaneously alive application instances a single application may occupy."
  },
  {
    :name => "PassengerUser",
    :type => :string,
    :context => ["ACCESS_CONF", "RSRC_CONF"],
    :desc => "The user that Ruby applications must run as."
  },
  {
    :name => "PassengerGroup",
    :type => :string,
    :context => ["ACCESS_CONF", "RSRC_CONF"],
    :desc => "The group that Ruby applications must run as."
  },
  {
    :name => "PassengerErrorOverride",
    :type => :flag,
    :context => ["OR_ALL"],
    :desc    => "Allow Apache to handle error response.",
    :header  => nil
  },
  {
    :name => "PassengerMaxRequests",
    :type => :integer,
    :context => ["OR_LIMIT", "ACCESS_CONF", "RSRC_CONF"],
    :min_value => 0,
    :desc => "The maximum number of requests that an application instance may process."
  },
  {
    :name => "PassengerStartTimeout",
    :type => :integer,
    :context => ["OR_LIMIT", "ACCESS_CONF", "RSRC_CONF"],
    :min_value => 1,
    :desc => "A timeout for application startup."
  },
  {
    :name => "PassengerHighPerformance",
    :type => :flag,
    :context => ["OR_ALL"],
    :desc    => "Enable or disable Passenger's high performance mode.",
    :header  => nil
  },
  {
    :name => "PassengerEnabled",
    :type => :flag,
    :context => ["OR_ALL"],
    :desc    => "Enable or disable Phusion Passenger.",
    :header  => nil
  },
  {
    :name      => "PassengerMaxRequestQueueSize",
    :type      => :integer,
    :min_value => 0,
    :context   => ["OR_ALL"],
    :desc      => "The maximum number of queued requests."
  },
  {
    :name      => "PassengerMaxPreloaderIdleTime",
    :type      => :integer,
    :min_value => 0,
    :context   => ["RSRC_CONF"],
    :desc      => "The maximum number of seconds that a preloader process may be idle before it is shutdown."
  },
  {
    :name => "PassengerLoadShellEnvvars",
    :type => :flag,
    :desc => "Whether to load environment variables from the shell before running the application."
  },
  {
    :name    => "PassengerBufferUpload",
    :type    => :flag,
    :context => ["OR_ALL"],
    :desc    => "Whether to buffer file uploads.",
    :header  => nil
  },
  {
    :name    => 'PassengerAppType',
    :type    => :string,
    :context => ["OR_ALL"],
    :desc    => "Force specific application type.",
    :header  => nil
  },
  {
    :name    => 'PassengerStartupFile',
    :type    => :string,
    :context => ["OR_ALL"],
    :desc    => "Force specific startup file."
  },
  {
    :name    => 'PassengerStickySessions',
    :type    => :flag,
    :context => ["OR_ALL"],
    :desc    => "Whether to enable sticky sessions."
  },
  {
    :name    => 'PassengerStickySessionsCookieName',
    :type    => :flag,
    :context => ["OR_ALL"],
    :desc    => "The cookie name to use for sticky sessions."
  },
  {
    :name     => "PassengerSpawnMethod",
    :type     => :string,
    :context  => ["RSRC_CONF"],
    :desc     => "The spawn method to use.",
    :function => "cmd_passenger_spawn_method"
  },
  {
    :name     => "PassengerShowVersionInHeader",
    :type     => :flag,
    :desc     => "Whether to show the Phusion Passenger version number in the X-Powered-By header."
  },
  {
    :name     => "PassengerFriendlyErrorPages",
    :type     => :flag,
    :desc     => "Whether to display friendly error pages when something goes wrong."
  },
  {
    :name     => "PassengerRestartDir",
    :type     => :string,
    :desc     => "The directory in which Passenger should look for restart.txt."
  },
  {
    :name     => "PassengerAppGroupName",
    :type     => :string,
    :desc     => "Application process group name."
  },
  {
    :name     => "PassengerForceMaxConcurrentRequestsPerProcess",
    :type     => :integer,
    :desc     => "Force #{SHORT_PROGRAM_NAME} to believe that an application process " \
                 "can handle the given number of concurrent requests per process"
  },
  {
    :name      => "PassengerLveMinUid",
    :type      => :integer,
    :min_value => 0,
    :context   => ["RSRC_CONF"],
    :desc      => "Minimum user id starting from which entering LVE and CageFS is allowed."
  },


  ##### Aliases #####

  {
    :name => "RailsEnv",
    :type => :string,
    :desc => "The environment under which applications are run.",
    :alias_for => "PassengerAppEnv"
  },
  {
    :name => "RackEnv",
    :type => :string,
    :desc => "The environment under which applications are run.",
    :alias_for => "PassengerAppEnv"
  },

  ##### Deprecated options #####

  {
    :name      => "RailsSpawnMethod",
    :type      => :string,
    :context   => ["RSRC_CONF"],
    :desc      => "Deprecated option.",
    :alias_for => "PassengerSpawnMethod"
  }
]
