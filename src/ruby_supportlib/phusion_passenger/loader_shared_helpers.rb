# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2011-2015 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'public_api'
PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'
PhusionPassenger.require_passenger_lib 'debug_logging'
PhusionPassenger.require_passenger_lib 'utils/shellwords'

module PhusionPassenger

  # Provides shared functions for loader and preloader apps.
  module LoaderSharedHelpers
    extend self

    # To be called by the (pre)loader as soon as possible.
    def init(options)
      Thread.main[:name] = "Main thread"
      # We don't dump PATH info because at this point it's
      # unlikely to be changed.
      dump_ruby_environment
      check_rvm_using_wrapper_script(options)
      return sanitize_spawn_options(options)
    end

    def check_rvm_using_wrapper_script(options)
      ruby = options["ruby"]
      if ruby =~ %r(/\.?rvm/) && ruby =~ %r(/bin/ruby$)
        case options["integration_mode"] || DEFAULT_INTEGRATION_MODE
        when "nginx"
          passenger_ruby = "passenger_ruby"
          passenger_ruby_doc = "https://www.phusionpassenger.com/library/config/nginx/reference/#setting_correct_passenger_ruby_value"
        when "apache"
          passenger_ruby = "PassengerRuby"
          passenger_ruby_doc = "https://www.phusionpassenger.com/library/config/apache/reference/#setting_correct_passenger_ruby_value"
        when "standalone"
          passenger_ruby = "--ruby"
          passenger_ruby_doc = "https://www.phusionpassenger.com/library/config/standalone/reference/#setting_correct_passenger_ruby_value"
        end

        raise "You've set the `#{passenger_ruby}` option to '#{ruby}'. " +
          "However, because you are using RVM, this is not allowed: the option must point to " +
          "an RVM wrapper script, not a raw Ruby binary. This is because RVM is implemented " +
          "through various environment variables, which are set through the wrapper script.\n" +
          "\n" +
          "To find out the correct value for `#{passenger_ruby}`, please read:\n\n" +
          "  #{passenger_ruby_doc}\n" +
          "\n-------------------------\n"
      end
    end

    # To be called whenever the (pre)loader is about to abort with an error.
    def about_to_abort(options, exception = nil)
      dump_all_information(options)
      # https://code.google.com/p/phusion-passenger/issues/detail?id=1039
      puts
    end

    def to_boolean(value)
      return !(value.nil? || value == false || value == "false")
    end

    def sanitize_spawn_options(options)
      defaults = {
        "app_type"         => "rack",
        "environment"      => "production",
        "print_exceptions" => true
      }
      options = defaults.merge(options)
      options["app_group_name"]            = options["app_root"] if !options["app_group_name"]
      options["print_exceptions"]          = to_boolean(options["print_exceptions"])
      options["analytics"]                 = to_boolean(options["analytics"])
      options["show_version_in_header"]    = to_boolean(options["show_version_in_header"])
      options["log_level"]                 = options["log_level"].to_i if options["log_level"]
      # TODO: smart spawning is not supported when using ruby-debug. We should raise an error
      # in this case.
      options["debugger"]     = to_boolean(options["debugger"])
      options["spawn_method"] = "direct" if options["debugger"]

      return options
    end

    def dump_all_information(options)
      dump_ruby_environment
      dump_envvars
      dump_system_metrics(options)
    end

    def dump_ruby_environment
      if dir = ENV['PASSENGER_DEBUG_DIR']
        File.open("#{dir}/ruby_info", "w") do |f|
          f.puts "RUBY_VERSION = #{RUBY_VERSION}"
          f.puts "RUBY_PLATFORM = #{RUBY_PLATFORM}"
          f.puts "RUBY_ENGINE = #{defined?(RUBY_ENGINE) ? RUBY_ENGINE : 'nil'}"
        end
        File.open("#{dir}/load_path", "wb") do |f|
          $LOAD_PATH.each do |path|
            f.puts path
          end
        end
        File.open("#{dir}/loaded_libs", "wb") do |f|
          $LOADED_FEATURES.each do |filename|
            f.puts filename
          end
        end

        # We write to these files last because the 'require' calls can fail.
        require 'rbconfig' if !defined?(RbConfig::CONFIG)
        File.open("#{dir}/rbconfig", "wb") do |f|
          RbConfig::CONFIG.each_pair do |key, value|
            f.puts "#{key} = #{value}"
          end
        end
        begin
          require 'rubygems' if !defined?(Gem)
        rescue LoadError
        end
        if defined?(Gem)
          File.open("#{dir}/ruby_info", "a") do |f|
            f.puts "RubyGems version = #{Gem::VERSION}"
            if Gem.respond_to?(:path)
              f.puts "RubyGems paths = #{Gem.path.inspect}"
            else
              f.puts "RubyGems paths = unknown; incompatible RubyGems API"
            end
          end
          File.open("#{dir}/activated_gems", "wb") do |f|
            if Gem.respond_to?(:loaded_specs)
              Gem.loaded_specs.each_pair do |name, spec|
                f.puts "#{name} => #{spec.version}"
              end
            else
              f.puts "Unable to query this information; incompatible RubyGems API."
            end
          end
        end
      end
    rescue SystemCallError
      # Don't care.
    end

    def dump_envvars
      if dir = ENV['PASSENGER_DEBUG_DIR']
        File.open("#{dir}/envvars", "wb") do |f|
          ENV.each_pair do |key, value|
            f.puts "#{key} = #{value}"
          end
        end
      end
    rescue SystemCallError
      # Don't care.
    end

    def dump_system_metrics(options)
      if dir = ENV['PASSENGER_DEBUG_DIR']
        # When invoked through Passenger Standalone, we want passenger-config
        # to use the PassengerAgent in the Passsenger Standalone buildout directory,
        # because the one in the source root may not exist.
        passenger_config = "#{PhusionPassenger.bin_dir}/passenger-config"
        if is_ruby_program?(passenger_config)
          ruby = options["ruby"]
        else
          ruby = nil
        end
        command = [
          "env",
          "PASSENGER_LOCATION_CONFIGURATION_FILE=#{PhusionPassenger.install_spec}",
          ruby,
          passenger_config,
          "system-metrics"
        ].compact
        contents = `#{Shellwords.join(command)}`
        if $? && $?.exitstatus == 0
          File.open("#{dir}/system_metrics", "wb") do |f|
            f.write(contents)
          end
        end
      end
    rescue SystemCallError
      # Don't care.
    end

    def is_ruby_program?(path)
      File.open(path, "rb") do |f|
        f.readline =~ /ruby/
      end
    rescue EOFError
      false
    end

    # Prepare an application process using rules for the given spawn options.
    # This method is to be called before loading the application code.
    #
    # +startup_file+ is the application type's startup file, e.g.
    # "config/environment.rb" for Rails apps and "config.ru" for Rack apps.
    # +options+ are the spawn options that were given.
    #
    # This function may modify +options+. The modified options are to be
    # passed to the request handler.
    def before_loading_app_code_step1(startup_file, options)
      DebugLogging.log_level = options["log_level"] if options["log_level"]

      # We always load the union_station_hooks_* gems and do not check for
      # `options["analytics"]` here. The gems don't actually initialize (and
      # load the bulk of their code) unless they have determined that
      # `options["analytics"]` is true. Regardless of whether Union Station
      # support is enabled in Passenger, the UnionStationHooks namespace must
      # be available so that applications can call it, even though the actual
      # calls don't do anything when Union Station support is disabled.
      PhusionPassenger.require_passenger_lib 'vendor/union_station_hooks_core/lib/union_station_hooks_core'
      UnionStationHooks.vendored = true
      PhusionPassenger.require_passenger_lib 'vendor/union_station_hooks_rails/lib/union_station_hooks_rails'
      UnionStationHooksRails.vendored = true
    end

    def run_load_path_setup_code(options)
      # rack-preloader.rb depends on the 'rack' library, but the app
      # might want us to use a bundled version instead of a
      # gem/apt-get/yum/whatever-installed version. Therefore we must setup
      # the correct load paths before requiring 'rack'.
      #
      # The most popular tool for bundling dependencies is Bundler. Bundler
      # works as follows:
      # - If the bundle is locked then a file .bundle/environment.rb exists
      #   which will setup the load paths.
      # - If the bundle is not locked then the load paths must be set up by
      #   calling Bundler.setup.
      # - Rails 3's boot.rb automatically loads .bundle/environment.rb or
      #   calls Bundler.setup if that's not available.
      # - Other Rack apps might not have a boot.rb but we still want to setup
      #   Bundler.
      # - Some Rails 2 apps might have explicitly added Bundler support.
      #   These apps call Bundler.setup in their preinitializer.rb.
      #
      # So the strategy is as follows:

      # Our strategy might be completely unsuitable for the app or the
      # developer is using something other than Bundler, so we let the user
      # manually specify a load path setup file.
      if options["load_path_setup_file"]
        require File.expand_path(options["load_path_setup_file"])

      # The app developer may also override our strategy with this magic file.
      elsif File.exist?('config/setup_load_paths.rb')
        require File.expand_path('config/setup_load_paths')

      # Older versions of Bundler use .bundle/environment.rb as the Bundler
      # environment lock file. This has been replaced by Gemfile.lock in later
      # versions, but we still support the older mechanism.
      # If the Bundler environment lock file exists then load that. If it
      # exists then there's a 99.9% chance that loading it is the correct
      # thing to do.
      elsif File.exist?('.bundle/environment.rb')
        running_bundler(options) do
          require File.expand_path('.bundle/environment')
        end

      # If the legacy Bundler environment file doesn't exist then there are two
      # possibilities:
      # 1. Bundler is not used, in which case we don't have to do anything.
      # 2. Bundler *is* used, but either the user is using a newer Bundler versions,
      #    or the gems are not locked. In either case, we're supposed to call
      #    Bundler.setup.
      #
      # The existence of Gemfile indicates whether (2) is true:
      elsif File.exist?('Gemfile')
        # In case of Rails 3, config/boot.rb already calls Bundler.setup.
        # However older versions of Rails may not so loading boot.rb might
        # not be the correct thing to do. To be on the safe side we
        # call Bundler.setup ourselves; calling Bundler.setup twice is
        # harmless. If this isn't the correct thing to do after all then
        # there's always the load_path_setup_file option and
        # setup_load_paths.rb.
        running_bundler(options) do
          activate_gem 'bundler', 'bundler/setup'
        end
      end


      # !!! NOTE !!!
      # If the app is using Bundler then any dependencies required past this
      # point must be specified in the Gemfile. Like ruby-debug if debugging is on...
    end

    # This method is to be called after the load path has been set up
    # (e.g. Bundler.setup is called), but before loading the app code.
    def before_loading_app_code_step2(options)
      # Do nothing
    end

    # This method is to be called after loading the application code but
    # before forking a worker process.
    def after_loading_app_code(options)
      UnionStationHooks.check_initialized
    end

    # If the current working directory equals `app_root`, and `abs_path` is a
    # file inside `app_root`, then returns its basename. Otherwise, returns
    # `abs_path`.
    #
    # The main use case for this method is to ensure that we load config.ru
    # with a relative path (only its base name) in most circumstances,
    # instead of with an absolute path. This is necessary in order to retain
    # compatibility with apps that expect config.ru's __FILE__ to be relative.
    # See https://github.com/phusion/passenger/issues/1596
    def maybe_make_path_relative_to_app_root(app_root, abs_path)
      if Dir.logical_pwd == app_root && File.dirname(abs_path) == app_root
        File.basename(abs_path)
      else
        abs_path
      end
    end

    def create_socket_address(protocol, address)
      if protocol == 'unix'
        return "unix:#{address}"
      elsif protocol == 'tcp'
        return "tcp://#{address}"
      else
        raise ArgumentError, "Unknown protocol '#{protocol}'"
      end
    end

    def advertise_readiness
      # https://code.google.com/p/phusion-passenger/issues/detail?id=1039
      puts

      puts "!> Ready"
    end

    def advertise_sockets(output, request_handler)
      request_handler.server_sockets.each_pair do |name, options|
        concurrency = PhusionPassenger.advertised_concurrency_level || options[:concurrency]
        output.puts "!> socket: #{name};#{options[:address]};#{options[:protocol]};#{concurrency}"
      end
    end

    # To be called before the request handler main loop is entered, but after the app
    # startup file has been loaded. This function will fire off necessary events
    # and perform necessary preparation tasks.
    #
    # +forked+ indicates whether the current worker process is forked off from
    # an ApplicationSpawner that has preloaded the app code.
    # +options+ are the spawn options that were passed.
    def before_handling_requests(forked, options)
      if forked
        # Reseed pseudo-random number generator for security reasons.
        srand
      end

      if options["process_title"] && !options["process_title"].empty?
        $0 = options["process_title"] + ": " + options["app_group_name"]
      end

      # If we were forked from a preloader process then clear or
      # re-establish ActiveRecord database connections. This prevents
      # child processes from concurrently accessing the same
      # database connection handles.
      if forked && defined?(ActiveRecord::Base)
        if ActiveRecord::Base.respond_to?(:clear_all_connections!)
          ActiveRecord::Base.clear_all_connections!
        elsif ActiveRecord::Base.respond_to?(:clear_active_connections!)
          ActiveRecord::Base.clear_active_connections!
        elsif ActiveRecord::Base.respond_to?(:connected?) &&
              ActiveRecord::Base.connected?
          ActiveRecord::Base.establish_connection
        end
      end

      # Fire off events.
      PhusionPassenger.call_event(:starting_worker_process, forked)
      if options["pool_account_username"] && options["pool_account_password_base64"]
        password = options["pool_account_password_base64"].unpack('m').first
        PhusionPassenger.call_event(:credentials,
          options["pool_account_username"], password)
      else
        PhusionPassenger.call_event(:credentials, nil, nil)
      end
    end

    # To be called after the request handler main loop is exited. This function
    # will fire off necessary events perform necessary cleanup tasks.
    def after_handling_requests
      PhusionPassenger.call_event(:stopping_worker_process)
    end

    # Activate a gem and require it. This method exists in order to load
    # a library from RubyGems instead of from vendor_ruby. For example,
    # on Debian systems, Rack may be installed from APT, but that is usually
    # a very old version which we don't want. This method ensures that the
    # RubyGems-installed version is loaded, not the the version in vendor_ruby.
    # See the following threads for discussion:
    # https://github.com/phusion/passenger/issues/1478
    # https://github.com/phusion/passenger/issues/1480
    def activate_gem(gem_name, library_name = nil)
      if !defined?(::Gem)
        begin
          require 'rubygems'
        rescue LoadError
        end
      end
      if Kernel.respond_to?(:gem, true)
        begin
          gem(gem_name)
        rescue Gem::LoadError
        end
      end
      require(library_name || gem_name)
    end

  private
    def running_bundler(options)
      yield
    rescue Exception => e
      if (defined?(Bundler::GemNotFound) && e.is_a?(Bundler::GemNotFound)) ||
         (defined?(Bundler::GitError) && e.is_a?(Bundler::GitError))
        PhusionPassenger.require_passenger_lib 'platform_info/ruby'
        comment =
          "<p>It looks like Bundler could not find a gem. Maybe you didn't install all the " +
          "gems that this application needs. To install your gems, please run:</p>\n\n" +
          "  <pre class=\"commands\">bundle install</pre>\n\n"
        ruby = options["ruby"]
        if ruby =~ %r(^/usr/local/rvm/)
          comment <<
            "<p>If that didn't work, then maybe the problem is that your gems are installed " +
            "to <code>#{h home_dir}/.rvm/gems</code>, while at the same time you set " +
            "<code>PassengerRuby</code> (Apache) or <code>passenger_ruby</code> (Nginx) to " +
            "<code>#{h ruby}</code>. Because of the latter, RVM does not load gems from the " +
            "home directory.</p>\n\n" +
            "<p>To make RVM load gems from the home directory, you need to set " +
            "<code>PassengerRuby</code>/<code>passenger_ruby</code> to an RVM wrapper script " +
            "inside the home directory:</p>\n\n" +
            "<ol>\n" +
            "  <li>Login as #{h whoami}.</li>\n"
          if PlatformInfo.rvm_installation_mode == :multi
            comment <<
              "  <li>Enable RVM mixed mode by running:\n" +
              "      <pre class=\"commands\">rvm user gemsets</pre></li>\n"
          end
          comment <<
            "  <li>Run this to find out what to set <code>PassengerRuby</code>/<code>passenger_ruby</code> to:\n" +
            "      <pre class=\"commands\">#{PlatformInfo.ruby_command} \\\n" +
            "#{PhusionPassenger.bin_dir}/passenger-config --detect-ruby</pre></li>\n" +
            "</ol>\n\n" +
            "<p>If that didn't help either, then maybe your application is being run under a " +
            "different environment than it's supposed to. Please check the following:</p>\n\n"
        else
          comment <<
            "<p>If that didn't work, then the problem is probably caused by your " +
            "application being run under a different environment than it's supposed to. " +
            "Please check the following:</p>\n\n"
        end
        comment << "<ol>\n"
        comment <<
          "  <li>Is this app supposed to be run as the <code>#{h whoami}</code> user?</li>\n" +
          "  <li>Is this app being run on the correct Ruby interpreter? Below you will\n" +
          "      see which Ruby interpreter Phusion Passenger attempted to use.</li>\n"
        if PlatformInfo.in_rvm?
          comment <<
            "  <li>Please check whether the correct RVM gemset is being used.</li>\n" +
            "  <li>Sometimes, RVM gemsets may be broken.\n" +
            "      <a href=\"https://github.com/phusion/passenger/wiki/Resetting-RVM-gemsets\">Try resetting them.</a></li>\n"
        end
        comment << "</ol>\n"
        prepend_exception_html_comment(e, comment)
      end
      raise e
    end

    def prepend_exception_html_comment(e, comment)
      # Since Exception doesn't allow changing the message, we monkeypatch
      # the #message and #to_s methods.
      separator = "\n<p>-------- The exception is as follows: -------</p>\n"
      new_message = comment + separator + h(e.message)
      new_s = comment + separator + h(e.to_s)
      metaclass = class << e; self; end
      metaclass.send(:define_method, :message) do
        new_message
      end
      metaclass.send(:define_method, :to_s) do
        new_s
      end
      metaclass.send(:define_method, :html?) do
        true
      end
    end

    def h(text)
      require 'erb' if !defined?(ERB)
      return ERB::Util.h(text)
    end

    def whoami
      require 'etc' if !defined?(Etc)
      begin
        user = Etc.getpwuid(Process.uid)
      rescue ArgumentError
        user = nil
      end
      if user
        return user.name
      else
        return "##{Process.uid}"
      end
    end

    def home_dir
      return PhusionPassenger.home_dir
    end
  end

end # module PhusionPassenger
