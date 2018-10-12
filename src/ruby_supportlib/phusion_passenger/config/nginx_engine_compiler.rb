#  encoding: utf-8
#
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
require 'fileutils'
require 'logger'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'abstract_installer'
PhusionPassenger.require_passenger_lib 'common_library'
PhusionPassenger.require_passenger_lib 'config/installation_utils'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/openssl'
PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'utils/shellwords'
PhusionPassenger.require_passenger_lib 'utils/progress_bar'
PhusionPassenger.require_passenger_lib 'utils/tmpio'

module PhusionPassenger
  module Config

    class NginxEngineCompiler < AbstractInstaller
      include InstallationUtils

      def self.configure_script_options
        extra_cflags = "-Wno-error #{PlatformInfo.openssl_extra_cflags}".strip
        result = "--with-cc-opt=#{Shellwords.escape extra_cflags} "

        extra_ldflags = PlatformInfo.openssl_extra_ldflags
        if !extra_ldflags.empty?
          result << "--with-ld-opt=#{Shellwords.escape extra_ldflags} "
        end

        result << "--without-http_fastcgi_module " \
          "--without-http_scgi_module " \
          "--without-http_uwsgi_module " \
          "--with-http_ssl_module " \
          "--with-http_v2_module " \
          "--with-http_realip_module " \
          "--with-http_gzip_static_module " \
          "--with-http_stub_status_module " \
          "--with-http_addition_module"

        result
      end

    protected
      def dependencies
        specs = [
          'depcheck_specs/compiler_toolchain',
          'depcheck_specs/ruby',
          'depcheck_specs/libs',
          'depcheck_specs/utilities'
        ]
        ids = [
          'cc',
          'c++',
          'gmake',
          'rake',
          'openssl-dev',
          'zlib-dev',
          'pcre-dev'
        ].compact
        return [specs, ids]
      end

      def install_doc_url
        "https://www.phusionpassenger.com/library/install/standalone/"
      end

      def troubleshooting_doc_url
        "https://www.phusionpassenger.com/library/admin/standalone/troubleshooting/"
      end

      def run_steps
        check_source_code_available!
        check_precompiled_support_libs_available!
        if !@force
          check_whether_os_is_broken
          check_whether_system_has_enough_ram
          check_for_download_tool!
        end
        check_dependencies(false) || abort
        puts

        @destdir = find_or_create_writable_support_binaries_dir!
        puts "<banner>Installing...</banner>"
        download_and_extract_nginx_sources
        determine_support_libraries
        if PhusionPassenger.build_system_dir
          compile_support_libraries
        end
        configure_and_compile_nginx
      end

      def before_install
        super
        if !@working_dir
          @working_dir = PhusionPassenger::Utils.mktmpdir("passenger-install.", PlatformInfo.tmpexedir)
          @owns_working_dir = true
        end
        @nginx_version ||= PREFERRED_NGINX_VERSION
      end

      def after_install
        super
        FileUtils.remove_entry_secure(@working_dir) if @owns_working_dir
      end

    private
      def check_source_code_available!
        return if File.exist?(PhusionPassenger.nginx_module_source_dir)

        if PhusionPassenger.originally_packaged?
          puts "<red>Broken #{PROGRAM_NAME} installation detected</red>"
          puts
          puts "This program requires the #{PROGRAM_NAME} Nginx module sources " +
            "before it can compile an Nginx engine. However, the #{PROGRAM_NAME} " +
            "Nginx module sources are not installed, even though they should have been. " +
            "This probably means that your #{PROGRAM_NAME} installation has somehow " +
            "become corrupted. Please re-install #{PROGRAM_NAME}."
          abort
        else
          case PhusionPassenger.packaging_method
          when "deb"
            command = "sudo sh -c 'apt-get update && apt-get install #{DEB_DEV_PACKAGE}'"
          when "rpm"
            command = "sudo yum install #{RPM_DEV_PACKAGE}-#{VERSION_STRING}"
          end

          if command
            if STDIN.tty?
              puts " --> Installing #{PROGRAM_NAME} Nginx module sources"
              puts "     Running: #{command}"
              if !system(command)
                puts "     <red>*** Command failed: #{command}</red>"
                abort
              end
            else
              puts " --> #{PROGRAM_NAME} Nginx module sources not installed"
              puts "     Please install them first: #{command}"
              abort
            end
          else
            puts " --> #{PROGRAM_NAME} Nginx module sources not installed"
            puts "     <red>Please ask your #{PROGRAM_NAME} packager or operating " +
              "system vendor how to install these.</red>"
            abort
          end
        end
      end

      def check_precompiled_support_libs_available!
        return if PhusionPassenger.build_system_dir ||
          File.exist?("#{PhusionPassenger.lib_dir}/common/libboost_oxt.a")

        if PhusionPassenger.originally_packaged?
          puts "<red>Broken #{PROGRAM_NAME} installation detected</red>"
          puts
          puts "This program requires the #{PROGRAM_NAME} support libraries " +
            "before it can compile an Nginx engine. However, the #{PROGRAM_NAME} " +
            "support libraries are not installed, even though they should have been. " +
            "This probably means that your #{PROGRAM_NAME} installation has somehow " +
            "become corrupted. Please re-install #{PROGRAM_NAME}."
          abort
        else
          case PhusionPassenger.packaging_method
          when "deb"
            command = "sudo sh -c 'apt-get update && apt-get install #{DEB_DEV_PACKAGE}'"
          when "rpm"
            command = "sudo yum install #{RPM_DEV_PACKAGE}-#{VERSION_STRING}"
          end

          if command
            if STDIN.tty?
              puts " --> Installing #{PROGRAM_NAME} support libraries"
              puts "     Running: #{command}"
              if !system(command)
                puts "     <red>*** Command failed: #{command}</red>"
                abort
              end
            else
              puts " --> #{PROGRAM_NAME} support libraries not installed"
              puts "     Please install them first: #{command}"
              abort
            end
          else
            puts " --> #{PROGRAM_NAME} support libraries not installed"
            puts "     <red>Please ask your #{PROGRAM_NAME} packager or operating " +
              "system vendor how to install these.</red>"
            abort
          end
        end
      end

      def download_and_extract_nginx_sources
        if @nginx_tarball
          tarball  = @nginx_tarball
        else
          puts "Downloading Nginx #{@nginx_version} source code..."
          basename = "nginx-#{@nginx_version}.tar.gz"
          tarball  = "#{@working_dir}/#{basename}"

          options = {
            :show_progress => @stdout.tty?
          }
          if @connect_timeout && @connect_timeout != 0
            options[:connect_timeout] = @connect_timeout
          end
          if @idle_timeout && @idle_timeout != 0
            options[:idle_timeout] = @idle_timeout
          end

          result = download("https://nginx.org/download/#{basename}",
            tarball, options)

          if !result
            puts
            show_possible_solutions_for_download_and_extraction_problems
            abort
          end
        end

        puts "Extracting tarball..."
        e_working_dir = Shellwords.escape(@working_dir)
        e_tarball = Shellwords.escape(tarball)
        result = system("cd #{e_working_dir} && tar xzf #{e_tarball}")
        if !result
          puts
          if @nginx_tarball
            new_screen
            puts "You specified --nginx-tarball, but the file could not be extracted. " +
              "Please check the path and format (tar.gz), and ensure Passenger can write to " +
              PlatformInfo.tmpexedir + "."
            puts
          else
            show_possible_solutions_for_download_and_extraction_problems
          end
          abort
        end
      end

      def show_possible_solutions_for_download_and_extraction_problems
        new_screen
        render_template "nginx_engine_compiler/possible_solutions_for_download_and_extraction_problems"
        puts
      end

      def determine_support_libraries
        if PhusionPassenger.build_system_dir
          lib_dir = "#{@working_dir}/common/libpassenger_common"
          @support_libs = COMMON_LIBRARY.only(*NGINX_LIBS_SELECTOR).
            set_output_dir(lib_dir).
            link_objects
          @support_libs << "#{@working_dir}/common/libboost_oxt.a"
        else
          @support_libs = COMMON_LIBRARY.only(*NGINX_LIBS_SELECTOR).
            set_output_dir("#{PhusionPassenger.lib_dir}/common/libpassenger_common").
            link_objects
          @support_libs << "#{PhusionPassenger.lib_dir}/common/libboost_oxt.a"
        end
        @support_libs_string = @support_libs.join(" ")
      end

      def compile_support_libraries
        puts "Compiling support libraries (step 1 of 2)..."
        progress_bar = ProgressBar.new
        e_working_dir = Shellwords.escape(@working_dir)
        args = "#{@support_libs_string} CACHING=false OUTPUT_DIR=#{e_working_dir}"
        begin
          progress_bar.set(0.05)
          Dir.chdir(PhusionPassenger.build_system_dir) do
            run_rake_task!(args) do |progress, total|
              progress_bar.set(0.05 + (progress / total.to_f) * 0.95)
            end
          end
          progress_bar.set(1)
        ensure
          progress_bar.finish
        end
      end

      def configure_and_compile_nginx
        puts "Compiling Nginx engine (step 2 of 2)..."
        progress_bar = ProgressBar.new
        progress_bar.set(0)
        begin
          configure_nginx do
            progress_bar.set(0.25)
          end
          compile_nginx do |progress|
            progress_bar.set(0.25 + progress * 0.75)
          end
          progress_bar.set(1)
        ensure
          progress_bar.finish
        end

        FileUtils.cp("#{@working_dir}/nginx-#{@nginx_version}/objs/nginx",
          "#{@destdir}/nginx-#{@nginx_version}")
        puts "<green>Compilation finished!</green>"
      end

      def configure_nginx
        shell = PlatformInfo.find_command('bash') || "sh"
        e_nginx_source_dir = Shellwords.escape("#{@working_dir}/nginx-#{@nginx_version}")

        command = "cd #{e_nginx_source_dir} && "
        command << "env PASSENGER_INCLUDEDIR=#{Shellwords.escape PhusionPassenger.include_dir} " <<
          "PASSENGER_LIBS=#{Shellwords.escape(@support_libs_string)} "
        # RPM thinks it's being smart by scanning binaries for
        # paths and refusing to create package if it detects any
        # hardcoded thats that point to /usr or other important
        # locations. For Phusion Passenger Standalone we do not
        # care at all what the Nginx configured prefix is because
        # we pass it its resource locations during runtime, so
        # work around the problem by configure Nginx with prefix
        # /tmp.
        command << "#{shell} ./configure --prefix=/tmp " +
          "#{self.class.configure_script_options} " +
          "--add-module=#{Shellwords.escape PhusionPassenger.nginx_module_source_dir}"
        run_command_yield_activity(command) do
          yield
        end
      end

      def compile_nginx
        backlog = ""
        e_nginx_source_dir = Shellwords.escape("#{@working_dir}/nginx-#{@nginx_version}")

        # Capture and index the `make --dry-run` output for
        # progress determination.
        total_lines = 0
        dry_run_output = {}
        `cd #{e_nginx_source_dir} && #{PlatformInfo.gnu_make} --dry-run`.split("\n").each do |line|
          total_lines += 1
          dry_run_output[line] = true
        end

        IO.popen("cd #{e_nginx_source_dir} && #{PlatformInfo.gnu_make} 2>&1", "r") do |io|
          progress = 1
          while !io.eof?
            line = io.readline
            backlog << line
            # If the output is part of what we saw when dry-running,
            # then increase progress bar. Otherwise it could be compiler
            # warnings or something, so ignore those.
            if dry_run_output[line.chomp]
              yield(progress / total_lines.to_f)
              progress += 1
            end
          end
        end
        if $?.exitstatus != 0
          @stderr.puts
          @stderr.puts "*** ERROR: unable to compile web helper."
          @stderr.puts backlog
          exit 1
        end
      end

      def run_command_yield_activity(command)
        backlog = ""
        IO.popen("#{command} 2>&1", "rb") do |io|
          while !io.eof?
            backlog << io.readline
            yield
          end
        end
        if $?.exitstatus != 0
          @stderr.puts
          @stderr.puts backlog
          @stderr.puts "*** ERROR: command failed: #{command}"
          exit 1
        end
      end
    end

  end # module Standalone
end # module PhusionPassenger
