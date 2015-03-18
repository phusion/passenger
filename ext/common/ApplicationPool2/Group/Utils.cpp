/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion
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

// This file is included inside the Group class.

private:

static void
runAllActions(const boost::container::vector<Callback> &actions) {
	boost::container::vector<Callback>::const_iterator it, end = actions.end();
	for (it = actions.begin(); it != end; it++) {
		(*it)();
	}
}

static void
doCleanupSpawner(SpawningKit::SpawnerPtr spawner) {
	spawner->cleanup();
}

unsigned int
generateStickySessionId() {
	unsigned int result;

	while (true) {
		result = (unsigned int) rand();
		if (result != 0 && findProcessWithStickySessionId(result) == NULL) {
			return result;
		}
	}
	// Never reached; shut up compiler warning.
	return 0;
}

/**
 * Persists options into this Group. Called at creation time and at restart time.
 * Values will be persisted into `destination`. Or if it's NULL, into `this->options`.
 */
void
resetOptions(const Options &newOptions, Options *destination = NULL) {
	if (destination == NULL) {
		destination = &this->options;
	}
	*destination = newOptions;
	destination->persist(newOptions);
	destination->clearPerRequestFields();
	destination->groupSecret = getSecret();
	destination->groupUuid   = uuid;
}

/**
 * Merges some of the new options from the latest get() request into this Group.
 */
void
mergeOptions(const Options &other) {
	options.maxRequests      = other.maxRequests;
	options.minProcesses     = other.minProcesses;
	options.statThrottleRate = other.statThrottleRate;
	options.maxPreloaderIdleTime = other.maxPreloaderIdleTime;
}
