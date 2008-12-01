/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
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
#ifndef _PASSENGER_BUCKET_H_
#define _PASSENGER_BUCKET_H_

/**
 * apr_bucket_pipe closes a pipe's file descriptor when it has reached end-of-stream,
 * but not when an error has occurred. That behavior conflicts with Phusion Passenger's
 * file descriptor management code.
 *
 * passenger_bucket is like apr_bucket_pipe, but never closes the pipe's file descriptor.
 * It also ignores the APR_NONBLOCK_READ because that's known to cause strange
 * I/O problems.
 */

#include "apr_buckets.h"

apr_bucket *passenger_bucket_create(apr_file_t *pipe, apr_bucket_alloc_t *list);

#endif /* _PASSENGER_BUCKET_H_ */

