#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
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

require 'passenger/abstract_request_handler'
require 'passenger/railz/cgi_fixed'
module Passenger
module Railz

# A request handler for Ruby on Rails applications.
class RequestHandler < AbstractRequestHandler
	NINJA_PATCHING_LOCK = Mutex.new
	@@ninja_patched_action_controller = false
	
	def initialize(owner_pipe)
		super(owner_pipe)
		NINJA_PATCHING_LOCK.synchronize do
			ninja_patch_action_controller
		end
	end

protected
	# Overrided method.
	def process_request(headers, input, output)
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
					headers[X_POWERED_BY] = PASSENGER_HEADER
					passenger_orig_perform_action(*whatever)
				end
			end
		end
	end
end

end # module Railz
end # module Passenger
