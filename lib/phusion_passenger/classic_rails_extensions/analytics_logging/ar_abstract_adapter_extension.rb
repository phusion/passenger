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
require 'digest/md5'

module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module ARAbstractAdapterExtension
protected
	def log_with_passenger(sql, name, &block)
		# Log SQL queries and durations.
		log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			if name
				name = name.strip
			else
				name = "SQL"
			end
			digest = Digest::MD5.hexdigest("#{name}\0#{sql}\0#{rand}")
			log.measure("DB BENCHMARK: #{digest}", "#{name}\n#{sql}") do
				log_without_passenger(sql, name, &block)
			end
		else
			log_without_passenger(sql, name, &block)
		end
	end
end

end
end
end