/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2015 Phusion
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
#ifndef _PASSENGER_SPAWNING_KIT_RESULT_H_
#define _PASSENGER_SPAWNING_KIT_RESULT_H_

#include <vector>
#include <cassert>
#include <cstring>

#include <sys/types.h>

#include <StaticString.h>
#include <FileDescriptor.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;


struct Result {
	static const unsigned int GUPID_MAX_SIZE = 20;

	struct Socket {
		string name;
		string address;
		string protocol;
		int concurrency;
	};

	enum Type {
		OS_PROCESS,
		DUMMY_PROCESS
	};

	/**
	 * When OS_PROCESS, it means that this Result refers to a real OS process.
	 *
	 * When DUMMY_PROCESS, it means that the information in this Result is fake.
	 * This is the case if the Result is created by a DummySpawner, which is used
	 * in unit tests. The sockets in the socket list are fake and need not be
	 * deleted, the admin socket need not be closed, etc.
	 */
	Type type;

	/**
	 * The operating system process ID.
	 */
	pid_t pid;

	/**
	 * UUID for this process, randomly generated and extremely unlikely to ever
	 * appear again in this universe.
	 */
	char gupid[GUPID_MAX_SIZE];
	unsigned int gupidSize;

	/** Admin socket. See Process class description. */
	FileDescriptor adminSocket;

	/**
	 * Pipe on which this process outputs errors. Mapped to the process's STDERR.
	 * Only Processes spawned by DirectSpawner have this set.
	 * SmartSpawner-spawned Processes use the same STDERR as their parent preloader processes.
	 */
	FileDescriptor errorPipe;

	/** The sockets that this Process listens on for connections. */
	vector<Socket> sockets;

	/**
	 * The code revision of the application, inferred through various means.
	 * See Spawner::prepareSpawn() to learn how this is determined.
	 * May be an empty string if no code revision has been inferred.
	 */
	string codeRevision;

	/**
	 * Time at which the Spawner that created this process was created.
	 * Microseconds resolution.
	 */
	unsigned long long spawnerCreationTime;

	/**
	 * Time at which we started spawning this process. Microseconds resolution.
	 */
	unsigned long long spawnStartTime;

	void setGupid(const StaticString &str) {
		assert(str.size() <= GUPID_MAX_SIZE);
		memcpy(gupid, str.data(), str.size());
		gupidSize = str.size();
	}

	Result()
		: type(OS_PROCESS),
		  pid((pid_t) -1),
		  gupidSize(0),
		  spawnerCreationTime(0),
		  spawnStartTime(0)
		{ }
};


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_RESULT_H_ */
