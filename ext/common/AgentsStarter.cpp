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
#include <AgentsStarter.h>

using namespace std;
using namespace boost;
using namespace oxt;


PSG_VariantMap *
psg_variant_map_new() {
	return (PSG_VariantMap *) new Passenger::VariantMap();
}

void
psg_variant_map_set(PSG_VariantMap *m,
	const char *name,
	const char *value,
	unsigned int value_len)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->set(name, string(value, value_len));
}

void
psg_variant_map_set2(PSG_VariantMap *m,
	const char *name,
	unsigned int name_len,
	const char *value,
	unsigned int value_len)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->set(string(name, name_len), string(value, value_len));
}

void
psg_variant_map_set_int(PSG_VariantMap *m,
	const char *name,
	int value)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->setInt(name, value);
}

void
psg_variant_map_set_bool(PSG_VariantMap *m,
	const char *name,
	int value)
{
	Passenger::VariantMap *vm = (Passenger::VariantMap *) m;
	vm->setBool(name, value);
}

void
psg_variant_map_set_strset(PSG_VariantMap *m,
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
psg_variant_map_free(PSG_VariantMap *m) {
	delete (Passenger::VariantMap *) m;
}


PSG_AgentsStarter *
psg_agents_starter_new(PSG_AgentsStarterType type, char **error_message) {
	return (PSG_AgentsStarter *) new Passenger::AgentsStarter(type);
}

int
psg_agents_starter_start(PSG_AgentsStarter *as,
	const char *passengerRoot,
	PSG_VariantMap *extraParams,
	const PSG_AfterForkCallback afterFork,
	void *callbackArgument,
	char **errorMessage)
{
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	this_thread::disable_syscall_interruption dsi;
	try {
		function<void ()> afterForkFunctionObject;
		
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
psg_agents_starter_get_request_socket_filename(PSG_AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketFilename().size();
	}
	return agentsStarter->getRequestSocketFilename().c_str();
}

const char *
psg_agents_starter_get_request_socket_password(PSG_AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketPassword().size();
	}
	return agentsStarter->getRequestSocketPassword().c_str();
}

const char *
psg_agents_starter_get_server_instance_dir(PSG_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getServerInstanceDir()->getPath().c_str();
}

const char *
psg_agents_starter_get_generation_dir(PSG_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getGeneration()->getPath().c_str();
}

pid_t
psg_agents_starter_get_pid(PSG_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getPid();
}

void
psg_agents_starter_detach(PSG_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	agentsStarter->detach();
}

void
psg_agents_starter_free(PSG_AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	delete agentsStarter;
}
