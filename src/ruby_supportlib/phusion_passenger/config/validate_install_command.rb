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

require 'optparse'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'config/command'

module PhusionPassenger
  module Config

    class ValidateInstallCommand < Command
      # Signifies that there is at least 1 error.
      FAIL_EXIT_CODE = 1
      # Signifies that there are no error, but at least 1 warning.
      WARN_EXIT_CODE = 2
      # Internal error occurred.
      INTERNAL_ERROR_CODE = 9

      def run
        @orig_argv = @argv.dup
        parse_options
        prepare
        begin
          if !@options[:auto] && !@options[:invoked_from_installer]
            ask_what_to_validate
          end
          if @options[:validate_apache2]
            initialize_apache_envvars
            if !@options[:auto] && !@options[:invoked_from_installer]
              check_whether_there_are_multiple_apache_installs
            end
          end

          if @options[:validate_passenger]
            check_tools_in_path
            check_no_other_installs_in_path
          end
          if @options[:validate_apache2]
            if check_apache2_installed
              check_apache2_load_module_config
            end
          end

          if @options[:summary]
            summarize
          end
          exit(FAIL_EXIT_CODE) if @error_count > 0
          exit(WARN_EXIT_CODE) if @warning_count > 0
        ensure
          reset_terminal
        end
      end

    private
      def self.create_default_options
        return {
          :auto => !STDIN.tty?,
          :validate_passenger => true,
          :colors => STDOUT.tty?,
          :summary => true
        }
      end

      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config validate-install [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Validate this #{SHORT_PROGRAM_NAME} installation and/or its integration with web servers."
          opts.separator "  If you run this command in a terminal, it will run interactively and ask you questions."
          opts.separator "  You can run this command non-interactively either by running it without a terminal, "
          opts.separator "  or by passing --auto."
          opts.separator ""
          opts.separator "  When running non-interactively, the default is to validate the #{SHORT_PROGRAM_NAME}"
          opts.separator "  installation only (so e.g. Apache is not validated). You can customize this"
          opts.separator "  using the appropriate command line options."
          opts.separator ""
          opts.separator "  The exit codes are as follows:"
          opts.separator "    0 - All checks passed. No errors, no warnings."
          opts.separator "    #{FAIL_EXIT_CODE} - Some checks failed with an error."
          opts.separator "    #{WARN_EXIT_CODE} - No checks failed with an error, but some produced warnings."
          opts.separator "    #{INTERNAL_ERROR_CODE} - Some internal error occurred."
          opts.separator ""

          opts.separator "Options:"
          opts.on("--auto", "Run non-interactively") do
            options[:auto] = true
          end
          opts.on("--no-validate-passenger", "Do not validate the #{SHORT_PROGRAM_NAME} installation#{nl}" +
            "itself") do
            options[:validate_passenger] = false
          end
          opts.on("--validate-apache2", "Validate Apache 2 integration") do
            options[:validate_apache2] = true
          end
          opts.on("--apxs2-path=PATH", String, "Apache installation to validate") do |val|
            ENV['APXS2'] = val
          end
          opts.separator ""
          opts.on("--no-colors", "Never output colors") do
            options[:colors] = false
          end
          opts.on("--no-summary", "Do not display a summary") do
            options[:summary] = false
          end
          opts.separator ""
          opts.on("-h", "--help", "Show this help") do
            options[:help] = true
          end

          opts.separator ""
          opts.separator "Internal options:"
          opts.on("--invoked-from-installer", "Indicate that this program is invoked from#{nl}" +
            "passenger-install-apache2-module") do
            options[:invoked_from_installer] = true
          end
        end
      end

      def prepare
        begin
          require 'rubygems'
        rescue LoadError
        end
        PhusionPassenger.require_passenger_lib 'utils/ansi_colors'
        PhusionPassenger.require_passenger_lib 'utils/terminal_choice_menu'
        PhusionPassenger.require_passenger_lib 'platform_info'
        PhusionPassenger.require_passenger_lib 'platform_info/ruby'
        PhusionPassenger.require_passenger_lib 'platform_info/apache'
        PhusionPassenger.require_passenger_lib 'platform_info/apache_detector'
        PhusionPassenger.require_passenger_lib 'platform_info/depcheck'
        require 'stringio'
        require 'pathname'

        @error_count = 0
        @warning_count = 0

        @colors = Utils::AnsiColors.new(@options[:colors])

        prepare_terminal
      end

      def prepare_terminal
        STDOUT.write(@colors.default_terminal_color)
        STDOUT.flush
      end

      def reset_terminal
        STDOUT.write(@colors.reset)
        STDOUT.flush
      end

      def ask_what_to_validate
        log "<banner>What would you like to validate?</banner>"
        log "Use <space> to select."
        log "<dgray>If the menu doesn't display correctly, press '!'</dgray>"
        puts

        menu = Utils::TerminalChoiceMenu.new([
          "#{SHORT_PROGRAM_NAME} itself",
          "Apache"
        ])
        menu["#{SHORT_PROGRAM_NAME} itself"].checked = @options[:validate_passenger]
        menu["Apache"].checked = @options[:validate_apache2]

        begin
          menu.query
        rescue Interrupt
          exit(INTERNAL_ERROR_CODE)
        end
        display_separator

        @options[:validate_passenger] = menu.selected_choices.include?("#{SHORT_PROGRAM_NAME} itself")
        @options[:validate_apache2] = menu.selected_choices.include?("Apache")
      end

      def initialize_apache_envvars
        # The Apache executable may be located in an 'sbin' folder. We add
        # the 'sbin' folders to $PATH just in case. On some systems
        # 'sbin' isn't in $PATH unless the user is logged in as root from
        # the start (i.e. not via 'su' or 'sudo').
        ENV["PATH"] += ":/usr/sbin:/sbin:/usr/local/sbin"
      end

      def check_tools_in_path
        checking "whether this #{SHORT_PROGRAM_NAME} install is in PATH"
        paths = ENV['PATH'].to_s.split(':')
        if paths.include?(gem_bindir) ||
           paths.include?(homebrew_bindir) ||
           paths.include?(PhusionPassenger.bin_dir)
          check_ok
        else
          check_warning
          suggest %Q{
            Please add #{PhusionPassenger.bin_dir} to PATH.
            Otherwise you will get "command not found" errors upon running
            any Passenger commands.

            Learn more at about PATH at:

              https://www.phusionpassenger.com/library/indepth/environment_variables.html#the-path-environment-variable
          }
        end
      end

      def check_no_other_installs_in_path
        checking "whether there are no other #{SHORT_PROGRAM_NAME} installations"

        paths = ENV['PATH'].to_s.split(':')
        if Process.uid == 0 &&
           (sudo_user = ENV['SUDO_USER']) &&
           (bash = PlatformInfo.find_command("bash")) &&
           PlatformInfo.find_command("sudo")
          # If we were invoked through sudo then we need to check the original user's PATH too.
          output = `sudo -u #{sudo_user} #{bash} -lc 'echo; echo PATH FOLLOWS; echo "$PATH"' 2>&1`
          output.sub!(/.*\nPATH FOLLOWS\n/m, '')
          output.strip!
          paths.concat(output.split(':'))
        end

        # These may not be in PATH if the user did not run this command through sudo.
        paths << "/usr/bin"
        paths << "/usr/sbin"
        # Some of the paths may be symlinks, so we take the realpaths when
        # possible and remove duplicates. This is especially important on
        # Red Hat 7, where /bin is a symlink to /usr/bin.
        paths.map! do |path|
          try_realpath(path)
        end

        paths.delete(try_realpath(gem_bindir))
        paths.delete(try_realpath(homebrew_bindir))
        paths.delete(try_realpath(rbenv_shims_dir))
        paths.delete(try_realpath(PhusionPassenger.bin_dir))
        paths.uniq!

        other_installs = []
        paths.each do |path|
          filename = "#{path}/passenger"
          if File.exist?(filename)
            other_installs << filename
          end
        end
        if other_installs.empty?
          check_ok
        else
          check_warning
          suggest %Q{
            You are currently validating against #{PROGRAM_NAME} #{VERSION_STRING}, located in:

              #{PhusionPassenger.bin_dir}/passenger

            Besides this #{SHORT_PROGRAM_NAME} installation, the following other
            #{SHORT_PROGRAM_NAME} installations have also been detected:

              #{other_installs.join("\n              ")}

            Please uninstall these other #{SHORT_PROGRAM_NAME} installations to avoid
            confusion or conflicts.
          }
        end
      end

      def check_whether_there_are_multiple_apache_installs
        if PlatformInfo.httpd.nil? || PlatformInfo.apxs2.nil?
          # check_apache2_installed will handle this.
          return
        end

        log '<banner>Checking whether there are multiple Apache installations...</banner>'

        output = StringIO.new
        detector = PlatformInfo::ApacheDetector.new(output)
        begin
          detector.detect_all
          detector.report
          apache2 = detector.result_for(PlatformInfo.apxs2)

          if apache2.nil?
            # Print an extra newline because the autodetection routines
            # may have run some commands which printed stuff to stderr.
            puts

            if Process.uid == 0
              # More information will be displayed in #check_no_duplicate_apache2_load_module_config,
              # which should also fail.
              log "<red>Your Apache installation appears to be broken. More information will be displayed later.</red>"
            else
              whoami = `whoami`.strip
              sudo = PhusionPassenger::PlatformInfo.ruby_sudo_command
              selfcommand = "#{PhusionPassenger.bin_dir}/passenger-config validate-install #{@orig_argv.join(' ')}"

              log "<red>Permission problems</red>"
              log "This program must be able to analyze your Apache installation. But it can't"
              log "do that, because you're running the installer as <b>#{whoami}</b>."
              log "Please give this program root privileges, by re-running it with <yellow>#{sudo}</yellow>:"
              log ""
              log "  <b>export ORIG_PATH=\"$PATH\"</b>"
              log "  <b>#{sudo_s_e}</b>"
              log "  <b>export PATH=\"$ORIG_PATH\"</b>"
              log "  <b>#{ruby_command} #{selfcommand}</b>"
              exit(INTERNAL_ERROR_CODE)
            end
          elsif detector.results.size > 1
            other_installs = detector.results - [apache2]

            log "<yellow>Multiple Apache installations detected!</yellow>"
            log ""
            log "You are about to validate #{SHORT_PROGRAM_NAME} against the following"
            log "Apache installation:"
            log ""
            log "  <b>Apache #{apache2.version}</b>"
            log "  <b>apxs2     : #{apache2.apxs2}</b>"
            log "  <b>Executable: #{apache2.httpd}</b>"
            log ""
            log "However, #{other_installs.size} other Apache installation(s) have been found on your system:"
            log ""
            other_installs.each do |result|
              log "  <b>Apache #{result.version}</b>"
              log "  <b>apxs2     : #{result.apxs2}</b>"
              log "  <b>Executable: #{result.httpd}</b>"
              log ""
            end

            result = prompt_confirmation "Are you sure you want to validate " +
              "against Apache #{apache2.version} (#{apache2.apxs2})?"
            if !result
              puts
              display_separator
              other_installs.each do |result|
                log " * To validate against <yellow>Apache #{result.version} (#{result.apxs2})</yellow>:"
                log "   Re-run this program with: <b>--apxs2-path '#{result.apxs2}'</b>"
              end
              log ""
              log "You may also want to read the \"Installation\" section of Passenger Library"
              log "installation troubleshooting:"
              log ""
              log "  https://www.phusionpassenger.com/library/install/apache/"
              log ""
              log "If you keep having problems installing, please visit the following website for"
              log "support:"
              log ""
              log "  #{SUPPORT_URL}"
              exit(INTERNAL_ERROR_CODE)
            end
          else
            log '<green>Only a single installation detected. This is good.</green>'
          end

          display_separator
        ensure
          detector.finish
        end
      end

      def check_apache2_installed
        checking "whether Apache is installed"

        if PlatformInfo.httpd
          # macOS >= 10.13 High Sierra no longer includes apxs2, but that's
          # okay because we know Apache is installed
          if PlatformInfo.apxs2 ||
            (PlatformInfo.os_name_simple == 'macosx' &&
             PlatformInfo.os_version >= '10.13' &&
             PlatformInfo.httpd == '/usr/sbin/httpd')
            check_ok
            true
          else
            check_error

            PlatformInfo::Depcheck.load("depcheck_specs/apache2")
            dep = PlatformInfo::Depcheck.find("apache2-dev")
            install_instructions = dep.install_instructions.split("\n").join("\n              ")

            if !@options[:invoked_from_installer]
              next_step = "When done, please re-run this program."
            end

            suggest %Q{
              Unable to validate your Apache installation: more software required

              This program requires the <b>apxs2</b> tool in order to be able to validate your
              Apache installation. This tool is currently not installed. You can solve this
              as follows:

              #{install_instructions}

              #{next_step}
            }

            false
          end
        else
          check_error

          PlatformInfo::Depcheck.load("depcheck_specs/apache2")
          dep = PlatformInfo::Depcheck.find("apache2")
          install_instructions = dep.install_instructions.split("\n").join("\n            ")

          suggest %Q{
            Apache is not installed. You can solve this as follows:

            #{install_instructions}
          }

          false
        end
      end

      def check_apache2_load_module_config
        checking "whether the Passenger module is correctly configured in Apache"

        if PlatformInfo.httpd_default_config_file.nil?
          check_error
          passenger_config = "#{PhusionPassenger.bin_dir}/passenger-config"
          suggest %Q{
            Your Apache installation might be broken

            You are about to validate #{PROGRAM_NAME} against the following
            Apache installation:

               apxs2     : #{PlatformInfo.apxs2 || 'OS-provided installation'}
               Executable: #{PlatformInfo.httpd || 'unknown'}
               Version   : #{PlatformInfo.httpd_version || 'unknown'}

            However, this Apache installation appears to be broken, so this program
            cannot continue. To find out why this program thinks the above Apache
            installation is broken, run:

               export ORIG_PATH="$PATH"
               #{sudo_s_e}
               export PATH="$ORIG_PATH"
               #{ruby_command} #{passenger_config} --detect-apache2
          }
          return
        end

        result = PlatformInfo.httpd_included_config_files(
          PlatformInfo.httpd_default_config_file)
        if !result[:unreadable_files].empty?
          check_error

          if Process.uid == 0
            suggest %Q{
              Permission problems

              This program must be able to analyze your Apache installation. But it can't
              do that despite running with root privileges. In particular, it failed to
              read the following files:

                #{result[:unreadable_files].join("\n                ")}

              This program doesn't know why this error occurred. Your system is probably
              secured using some mechanism that this program is not familiar with. Please
              consult your operating system's manual to learn which security mechanisms
              may be preventing this program from accessing the above files. On Linux
              systems, SELinux and AppArmor might be responsible.

              When you've solved the problem, please re-run this program.
            }
          else
            whoami = `whoami`.strip
            sudo = PhusionPassenger::PlatformInfo.ruby_sudo_command
            selfcommand = "#{PhusionPassenger.bin_dir}/passenger-config validate-install #{@orig_argv.join(' ')}"

            suggest %Q{
              Permission problems

              This program must be able to analyze your Apache installation. But it can't
              do that, because you're running the installer as '#{whoami}'. In particular,
              it failed to read the following files:

                #{result[:unreadable_files].join("\n                ")}

              Please give this program root privileges, by re-running it with '#{sudo}':

                 export ORIG_PATH=\"$PATH\"
                 #{sudo_s_e}
                 export PATH=\"$ORIG_PATH\"
                 #{ruby_command} #{selfcommand}
            }
          end
          return
        end

        occurrences = 0
        occurrence_files = []
        module_path = nil

        result[:files].each do |path|
          lines = File.open(path, "rb") do |f|
            f.read.split("\n")
          end
          lines.each do |line|
            # Get rid of trailing CR
            line = line.strip

            if line !~ /^[\s\t]*#/ && line =~ /LoadModule[\s\t]+passenger_module[\s\t]+(.*)/
              module_path = $1
              occurrences += 1
              occurrence_files << path
            end
          end
        end

        if occurrences == 1
          if module_path !~ /^\//
            # Non-absolute path. Absolutize using ServerRoot.
            module_path = "#{PlatformInfo.httpd_default_root}/#{module_path}"
          end
          # Resolve symlinks.
          module_path = try_realpath(module_path)
          expected_module_path = try_realpath(PhusionPassenger.apache2_module_path)

          if module_path == expected_module_path
            check_ok
          else
            check_error
            suggest %Q{
              Incorrect #{SHORT_PROGRAM_NAME} module path detected

              #{PROGRAM_NAME} for Apache requires a 'LoadModule passenger_module'
              directive inside an Apache configuration file. This directive has been
              detected in the following config file:

                 #{occurrence_files[0]}

              However, the directive refers to the following Apache module, which is wrong:

                 #{module_path}

              Please edit the config file and change the directive to this instead:

                 LoadModule passenger_module #{PhusionPassenger.apache2_module_path}
            }
          end
        elsif occurrences == 0
          if @options[:invoked_from_installer]
            check_warning
            suggest %Q{
              You did not specify 'LoadModule passenger_module' in any of your Apache
              configuration files. Please paste the configuration snippet that this
              installer printed earlier, into one of your Apache configuration files, such
              as #{PlatformInfo.httpd_default_config_file}.
            }
          else
            check_error
            installer_command = "#{PhusionPassenger.bin_dir}/passenger-install-apache2-module"
            suggest %Q{
              You did not specify 'LoadModule passenger_module' in any of your
              Apache configuration files. This means that #{PROGRAM_NAME}
              for Apache is not installed or not active. Please run the
              #{PROGRAM_NAME} Apache module installer:

                 #{ruby_command} #{installer_command} --apxs2=#{PlatformInfo.apxs2 || 'none'}
            }
          end
        else
          check_error

          suggest %Q{
            You have #{occurrences} 'LoadModule passenger_module' directives in your Apache
            configuration files. However, you are only supposed to have one such
            directive.

            Please fix this by removing all 'LoadModule passenger_module' directives
            besides the one for #{SHORT_PROGRAM_NAME} version #{VERSION_STRING}. The directives were
            found in these files:

               #{occurrence_files.uniq.join("\n               ")}

            Note: 'LoadModule passenger_module' may be placed inside the global context
            only (so not within a VirtualHost).
          }
        end
      end

      def summarize
        puts
        if @error_count == 0 && @warning_count == 0
          log "<green>Everything looks good. :-)</green>"
        elsif @error_count == 0
          log "<yellow>Detected 0 error(s), #{@warning_count} warning(s).</yellow>"
        else
          log "<red>Detected #{@error_count} error(s), #{@warning_count} warning(s).</red>"
        end
      end

      # Returns the RubyGems bin dir, if Phusion Passenger is installed through RubyGems.
      def gem_bindir
        if defined?(Gem) &&
           PhusionPassenger.originally_packaged? &&
           PhusionPassenger.build_system_dir =~ /^#{Regexp.escape Gem.dir}\// &&
           File.exist?("#{Gem.bindir}/passenger-config")
          return Gem.bindir
        else
          return nil
        end
      end

      # Returns the Homebrew bin dir, if Phusion Passenger is installed through Homebrew.
      def homebrew_bindir
        if PhusionPassenger.packaging_method == "homebrew"
          return "/usr/local/bin"
        else
          return nil
        end
      end

      # Returns the ~/.rbenv/shims directory if it exists.
      def rbenv_shims_dir
        home = PhusionPassenger.home_dir
        "#{home}/.rbenv/shims"
      end

      def logn(message)
        STDOUT.write(@colors.ansi_colorize(message))
        STDOUT.flush
      end

      def log(message)
        STDOUT.puts(@colors.ansi_colorize(message))
      end

      def display_separator
        puts
        puts "-------------------------------------------------------------------------"
        puts
      end

      def checking(message)
        logn " * Checking #{message}... "
      end

      def check_ok(message = "✓")
        log "<green>#{message}</green>"
      end

      def check_error(message = "✗")
        log "<red>#{message}</red>"
        @error_count += 1
      end

      def check_warning(message = "(!)")
        log "<yellow>#{message}</yellow>"
        @warning_count += 1
      end

      def suggest(message)
        puts
        log reindent(unindent(message), 3)
        puts
      end

      def prompt_confirmation(message)
        result = prompt("#{message} [y/n]") do |value|
          if value.downcase == 'y' || value.downcase == 'n'
            true
          else
            log "<red>Invalid input '#{value}'; please enter either 'y' or 'n'.</red>"
            false
          end
        end
        result.downcase == 'y'
      rescue Interrupt
        exit(INTERNAL_ERROR_CODE)
      end

      def prompt(message, default_value = nil)
        done = false
        while !done
          logn "#{message}: "

          if default_value
            puts default_value
            return default_value
          end

          begin
            result = STDIN.readline
          rescue EOFError
            exit(INTERNAL_ERROR_CODE)
          end
          result.strip!
          if result.empty?
            if default_value
              result = default_value
              done = true
            else
              done = !block_given? || yield(result)
            end
          else
            done = !block_given? || yield(result)
          end
        end
        result
      rescue Interrupt
        exit(INTERNAL_ERROR_CODE)
      end

      def unindent(text)
        return PlatformInfo.send(:unindent, text)
      end

      def reindent(text, level)
        return PlatformInfo.send(:reindent, text, level)
      end

      def sudo_s_e
        PlatformInfo.ruby_sudo_shell_command("-E")
      end

      def ruby_command
        PlatformInfo.ruby_command
      end

      def try_realpath(path)
        if path
          begin
            Pathname.new(path).realpath.to_s
          rescue Errno::ENOENT, Errno::EACCES, Errno::ENOTDIR
            path
          end
        else
          nil
        end
      end
    end

  end # module Config
end # module PhusionPassenger
