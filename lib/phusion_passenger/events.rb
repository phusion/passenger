#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2009  Phusion
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

module PhusionPassenger
	@@event_starting_worker_process = []
	@@event_stopping_worker_process = []
	
	def self.on_event(name, &block)
		callback_list_for_event(name) << block
	end
	
	def self.call_event(name, *args)
		callback_list_for_event(name).each do |callback|
			callback.call(*args)
		end
	end

private
	def self.callback_list_for_event(name)
		return case name
		when :starting_worker_process
			@@event_starting_worker_process
		when :stopping_worker_process
			@@event_stopping_worker_process
		else
			raise ArgumentError, "Unknown event name '#{name}'"
		end
	end

end # module PhusionPassenger
