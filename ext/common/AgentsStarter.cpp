/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#include <oxt/thread.hpp>
#include <set>
#include <cerrno>
#include <cstring>
#include <string.h>
#include <AgentsStarter.h>
#include <Exceptions.h>

using namespace std;
using namespace boost;
using namespace oxt;


PP_VariantMap *
pp_variant_map_new() {
	return (PP_VariantMap *) new Passenger::VariantMap();
}

void
pp_variant_map_set(PP_VariantMap *m,
	const char *name,
	const char *value,
	unsigned int value_len)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->set(name, string(value, value_len));
}

void
pp_variant_map_set2(PP_VariantMap *m,
	const char *name,
	unsigned int name_len,
	const char *value,
	unsigned int value_len)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->set(string(name, name_len), string(value, value_len));
}

void
pp_variant_map_set_int(PP_VariantMap *m,
	const char *name,
	int value)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->setInt(name, value);
}

void
pp_variant_map_set_bool(PP_VariantMap *m,
	const char *name,
	int value)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->setBool(name, value);
}

void
pp_variant_map_set_strset(PP_VariantMap *m,
	const char *name,
	const char **strs,
	unsigned int count)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	std::set<string> the_set;

	for (unsigned int i = 0; i < count; i++) {
		the_set.insert(strs[i]);
	}
	vm->setStrSet(name, the_set);
}

void
pp_variant_map_free(PP_VariantMap *m) {
	delete (Passenger::VariantMap *) m;
}


PP_AgentsStarter *
pp_agents_starter_new(PP_AgentsStarterType type, char **error_message) {
	return (PP_AgentsStarter *) new Passenger::AgentsStarter(type);
}

int
pp_agents_starter_start(PP_AgentsStarter *as,
	const char *passengerRoot,
	PP_VariantMap *extraParams,
	const PP_AfterForkCallback afterFork,
	void *callbackArgument,
	char **errorMessage)
{
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	this_thread::disable_syscall_interruption dsi;
	try {
		boost::function<void ()> afterForkFunctionObject;

		if (afterFork != NULL) {
			afterForkFunctionObject = boost::bind(afterFork, callbackArgument);
		}
		agentsStarter->start(passengerRoot,
			*((Passenger::VariantMap *) extraParams),
			afterForkFunctionObject);
		return 1;
	} catch (const Passenger::SystemException &e) {
		errno = e.code();
		*errorMessage = strdup(e.what());
		return 0;
	} catch (const std::exception &e) {
		errno = -1;
		*errorMessage = strdup(e.what());
		return 0;
	}
}

const char *
pp_agents_starter_get_request_socket_filename(PP_AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketFilename().size();
	}
	return agentsStarter->getRequestSocketFilename().c_str();
}

const char *
pp_agents_starter_get_request_socket_password(PP_AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketPassword().size();
	}
	return agentsStarter->getRequestSocketPassword().c_str();
}

const char *
pp_agents_starter_get_server_instance_dir(PP_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getServerInstanceDir()->getPath().c_str();
}

const char *
pp_agents_starter_get_generation_dir(PP_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getGeneration()->getPath().c_str();
}

pid_t
pp_agents_starter_get_pid(PP_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getPid();
}

void
pp_agents_starter_detach(PP_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	agentsStarter->detach();
}

void
pp_agents_starter_free(PP_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	delete agentsStarter;
}
