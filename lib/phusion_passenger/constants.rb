#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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
	PASSENGER_ANALYTICS_WEB_LOG = "PASSENGER_ANALYTICS_WEB_LOG".freeze
	PASSENGER_TXN_ID            = "PASSENGER_TXN_ID".freeze
	PASSENGER_UNION_STATION_KEY = "PASSENGER_UNION_STATION_KEY".freeze
	RACK_HIJACK_IO              = "rack.hijack_io".freeze

	# If you change the structure version then don't forget to change
	# ext/common/ServerInstanceDir.h too.
	DIR_STRUCTURE_MAJOR_VERSION = 1
	DIR_STRUCTURE_MINOR_VERSION = 0
	GENERATION_STRUCTURE_MAJOR_VERSION = 2
	GENERATION_STRUCTURE_MINOR_VERSION = 0
end
