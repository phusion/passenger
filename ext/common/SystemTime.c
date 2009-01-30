/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2009  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "SystemTime.h"

static int has_forced_value = 0;
static time_t forced_value = 0;

time_t
passenger_system_time_get() {
	if (has_forced_value) {
		return forced_value;
	} else {
		return time(NULL);
	}
}

void
passenger_system_time_force_value(time_t value) {
	has_forced_value = 1;
	forced_value = value;
}

void
passenger_system_time_release_forced_value() {
	has_forced_value = 0;
}
