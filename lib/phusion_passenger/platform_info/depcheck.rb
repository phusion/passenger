# encoding: utf-8
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/platform_info/linux'
require 'phusion_passenger/platform_info/compiler'
require 'phusion_passenger/platform_info/operating_system'

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
# where some dependencies are suitable while others are not. Therefore the
# Depcheck framework will ensure that the user is notified of all the specs'
# internal thoughts so that he can override the decision if necessary.
module Depcheck
	@@loaded   = {}
	@@database = {}

	def self.load(filename)
		if !@@loaded[filename]
			content = File.read(filename)
			instance_eval(content, filename)
			@@loaded[filename] = true
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
				else
					"Search Google for '#{@name}'."
				end
			end
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

		def check_for_command(name)
			result = find_command(name)
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

		def check_for_library(name)
			check_by_compiling("int main() { return 0; }", :cxx, nil, "-l#{name}")
		end

		def check_by_compiling(source, language = :c, cflags = nil, linkflags = nil)
			case language
			when :c
				source_file		= "#{PlatformInfo.tmpexedir}/depcheck-#{Process.pid}-#{Thread.current.object_id}.c"
				compiler			 = "gcc"
				compiler_flags = ENV['CFLAGS']
			when :cxx
				source_file		= "#{PlatformInfo.tmpexedir}/depcheck-#{Process.pid}-#{Thread.current.object_id}.cpp"
				compiler			 = "g++"
				compiler_flags = "#{ENV['CFLAGS']} #{ENV['CXXFLAGS']}".strip
			else
				raise ArgumentError, "Unknown language '#{language}"
			end
		
			output_file = "#{PlatformInfo.tmpexedir}/depcheck-#{Process.pid}-#{Thread.current.object_id}"
		
			begin
				File.open(source_file, 'w') do |f|
					f.puts(source)
				end
			
				if find_command(compiler)
					command = "#{compiler} #{compiler_flags} #{cflags} " +
						"#{source_file} -o #{output_file} #{linkflags}"
					[!!system(command)]
				else
					[:unknown, "Cannot check: compiler '#{compiler}' not found."]
				end
			ensure
				File.unlink(source_file) rescue nil
				File.unlink(output_file) rescue nil
			end
		end

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
			if (linux_distro_tags || []).include?(platform)
				yield
			else
				case platform
				when :linux
					yield if PlatformInfo.os_name =~ /linux/
				when :freebsd
					yield if PlatformInfo.os_name =~ /freebsd/
				when :macosx
					yield if PlatformInfo.os_name == "macosx"
				when :solaris
					yield if PlatformInfo.os_name =~ /solaris/
				when :other_platforms
					yield
				end
			end
			@on_invoked = true
		end

		def apt_get_install(package_name)
			install_instructions("Please install it with <b>apt-get install #{package_name}</b>")
		end

		def urpmi(package_name)
			install_instructions("Please install it with <b>urpmi #{package_name}</b>")
		end

		def yum_install(package_name)
			install_instructions("Please install it with <b>yum install #{package_name}</b>")
		end

		def emerge(package_name)
			install_instructions("Please install it with <b>emerge -av #{package_name}</b>")
		end

		def gem_install(package_name)
			install_instructions("Please make sure RubyGems is installed, then run " +
				"<b>#{gem_command || 'gem'} install #{package_name}</b>")
		end

		def xcode_install(component)
			install_instructions("Please install the Apple Development Tools: http://developer.apple.com/tools/")
		end


		def ruby_command
			PlatformInfo.ruby_command
		end

		def gem_command
			PlatformInfo.gem_command
		end

		def find_command(command)
			PlatformInfo.find_command(command)
		end

		def linux_distro_tags
			PlatformInfo.linux_distro_tags
		end

		def locate_ruby_tool(name)
			PlatformInfo.locate_ruby_tool(name)
		end
	end # class Dependency
end # module Depcheck

end # module PlatformInfo
end # module PhusionPassenger
