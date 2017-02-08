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
PhusionPassenger.require_passenger_lib 'config/command'

module PhusionPassenger
  module Config

    class AboutCommand < Command
      def self.help
        puts "Usage: passenger-config about <SUBCOMMAND>"
        puts
        puts "  Show information about #{PROGRAM_NAME}."
        puts
        puts "Available subcommands:"
        puts "  root                     Show #{PROGRAM_NAME}'s root."
        puts "  ruby-libdir              Show #{PROGRAM_NAME}'s Ruby library directory."
        puts "  includedir               Show the Nginx runtime library headers directory."
        puts "  nginx-addon-dir          Show #{PROGRAM_NAME}'s Nginx addon directory."
        puts "  nginx-libs               Show paths to #{PROGRAM_NAME}'s libraries" 
        puts "                           for static linking to Nginx runtime."
        puts "  nginx-dynamic-libs       Show paths to #{PROGRAM_NAME}'s libraries" 
        puts "                           for dynamic linking to Nginx runtime."
        puts "  nginx-dynamic-compiled   Check whether runtime libraries for dynamic"
        puts "                           linking to Nginx are compiled."
        puts "  resourcesdir             Show #{PROGRAM_NAME}'s resources directory."
        puts "  support-binaries-dir     Show #{PROGRAM_NAME}'s support binaries dir."
        puts "  compiled                 Check whether runtime libraries are compiled."
        puts "  custom-packaged          Check whether Phusion Passenger is custom"
        puts "                           packaged."
        puts "  installed-from-release-package  Check whether this installation came from"
        puts "                                  an official release package."
        puts "  make-locations-ini       Generate a locations.ini based on the current"
        puts "                           install paths."
        puts "  detect-apache2           Autodetect Apache installations."
        puts "  ruby-command             Show the correct command for invoking the Ruby"
        puts "                           interpreter."
        puts "  rubyext-compat-id        Show the Ruby extension binary compatibility ID."
        puts "  cxx-compat-id            Show the C++ binary compatibility ID."
        puts "  version                  Show the version number."
      end

      def run
        PhusionPassenger.require_passenger_lib 'platform_info'
        PhusionPassenger.require_passenger_lib 'platform_info/compiler'

        subcommand = @argv[0].to_s.dup
        # Compatibility with version <= 4.0.29: accept both
        # 'subcommand' and '--subcommand'.
        subcommand = "--#{subcommand}" if subcommand !~ /^--/

        case subcommand
        when "--root"
          puts PhusionPassenger.install_spec
        when "--ruby-libdir"
          puts PhusionPassenger.ruby_libdir
        when "--includedir"
          puts PhusionPassenger.include_dir
        when "--nginx-addon-dir"
          puts PhusionPassenger.nginx_module_source_dir
        when "--nginx-libs"
          puts "#{common_library.link_objects_as_string} #{PhusionPassenger.lib_dir}/common/libboost_oxt.a"
        when "--nginx-dynamic-libs"
          puts "#{common_dynamic_library.link_objects_as_string} #{PhusionPassenger.lib_dir}/nginx_dynamic/libboost_oxt.a"
        when "--resourcesdir"
          puts PhusionPassenger.resources_dir
        when "--support-binaries-dir"
          puts PhusionPassenger.support_binaries_dir
        when "--compiled"
          common_library.link_objects.each do |filename|
            if !File.exist?(filename)
              exit 1
            end
          end
          if File.exist?("#{PhusionPassenger.lib_dir}/common/libboost_oxt.a")
            exit 0
          else
            exit 1
          end
        when "--nginx-dynamic-compiled"
          common_dynamic_library.link_objects.each do |filename|
            if !File.exist?(filename)
              exit 1
            end
          end
          if File.exist?("#{PhusionPassenger.lib_dir}/nginx_dynamic/libboost_oxt.a")
            exit 0
          else
            exit 1
          end
        when "--custom-packaged"
          if PhusionPassenger.custom_packaged?
            exit 0
          else
            exit 1
          end
        when "--installed-from-release-package"
          if PhusionPassenger.installed_from_release_package?
            exit 0
          else
            exit 1
          end
        when "--make-locations-ini"
          if @argv[1] =~ /^--for-(native-)?packaging-method=(.*)/
            packaging_method = $2
          else
            packaging_method = nil
          end

          puts "[locations]"
          if packaging_method
            puts "packaging_method=#{packaging_method}"
          else
            if PhusionPassenger.custom_packaged?
              puts "packaging_method=#{PhusionPassenger.packaging_method}"
            else
              puts "packaging_method=unknown"
            end
          end
          PhusionPassenger::REQUIRED_LOCATIONS_INI_FIELDS.each do |field|
            puts "#{field}=#{PhusionPassenger.send(field)}"
          end
          PhusionPassenger::OPTIONAL_LOCATIONS_INI_FIELDS.each do |field|
            value = PhusionPassenger.send(field)
            puts "#{field}=#{value}" if value
          end
        when "--detect-apache2"
          PhusionPassenger.require_passenger_lib 'platform_info/apache_detector'
          detector = PhusionPassenger::PlatformInfo::ApacheDetector.new(STDOUT)
          STDOUT.write(Utils::AnsiColors::DEFAULT_TERMINAL_COLOR)
          STDOUT.flush
          begin
            detector.detect_all
            detector.report
          ensure
            detector.finish
            STDOUT.write(Utils::AnsiColors::RESET)
            STDOUT.flush
          end
        when "--ruby-command"
          PhusionPassenger.require_passenger_lib 'platform_info/ruby'
          ruby = PhusionPassenger::PlatformInfo.ruby_command
          puts "passenger-config was invoked through the following Ruby interpreter:"
          puts "  Command: #{ruby}"
          STDOUT.write "  Version: "
          STDOUT.flush
          system("/bin/sh -c '#{ruby} -v'")
          puts "  To use in Apache: PassengerRuby #{ruby}"
          puts "  To use in Nginx : passenger_ruby #{ruby}"
          puts "  To use with Standalone: #{ruby} #{PhusionPassenger.bin_dir}/passenger start"
          puts

          ruby = PhusionPassenger::PlatformInfo.find_command('ruby')
          if ruby && !ruby.include?("rvm/rubies/")
            # If this is an RVM Ruby executable then we don't show it. We want people to
            # use the RVM wrapper scripts only.
            puts "The following Ruby interpreter was found first in $PATH:"
            puts "  Command: #{ruby}"
            STDOUT.write "  Version: "
            STDOUT.flush
            system("/bin/sh -c '#{ruby} -v'")
            puts "  To use in Apache: PassengerRuby #{ruby}"
            puts "  To use in Nginx : passenger_ruby #{ruby}"
            puts "  To use with Standalone: #{ruby} #{PhusionPassenger.bin_dir}/passenger start"
          elsif !ruby.include?("rvm/rubies/")
            puts "No Ruby interpreter found in $PATH."
          end
          puts
          puts "## Notes for RVM users"
          puts "Do you want to know which command to use for a different Ruby interpreter? 'rvm use' that Ruby interpreter, then re-run 'passenger-config about ruby-command'."
        when "--rubyext-compat-id"
          PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
          puts PhusionPassenger::PlatformInfo.ruby_extension_binary_compatibility_id
        when "--cxx-compat-id"
          PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
          puts PhusionPassenger::PlatformInfo.cxx_binary_compatibility_id
        when "--version"
          if defined?(PhusionPassenger::PASSENGER_IS_ENTERPRISE)
            name = "#{PhusionPassenger::PROGRAM_NAME} Enterprise"
          else
            name = PhusionPassenger::PROGRAM_NAME
          end
          puts "#{name} #{PhusionPassenger::VERSION_STRING}"
        when "--help"
          self.class.help
        else
          self.class.help
          exit 1
        end
      end

    private
      def common_library
        PhusionPassenger.require_passenger_lib 'common_library'
        return COMMON_LIBRARY.
          only(*NGINX_LIBS_SELECTOR).
          set_output_dir("#{PhusionPassenger.lib_dir}/common/libpassenger_common")
      end
      
      def common_dynamic_library
        PhusionPassenger.require_passenger_lib 'common_library'
        return COMMON_LIBRARY.
          only(*NGINX_LIBS_SELECTOR).
          set_output_dir("#{PhusionPassenger.lib_dir}/nginx_dynamic/module_libpassenger_common")
      end
    end

  end # module Config
end # module PhusionPassenger
