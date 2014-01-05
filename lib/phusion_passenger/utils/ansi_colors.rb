#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

module PhusionPassenger
module Utils

module AnsiColors
	RESET    = "\e[0m".freeze
	BOLD     = "\e[1m".freeze
	DGRAY    = "\e[90m".freeze
	RED      = "\e[31m".freeze
	ORANGE   = "\e[38;5;214m".freeze
	GREEN    = "\e[32m".freeze
	YELLOW   = "\e[33m".freeze
	WHITE    = "\e[37m".freeze
	BLACK_BG = "\e[40m".freeze
	BLUE_BG  = "\e[44m".freeze
	DEFAULT_TERMINAL_COLOR = "#{RESET}#{WHITE}#{BLACK_BG}".freeze
	
	extend self  # Make methods available as class methods.
	
	def self.included(klass)
		# When included into another class, make sure that Utils
		# methods are made private.
		public_instance_methods(false).each do |method_name|
			klass.send(:private, method_name)
		end
	end
	
	def ansi_colorize(text)
		text = text.gsub(%r{<b>(.*?)</b>}m, "#{BOLD}\\1#{DEFAULT_TERMINAL_COLOR}")
		text.gsub!(%r{<dgray>(.*?)</dgray>}m, "#{BOLD}#{DGRAY}\\1#{DEFAULT_TERMINAL_COLOR}")
		text.gsub!(%r{<red>(.*?)</red>}m, "#{BOLD}#{RED}\\1#{DEFAULT_TERMINAL_COLOR}")
		text.gsub!(%r{<orange>(.*?)</orange>}m, "#{BOLD}#{ORANGE}\\1#{DEFAULT_TERMINAL_COLOR}")
		text.gsub!(%r{<green>(.*?)</green>}m, "#{BOLD}#{GREEN}\\1#{DEFAULT_TERMINAL_COLOR}")
		text.gsub!(%r{<yellow>(.*?)</yellow>}m, "#{BOLD}#{YELLOW}\\1#{DEFAULT_TERMINAL_COLOR}")
		text.gsub!(%r{<banner>(.*?)</banner>}m, "#{BOLD}#{BLUE_BG}#{YELLOW}\\1#{DEFAULT_TERMINAL_COLOR}")
		return text
	end

	def strip_color_tags(text)
		text = text.gsub(%r{<b>(.*?)</b>}m, "\\1")
		text = text.gsub(%r{<dgray>(.*?)</dgray>}m, "\\1")
		text.gsub!(%r{<red>(.*?)</red>}m, "\\1")
		text.gsub!(%r{<orange>(.*?)</orange>}m, "\\1")
		text.gsub!(%r{<green>(.*?)</green>}m, "\\1")
		text.gsub!(%r{<yellow>(.*?)</yellow>}m, "\\1")
		text.gsub!(%r{<banner>(.*?)</banner>}m, "\\1")
		return text
	end
end

end # module Utils
end # module PhusionPassenger
