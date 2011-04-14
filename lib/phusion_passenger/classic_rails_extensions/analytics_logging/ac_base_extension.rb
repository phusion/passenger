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

module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module ACBaseExtension
private
	def perform_action_with_passenger(*args)
		# Log controller and action name.
		log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.begin_measure("framework request processing")
			begin
				log.message("Controller action: #{controller_class_name}##{action_name}")
				perform_action_without_passenger(*args)
			ensure
				log.end_measure("framework request processing",
					request.env["PASSENGER_ACTION_FAILED"])
			end
		else
			perform_action_without_passenger(*args)
		end
	end
	
protected
	def render_with_passenger(*args, &block)
		log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.measure("view rendering") do
				result = render_without_passenger(*args, &block)
				view_runtime = @view_runtime || @rendering_runtime
				if view_runtime
					log.message "View rendering time: #{(view_runtime * 1000).to_i}"
				end
				return result
			end
		else
			render_without_passenger(*args, &block)
		end
	end
end

end
end
end