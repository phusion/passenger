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

require 'phusion_passenger/constants'

module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module ACRescueExtension
protected
	def rescue_action_with_passenger(exception)
		# When a controller action crashes, log the exception.
		# But ignore routing errors (404s and stuff).
		request.env["PASSENGER_ACTION_FAILED"] = true
		if !defined?(ActionController::RoutingError) || !exception.is_a?(ActionController::RoutingError)
			AnalyticsLogging.new_transaction_log(request.env, :exceptions) do |log|
				request_txn_id = request.env[PASSENGER_TXN_ID]
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")
				
				log.message("Request transaction ID: #{request_txn_id}")
				log.message("Message: #{message}")
				log.message("Class: #{exception.class.name}")
				log.message("Backtrace: #{backtrace_string}")
				log.message("Controller action: #{controller_class_name}##{action_name}")
			end
		end
		rescue_action_without_passenger(exception)
	end
end

end
end
end