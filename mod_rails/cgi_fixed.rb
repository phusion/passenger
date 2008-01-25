require 'cgi'
require 'stringio'

module ModRails # :nodoc:

# Modifies CGI so that we can use it.  Main thing it does is expose
# the stdinput and stdoutput so SCGI::Processor can connect them to
# the right sources.  It also exposes the env_table so that SCGI::Processor
# and hook the SCGI parameters into the environment table.
#
# This is partially based on the FastCGI code, but much of the Ruby 1.6 
# backwards compatibility is removed.
#
# DISCLAIMER: This code is copied verbatim from Zed Shaw's scgi_rails package, version 0.4.3.
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
end # module ModRails