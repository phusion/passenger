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

module Passenger
	FRAMEWORK_SPAWNER_MAX_IDLE_TIME = 30 * 60
	APP_SPAWNER_MAX_IDLE_TIME       = 10 * 60
	
	SPAWNER_CLEAN_INTERVAL = [
		FRAMEWORK_SPAWNER_MAX_IDLE_TIME,
		APP_SPAWNER_MAX_IDLE_TIME
	].min + 5
	APP_SPAWNER_CLEAN_INTERVAL = APP_SPAWNER_MAX_IDLE_TIME + 5
end
