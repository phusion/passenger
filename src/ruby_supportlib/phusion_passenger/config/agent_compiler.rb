#  encoding: utf-8
#
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
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
PhusionPassenger.require_passenger_lib 'config/installation_utils'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'utils/shellwords'
PhusionPassenger.require_passenger_lib 'utils/progress_bar'
PhusionPassenger.require_passenger_lib 'utils/tmpio'

module PhusionPassenger
  module Config

    class AgentCompiler < AbstractInstaller
    protected
      def dependencies
        specs = [
          'depcheck_specs/compiler_toolchain',
          'depcheck_specs/ruby',
          'depcheck_specs/gems',
          'depcheck_specs/libs',
          'depcheck_specs/utilities'
        ]
        ids = [
          'cc',
          'c++',
          'rake',
          'libcurl-dev',
          'openssl-dev',
          'zlib-dev'
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
        if !@force
          check_whether_os_is_broken
          check_whether_system_has_enough_ram
          InstallationUtils.check_for_download_tool!
        end
        check_dependencies(false) || abort
        puts

        @destdir = InstallationUtils.find_or_create_writable_support_binaries_dir!
        confirm_enable_optimizations
        compile_agent
      end

      def before_install
        super
        if !@working_dir
          @working_dir = PhusionPassenger::Utils.mktmpdir("passenger-install.", PlatformInfo.tmpexedir)
          @owns_working_dir = true
        end
      end

      def after_install
        super
        FileUtils.remove_entry_secure(@working_dir) if @owns_working_dir
      end

    private
      def check_source_code_available!
        if PhusionPassenger.build_system_dir.nil?
          puts "<red>Cannot compile agent binary</red>"
          puts
          puts "This #{PROGRAM_NAME} installation does not " +
            "come with any source code, so the agent binary cannot " +
            "be compiled. So instead, please try to download a " +
            "precompiled agent binary from the #{PROGRAM_NAME} website, " +
            "by running:\n\n" +
            "  <b>passenger-config download-agent</b>"
          abort
        end
      end

      def confirm_enable_optimizations
        if @auto
          if @optimize
            puts "Compiling with optimizations."
          else
            puts "Not compiling with optimizations."
          end
        else
          if @optimize
            puts "Compiling with optimizations."
          else
            new_screen
            render_template 'config/agent_compiler/confirm_enable_optimizations',
              :total_ram => total_ram_gb
            puts
            @optimize = prompt_confirmation('Compile with optimizations?')
            puts
          end
        end
      end

      def compile_agent
        puts "<banner>Compiling #{PROGRAM_NAME} agent...</banner>"
        progress_bar = ProgressBar.new
        e_working_dir = Shellwords.escape(@working_dir)
        args = "#{e_working_dir}/support-binaries/#{AGENT_EXE}" \
          " CACHING=false" \
          " OUTPUT_DIR=#{e_working_dir} " \
          " OPTIMIZE=#{!!@optimize}"
        begin
          progress_bar.set(0)
          Dir.chdir(PhusionPassenger.build_system_dir) do
            InstallationUtils.run_rake_task!(args) do |progress, total|
              progress_bar.set(0.05 + (progress / total.to_f) * 0.95)
            end
          end
          progress_bar.set(1)
        ensure
          progress_bar.finish
        end

        FileUtils.cp("#{@working_dir}/support-binaries/#{AGENT_EXE}",
          "#{@destdir}/#{AGENT_EXE}")
        puts "<green>Compilation finished!</green>"
      end

      def total_ram_gb
        begin
          meminfo = File.read("/proc/meminfo")
          if meminfo =~ /^MemTotal: *(\d+) kB$/
            return sprintf("%.1f", $1.to_i / 1024 / 1024)
          end
        rescue Errno::ENOENT, Errno::EACCES
          # Don't do anything on systems without memory information.
          return nil
        end
      end
    end

  end # module Standalone
end # module PhusionPassenger
