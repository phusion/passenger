# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2017 Phusion Holding B.V.
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

require 'fileutils'
PhusionPassenger.require_passenger_lib 'admin_tools/instance'

module PhusionPassenger
  module AdminTools

    class InstanceRegistry
      def initialize(paths = nil)
        @paths = [paths || default_paths].flatten.uniq
      end

      def list(options = {})
        options = {
          :clean_stale_or_corrupted => true
        }.merge(options)

        instances = []

        @paths.each do |path|
          Dir["#{path}/passenger.*"].each do |dir|
            instance = Instance.new(dir)
            case instance.state
            when :good
              if instance.locked?
                instances << instance
              elsif options[:clean_stale_or_corrupted]
                cleanup(dir)
              end
            when :structure_version_unsupported
              next
            when :corrupted
              if !instance.locked? && options[:clean_stale_or_corrupted]
                cleanup(dir)
              end
            when :not_finalized
              if instance.stale? && options[:clean_stale_or_corrupted]
                cleanup(dir)
              end
            end
          end
        end

        instances
      end

      def find_by_name(name, options = {})
        return list(options).find { |instance| instance.name == name }
      end

      def find_by_name_prefix(name, options = {})
        prefix = /^#{Regexp.escape name}/
        results = list(options).find_all { |instance| instance.name =~ prefix }
        if results.size <= 1
          return results.first
        else
          return :ambiguous
        end
      end

    private
      def default_paths
        if result = string_env("PASSENGER_INSTANCE_REGISTRY_DIR")
          return [result]
        end

        # On OSX, TMPDIR is set to a different value per-user. But Apache
        # is launched through Launchctl and runs without TMPDIR (and thus
        # uses the default /tmp).
        #
        # The RPM packages configure Apache and Nginx to use /var/run/passenger-instreg
        # as the instance registry dir. See https://github.com/phusion/passenger/issues/1475
        [string_env("TMPDIR"), "/tmp", "/var/run/passenger-instreg"].compact
      end

      def string_env(name)
        if (result = ENV[name]) && !result.empty?
          result
        else
          nil
        end
      end

      def cleanup(path)
        puts "*** Cleaning stale instance directory #{path}"
        begin
          FileUtils.chmod_R(0700, path) rescue nil
          FileUtils.remove_entry_secure(path)
        rescue SystemCallError => e
          puts "    Warning: #{e}"
        end
      end
    end

  end # module AdminTools
end # module PhusionPassenger
