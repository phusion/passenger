#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
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

class Object # :nodoc:
	@@benchmark_results = {}

	def b!(name)
		time1 = Time.now
		begin
			yield
		ensure
			time2 = Time.now
			@@benchmark_results[name] = 0 unless @@benchmark_results.has_key?(name)
			@@benchmark_results[name] += time2 - time1
		end 
	end

	def benchmark_report(main = nil)
		total = 0
		if main.nil?
			@@benchmark_results.each_value do |time|
				total += time
			end
		else
			total = @@benchmark_results[main]
		end
		@@benchmark_results.each_pair do |name, time|
			printf "%-12s: %.4f (%.2f%%)\n", name, time, time / total * 100
		end
		printf "-- Total: %.4f\n", total
	end
end
