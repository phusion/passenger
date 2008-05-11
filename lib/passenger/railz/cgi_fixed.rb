# NOTE: This code is based on cgi_fixed.rb from Zed Shaw's scgi_rails
# package, version 0.4.3. The license of this single file is as follows:
#
#  Copyright (c) 2004 Zed A. Shaw
#  
#  Permission is hereby granted, free of charge, to any person obtaining
#  a copy of this software and associated documentation files (the
#  "Software"), to deal in the Software without restriction, including
#  without limitation the rights to use, copy, modify, merge, publish,
#  distribute, sublicense, and/or sell copies of the Software, and to
#  permit persons to whom the Software is furnished to do so, subject to
#  the following conditions:
#  
#  The above copyright notice and this permission notice shall be
#  included in all copies or substantial portions of the Software.
#  
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
#  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
#  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
#  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# We also made some small local modifications.

require 'cgi'

module Passenger
module Railz

# Modifies CGI so that we can use it.  Main thing it does is expose
# the stdinput and stdoutput so RequestHandler can connect them to
# the right sources.  It also exposes the env_table so that RequestHandler
# can hook the request parameters into the environment table.
#
# This is partially based on the FastCGI code, but much of the Ruby 1.6 
# backwards compatibility is removed.
class CGIFixed < ::CGI
	public :env_table
	
	def initialize(params, input, output, *args)
		@env_table = params
		@args = *args
		@input = input
		@out = output
		super(*args)
	end
	
	def args
		@args
	end
	
	def env_table
		@env_table
	end
	
	def stdinput
		@input
	end
	
	def stdoutput
		@out
	end
end

end # module Railz
end # module Passenger
