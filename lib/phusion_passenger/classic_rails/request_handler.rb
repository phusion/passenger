# encoding: binary
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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

require 'phusion_passenger/abstract_request_handler'
require 'phusion_passenger/classic_rails/cgi_fixed'
module PhusionPassenger
module ClassicRails

# A request handler for Ruby on Rails applications.
class RequestHandler < AbstractRequestHandler
	CONTENT_LENGTH      = 'CONTENT_LENGTH'      # :nodoc:
	HTTP_CONTENT_LENGTH = 'HTTP_CONTENT_LENGTH' # :nodoc:
	
	NINJA_PATCHING_LOCK = Mutex.new             # :nodoc:
	@@ninja_patched_action_controller = false
	
	def initialize(owner_pipe, options = {})
		super(owner_pipe, options)
		NINJA_PATCHING_LOCK.synchronize do
			ninja_patch_action_controller
		end
	end

protected
	# Overrided method.
	def process_request(headers, input, output, status_line_desired)
		# FIXME: how to print status line?
		headers[CONTENT_LENGTH] = headers[HTTP_CONTENT_LENGTH]
		cgi = CGIFixed.new(headers, input, output)
		::Dispatcher.dispatch(cgi,
			::ActionController::CgiRequest::DEFAULT_SESSION_OPTIONS,
			cgi.stdoutput)
	end
	
private
	def ninja_patch_action_controller
		if !@@ninja_patched_action_controller && defined?(::ActionController::Base) \
		&& ::ActionController::Base.private_method_defined?(:perform_action)
			@@ninja_patched_action_controller = true
			::ActionController::Base.class_eval do
				alias passenger_orig_perform_action perform_action
				
				def perform_action(*whatever)
					headers[X_POWERED_BY] = @passenger_header
					passenger_orig_perform_action(*whatever)
				end
				private :perform_action
			end
		end
	end
end

end # module ClassicRails
end # module PhusionPassenger
