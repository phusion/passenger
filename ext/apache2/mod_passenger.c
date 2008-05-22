/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Passenger is a trademark of Hongli Lai & Ninh Bui.
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
#include <httpd.h>
#include <http_config.h>
#include "Configuration.h"
#include "Hooks.h"

module AP_MODULE_DECLARE_DATA passenger_module = {
	STANDARD20_MODULE_STUFF,
	passenger_config_create_dir,        /* create per-dir config structs */
	passenger_config_merge_dir,         /* merge per-dir config structs */
	passenger_config_create_server,     /* create per-server config structs */
	passenger_config_merge_server,      /* merge per-server config structs */
	passenger_commands,                 /* table of config file commands */
	passenger_register_hooks,           /* register hooks */
};
