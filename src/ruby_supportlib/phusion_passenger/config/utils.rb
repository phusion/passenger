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

PhusionPassenger.require_passenger_lib 'constants'

module PhusionPassenger
  module Config

    module Utils
      extend self    # Make methods available as class methods.

      def self.included(klass)
        # When included into another class, make sure that Utils
        # methods are made private.
        public_instance_methods(false).each do |method_name|
          klass.send(:private, method_name)
        end
      end

      def select_passenger_instance
        if name = @options[:instance]
          @instance = AdminTools::InstanceRegistry.new.find_by_name_prefix(name)
          if !@instance
            if @options[:ignore_passenger_not_running]
              message_type = "WARNING"
            else
              message_type = "ERROR"
            end
            STDERR.puts "*** #{message_type}: there doesn't seem to be a #{PROGRAM_NAME} instance running with the name '#{name}'."
            list_all_passenger_instances(AdminTools::InstanceRegistry.new.list)
            STDERR.puts
            STDERR.puts "Please pass `--instance <NAME>` to select a specific #{PROGRAM_NAME} instance."
            if @options[:ignore_passenger_not_running]
              exit
            else
              abort
            end
          elsif @instance == :ambigious
            abort "*** ERROR: there are multiple instances whose name start with '#{name}'. Please specify the full name."
          end
        else
          instances = AdminTools::InstanceRegistry.new.list
          if instances.empty?
            if @options[:ignore_passenger_not_running]
              message_type = "WARNING"
            else
              message_type = "ERROR"
            end
            STDERR.puts "*** #{message_type}: #{PROGRAM_NAME} doesn't seem to be running. If you are sure that it"
            STDERR.puts "is running, then the causes of this problem could be one of:"
            STDERR.puts
            STDERR.puts " 1. You customized the instance registry directory using Apache's"
            STDERR.puts "    PassengerInstanceRegistryDir option, Nginx's"
            STDERR.puts "    passenger_instance_registry_dir option, or #{PROGRAM_NAME} Standalone's"
            STDERR.puts "    --instance-registry-dir command line argument. If so, please set the"
            STDERR.puts "    environment variable PASSENGER_INSTANCE_REGISTRY_DIR to that directory"
            STDERR.puts "    and run this command again."
            STDERR.puts " 2. The instance directory has been removed by an operating system background"
            STDERR.puts "    service. Please set a different instance registry directory using Apache's"
            STDERR.puts "    PassengerInstanceRegistryDir option, Nginx's passenger_instance_registry_dir"
            STDERR.puts "    option, or #{PROGRAM_NAME} Standalone's --instance-registry-dir command"
            STDERR.puts "    line argument."
            if @options[:ignore_passenger_not_running]
              exit
            else
              abort
            end
          elsif instances.size == 1
            @instance = instances.first
          else
            complain_that_multiple_passenger_instances_are_running(instances)
            abort
          end
        end
      end

      def complain_that_multiple_passenger_instances_are_running(instances)
        puts "It appears that multiple #{PROGRAM_NAME} instances are running. Please select"
        puts "a specific one by passing:"
        puts
        puts "  --instance <NAME>"
        puts
        list_all_passenger_instances(instances)
        abort
      end

      def list_all_passenger_instances(instances, print_preamble = true)
        if print_preamble
          puts "The following #{PROGRAM_NAME} instances are running:"
          puts
        end
        printf "%-25s  %-7s  %s\n", "Name", "PID", "Description"
        puts "--------------------------------------------------------------------------"
        if instances.empty?
          printf "%-25s  %-7s  %s\n", "(list empty)", "-", "-"
        else
          instances.each do |instance|
            printf "%-25s  %-7s  %s\n", instance.name, instance.watchdog_pid, instance.server_software
          end
        end
      end

      def try_performing_ro_admin_basic_auth(request, instance)
        begin
          password = instance.read_only_admin_password
        rescue Errno::EACCES
          return
        end
        request.basic_auth("ro_admin", password)
      end

      def try_performing_full_admin_basic_auth(request, instance)
        begin
          password = instance.full_admin_password
        rescue Errno::EACCES
          return
        end
        request.basic_auth("admin", password)
      end


      def print_instance_querying_permission_error
        PhusionPassenger.require_passenger_lib 'platform_info/ruby'
        STDERR.puts "*** ERROR: You are not authorized to query the status for this " +
          "#{PROGRAM_NAME} instance. Please try again with " +
          "'#{PhusionPassenger::PlatformInfo.ruby_sudo_command}'."
      end

      def print_full_admin_command_permission_error
        PhusionPassenger.require_passenger_lib 'platform_info/ruby'
        STDERR.puts "*** ERROR: You are not authorized to perform this particular " +
          "administration command on this #{PROGRAM_NAME} instance. Please try again with " +
          "'#{PhusionPassenger::PlatformInfo.ruby_sudo_command}'."
      end

      def is_enterprise?
        return defined?(PhusionPassenger::PASSENGER_IS_ENTERPRISE) && PhusionPassenger::PASSENGER_IS_ENTERPRISE
      end
    end

  end # module Config
end # module PhusionPassenger
