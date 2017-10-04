#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'
PhusionPassenger.require_passenger_lib 'standalone/config_utils'
PhusionPassenger.require_passenger_lib 'utils/file_system_watcher'

module PhusionPassenger
  module Standalone

    class AppFinder
      STARTUP_FILES = [
        "config.ru",
        "passenger_wsgi.py",
        "app.js",
        ".meteor"
      ]
      WATCH_ENTRIES = [
        "config", "Passengerfile.json", "passenger-standalone.json"
      ]

      attr_accessor :dirs
      attr_reader :apps
      attr_reader :execution_root

      def self.supports_multi?
        false
      end

      def initialize(dirs, options = {}, local_options = {})
        @dirs = dirs
        @options = options.dup
        determine_mode_and_execution_root(options, local_options)
      end

      def scan
        apps = []
        watchlist = []

        if single_mode?
          app_root = find_app_root
          apps << {
            :server_names => ["_"],
            :root => app_root
          }
          watchlist << app_root
          WATCH_ENTRIES.each do |entry|
            if File.exist?("#{app_root}/#{entry}")
              watchlist << "#{app_root}/#{entry}"
            end
          end

          apps.map! do |app|
            @options.merge(app)
          end
        end

        @apps = apps
        @watchlist = watchlist
        return apps
      end

      def monitor(termination_pipe)
        raise "You must call #scan first" if !@apps

        watcher = PhusionPassenger::Utils::FileSystemWatcher.new(@watchlist, termination_pipe)
        if wait_on_io(termination_pipe, 3)
          return
        end

        while true
          changed = watcher.wait_for_change
          watcher.close
          if changed
            old_apps = @apps
            # The change could be caused by a write to some Passengerfile.json file.
            # Wait for a short period so that the write has a chance to finish.
            if wait_on_io(termination_pipe, 0.25)
              return
            end

            new_apps = scan
            watcher = PhusionPassenger::Utils::FileSystemWatcher.new(@watchlist, termination_pipe)
            if old_apps != new_apps
              yield(new_apps)
            end

            # Don't process change events again for a short while,
            # but do detect changes while waiting.
            if wait_on_io(termination_pipe, 3)
              return
            end
          else
            return
          end
        end
      ensure
        watcher.close if watcher
      end

      def single_mode?
        return @mode == :single
      end

      def multi_mode?
        return !single_mode?
      end

      ##################

    private
      class ConfigLoadError < StandardError
      end

      def find_app_root
        if @dirs.empty?
          return File.absolute_logical_path(".")
        else
          return File.absolute_logical_path(@dirs[0])
        end
      end

      # Only pass `local_options` if the directory that you're checking is
      # the directory that should be used in single mode.
      #
      # `local_options` must be the the value obtained from
      # `ConfigUtils.load_local_config_file_from_app_dir_param!`.
      def looks_like_app_directory?(dir, options = {}, local_options = {})
        options = options.dup
        ConfigUtils.load_local_config_file!(dir, options)
        options[:app_type] ||
          STARTUP_FILES.any? do |file|
            File.exist?("#{dir}/#{file}")
          end
      end

      def filename_to_server_names(filename)
        basename = File.basename(filename)
        names = [basename]
        if basename !~ /^www\.$/i
          names << "www.#{basename}"
        end
        return names
      end

      # Wait until the given IO becomes readable, or until the timeout has
      # been reached. Returns true if the IO became readable, false if the
      # timeout has been reached.
      def wait_on_io(io, timeout)
        return !!select([io], nil, nil, timeout)
      end

      def determine_mode_and_execution_root(options, local_options)
        @mode = :single
        if @dirs.empty?
          @execution_root = Dir.logical_pwd
        elsif @dirs.size == 1
          @execution_root = File.absolute_logical_path(@dirs[0])
        else
          @execution_root = Dir.logical_pwd
        end
      end

      ##################
    end

  end # module Standalone
end # module PhusionPassenger
