#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2009  Phusion
#
#  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

require 'passenger/console_text_template'
require 'passenger/version'

module Passenger

# Abstract base class for installers. Used by passenger-install-apache2-module
# and passenger-install-nginx-module.
class AbstractInstaller
	PASSENGER_WEBSITE = "http://www.modrails.com/"
	PHUSION_WEBSITE = "www.phusion.nl"
	USERS_GUIDE = "TODO"

	def initialize(options = {})
		options.each_pair do |key, value|
			instance_variable_set(:"@#{key}", value)
		end
	end
	
	def start
		install!
	ensure
		reset_terminal_colors
	end

private
	def reset_terminal_colors
		STDOUT.write("\e[0m")
		STDOUT.flush
	end
	
	def color_print(text)
		STDOUT.write(ConsoleTextTemplate.new(:text => text).result)
		STDOUT.flush
	end
	
	def color_puts(text)
		color_print("#{text}\n")
	end
	
	def render_template(name, options = {})
		puts ConsoleTextTemplate.new({ :file => name }, options).result
	end
	
	def line
		puts "--------------------------------------------"
	end
	
	def wait(timeout = nil)
		return if @auto
		begin
			if timeout
				require 'timeout' unless defined?(Timeout)
				begin
					Timeout.timeout(timeout) do
						STDIN.readline
					end
				rescue Timeout::Error
					# Do nothing.
				end
			else
				STDIN.readline
			end
		rescue Interrupt
			exit 2
		end
	end
	
	def check_dependencies
		missing_dependencies = []
		color_puts "<banner>Checking for required software...</banner>"
		puts
		REQUIRED_DEPENDENCIES.each do |dep|
			color_print " * #{dep.name}... "
			result = dep.check
			if result.found?
				if result.found_at
					color_puts "<green>found at #{result.found_at}</green>"
				else
					color_puts "<green>found</green>"
				end
			else
				color_puts "<red>not found</red>"
				missing_dependencies << dep
			end
		end
		
		if missing_dependencies.empty?
			return true
		else
			puts
			color_puts "<red>Some required software is not installed.</red>"
			color_puts "But don't worry, this installer will tell you how to install them.\n"
			color_puts "<b>Press Enter to continue, or Ctrl-C to abort.</b>"
			if natively_packaged?
				wait(10)
			else
				wait
			end
			
			line
			color_puts "<banner>Installation instructions for required software</banner>"
			puts
			missing_dependencies.each do |dep|
				print_dependency_installation_instructions(dep)
				puts
			end
			color_puts "If the aforementioned instructions didn't solve your problem, then please take"
			color_puts "a look at the Users Guide:"
			puts
			color_puts "  <yellow>#{USERS_GUIDE}</yellow>"
			return false
		end
	end
	
	def print_dependency_installation_instructions(dep)
		color_puts " * To install <yellow>#{dep.name}</yellow>:"
		if !dep.install_command.nil?
			color_puts "   Please run <b>#{dep.install_command}</b> as root."
		elsif !dep.install_instructions.nil?
			color_puts "   " << dep.install_instructions
		elsif !dep.website.nil?
			color_puts "   Please download it from <b>#{dep.website}</b>"
			if !dep.website_comments.nil?
				color_puts "   (#{dep.website_comments})"
			end
		else
			color_puts "   Search Google."
		end
	end
end

end # module Passenger