# encoding: utf-8
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/linux'
PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'platform_info/openssl'
PhusionPassenger.require_passenger_lib 'platform_info/curl'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'

module PhusionPassenger
  module PlatformInfo

    # Almost all software require other software in order to run. We call those
    # other software 'dependencies'. Reliably checking for dependencies can be
    # difficult. Helping the user in case a dependency is not installed (or
    # doesn't seem to be installed) is more difficult still.
    #
    # The Depcheck framework seeks to make all this easier. It allows the programmer
    # to write "specs" which contain dependency checking code in a structured way.
    # The programmer defines a dependency's basic information (name, website, etc),
    # defines installation instructions (which may be customized per platform) and
    # defines code for checking whether the dependency actually exists. The Depcheck
    # framework:
    #
    #  * Provides helpers for checking for the existance of commands, libraries,
    #    headers, etc.
    #  * Registers all dependency specs in a way that can be easily accessed
    #    structurally.
    #  * Allows user-friendly display of dependency checking progress and user help
    #    instructions.
    #
    # Most dependency checking code (e.g. autoconf) is very straightforward: they
    # just check for the existance of a command, library, header, etc and either
    # report "found" or "not found". In our experience the world is unfortunately
    # not that simple. Users can have multiple versions of a dependency installed,
    # where some dependencies are suitable while others are not. Therefore specs
    # should print as many details about the dependency as possible (location, version,
    # etc) so that the user can override any decisions if necessary.
    module Depcheck
      THIS_DIR   = File.expand_path(File.dirname(__FILE__))
      @@loaded   = {}
      @@database = {}

      def self.load(partial_filename)
        if !@@loaded[partial_filename]
          filename = "#{THIS_DIR}/#{partial_filename}.rb"
          content = File.read(filename)
          instance_eval(content, filename)
          @@loaded[partial_filename] = true
        end
      end

      def self.define(identifier, &block)
        @@database[identifier.to_s] = block
      end

      def self.find(identifier)
        # We lazy-initialize everything in order to save resources. This also
        # allows blocks to perform relatively expensive checks without hindering
        # startup time.
        identifier = identifier.to_s
        result = @@database[identifier]
        if result.is_a?(Proc)
          result = Dependency.new(&result)
          @@database[identifier] = result
        end
        result
      end

      class Dependency
        def initialize(&block)
          instance_eval(&block)
          check_syntax_aspect("Name must be given") { !!@name }
          check_syntax_aspect("A checker must be given") { !!@checker }
        end

        def check
          @install_comments = nil
          @check_result ||= @checker.call
        end

        ### DSL for specs ###

        def name(value = nil)
          value ? @name = value : @name
        end

        def website(value = nil)
          value ? @website = value : @website
        end

        def website_comments(value = nil)
          value ? @website_comments = value : @website_comments
        end

        def install_instructions(value = nil)
          if value
            @install_instructions = value
          else
            if @install_instructions
              @install_instructions
            elsif @website
              result = "Please download it from <b>#{@website}</b>"
              result << "\n(#{@website_comments})" if @website_comments
              result
            else
              "Search Google for '#{@name}'."
            end
          end
        end

        def append_install_instructions(value)
            @install_instructions << "\n#{value}" if value
        end

        def install_comments(value = nil)
          value ? @install_comments = value : @install_comments
        end

      private
        def check_syntax_aspect(description)
          if !yield
            raise description
          end
        end

        ### DSL for specs ###

        def define_checker(&block)
          @checker = block
        end

        def check_for_command(name, *args)
          result = find_command(name, *args)
          if result
            { :found => true,
              "Location" => result }
          else
            false
          end
        end

        def check_for_ruby_tool(name)
          result = locate_ruby_tool(name)
          if result
            { :found => true,
              "Location" => result }
          else
            false
          end
        end

        def check_for_header(header_name, language = :c, flags = nil)
          if result = PlatformInfo.find_header(header_name, language, flags)
            { :found => true,
              "Location" => result }
          else
            false
          end
        end

        # def check_for_library(name)
        #   check_by_compiling("int main() { return 0; }", :cxx, nil, "-l#{name}")
        # end

        # def check_by_compiling(source, language = :c, cflags = nil, linkflags = nil)
        #   case language
        #   when :c
        #     source_file   = "#{PlatformInfo.tmpexedir}/depcheck-#{Process.pid}-#{Thread.current.object_id}.c"
        #     compiler       = "gcc"
        #     compiler_flags = ENV['CFLAGS']
        #   when :cxx
        #     source_file   = "#{PlatformInfo.tmpexedir}/depcheck-#{Process.pid}-#{Thread.current.object_id}.cpp"
        #     compiler       = "g++"
        #     compiler_flags = "#{ENV['CFLAGS']} #{ENV['CXXFLAGS']}".strip
        #   else
        #     raise ArgumentError, "Unknown language '#{language}"
        #   end

        #   output_file = "#{PlatformInfo.tmpexedir}/depcheck-#{Process.pid}-#{Thread.current.object_id}"

        #   begin
        #     File.open(source_file, 'w') do |f|
        #       f.puts(source)
        #     end

        #     if find_command(compiler)
        #       command = "#{compiler} #{compiler_flags} #{cflags} " +
        #         "#{source_file} -o #{output_file} #{linkflags}"
        #       [!!system(command)]
        #     else
        #       [:unknown, "Cannot check: compiler '#{compiler}' not found."]
        #     end
        #   ensure
        #     File.unlink(source_file) rescue nil
        #     File.unlink(output_file) rescue nil
        #   end
        # end

        def check_for_ruby_library(name)
          begin
            require(name)
            { :found => true }
          rescue LoadError
            if defined?(Gem)
              false
            else
              begin
                require 'rubygems'
                require(name)
                { :found => true }
              rescue LoadError
                false
              end
            end
          end
        end

        def on(platform)
          return if @on_invoked
          invoke = false
          if (linux_distro_tags || []).include?(platform)
            invoke = true
          else
            case platform
            when :linux
              invoke = true if PlatformInfo.os_name_simple == "linux"
            when :freebsd
              invoke = true if PlatformInfo.os_name_simple == "freebsd"
            when :macosx
              invoke = true if PlatformInfo.os_name_simple == "macosx"
            when :solaris
              invoke = true if PlatformInfo.os_name_simple == "solaris"
            when :other_platforms
              invoke = true
            end
          end
          if invoke
            yield
            @on_invoked = true
          end
        end

        def apt_get_install(package_name)
          install_instructions("Please install it with <b>apt-get install #{package_name}</b>")
        end

        def urpmi(package_name)
          install_instructions("Please install it with <b>urpmi #{package_name}</b>")
        end

        def yum_install(package_name, options = {})
          if options[:epel]
            install_instructions("Please enable <b>EPEL</b>, then install with <b>yum install #{package_name}</b>")
          else
            install_instructions("Please install it with <b>yum install #{package_name}</b>")
          end
        end

        def emerge(package_name)
          install_instructions("Please install it with <b>emerge -av #{package_name}</b>")
        end

        def gem_install(package_name)
          install_instructions("Please make sure RubyGems is installed, then run " +
            "<b>#{gem_command} install #{package_name}</b>")
        end

        def brew_install(package_name)
          install_instructions("Please install it with <b>brew install #{package_name}</b>")
        end

        def brew_link(package_name)
          append_install_instructions("Please link it with <b>brew link --force #{package_name}</b>")
        end

        def install_osx_command_line_tools
          PhusionPassenger.require_passenger_lib 'platform_info/compiler'
          if PlatformInfo.xcode_select_version.to_s >= "2333"
            install_instructions "Please install the Xcode command line tools: " +
              "<b>sudo xcode-select --install</b>"
          else
            install_instructions "Please install Xcode, then install the command line tools " +
              "though the menu <b>Xcode -> Preferences -> Downloads -> Components</b>"
          end
        end


        def ruby_command
          PlatformInfo.ruby_command
        end

        def gem_command
          PlatformInfo.gem_command(:sudo => true) || 'gem'
        end

        def find_command(command, *args)
          PlatformInfo.find_command(command, *args)
        end

        def linux_distro_tags
          PlatformInfo.linux_distro_tags
        end

        def locate_ruby_tool(name)
          PlatformInfo.locate_ruby_tool(name)
        end
      end # class Dependency

      class ConsoleRunner
        attr_reader :missing_dependencies

        def initialize(colors)
          @colors = colors || Utils::AnsiColors.new(:auto)
          @stdout = STDOUT
          @dep_identifiers = []
        end

        def add(identifier)
          @dep_identifiers << identifier
        end

        def check_all
          old_log_impl = PlatformInfo.log_implementation
          begin
            PlatformInfo.log_implementation = lambda do |message|
              message = PlatformInfo.send(:reindent, message, 10)
              message.sub!(/^          /, '')
              STDOUT.puts "       -> #{message}"
            end
            @missing_dependencies = []
            @dep_identifiers.each do |identifier|
              dep = Depcheck.find(identifier)
              raise "Cannot find depcheck spec #{identifier.inspect}" if !dep
              puts_header "Checking for #{dep.name}..."
              result = dep.check
              result = { :found => false } if !result

              if result[:found] && !result[:error]
                puts_detail "Found: <green>yes</green>"
              else
                if result[:error]
                  puts_detail "Found: #{result[:found] ? "<yellow>yes, but there was an error</yellow>" : "<red>no</red>"}"
                  puts_detail "Error: <red>#{result[:error]}</red>"
                else
                  puts_detail "Found: #{result[:found] ? "<green>yes</green>" : "<red>no</red>"}"
                end
                @missing_dependencies << dep
              end

              result.each_pair do |key, value|
                if key.is_a?(String)
                  puts_detail "#{key}: #{value}"
                end
              end
            end

            return @missing_dependencies.empty?
          ensure
            PlatformInfo.log_implementation = old_log_impl
          end
        end

        def print_installation_instructions_for_missing_dependencies
          @missing_dependencies.each do |dep|
            puts " * To install <yellow>#{dep.name}</yellow>:"
            puts "   #{dep.install_instructions}"
            if dep.install_comments
              puts "   #{dep.install_comments}"
            end
            puts
          end
        end

      private
        def puts(text = nil)
          if text
            @stdout.puts(@colors.ansi_colorize(text))
          else
            @stdout.puts
          end
          @stdout.flush
        end

        def puts_header(text)
          puts " <b>* #{text}</b>"
        end

        def puts_detail(text)
          puts "      #{text}"
        end
      end # class ConsoleRunner
    end # module Depcheck

  end # module PlatformInfo
end # module PhusionPassenger
