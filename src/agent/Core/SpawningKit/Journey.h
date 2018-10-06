/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_HANDSHAKE_JOURNEY_H_
#define _PASSENGER_SPAWNING_KIT_HANDSHAKE_JOURNEY_H_

#include <map>
#include <utility>

#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>

#include <jsoncpp/json.h>

#include <LoggingKit/LoggingKit.h>
#include <StaticString.h>
#include <SystemTools/SystemTime.h>
#include <JsonTools/JsonUtils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;


/**
 * As explained in README.md, there are three possible journeys,
 * although each journey can have small variations (based on whether
 * a wrapper is used or not).
 */
enum JourneyType {
	SPAWN_DIRECTLY,
	START_PRELOADER,
	SPAWN_THROUGH_PRELOADER
};

enum JourneyStep {
	// Steps in Passenger Core / SpawningKit
	SPAWNING_KIT_PREPARATION,
	SPAWNING_KIT_FORK_SUBPROCESS,
	SPAWNING_KIT_CONNECT_TO_PRELOADER,
	SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER,
	SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER,
	SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER,
	SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER,
	SPAWNING_KIT_HANDSHAKE_PERFORM,
	SPAWNING_KIT_FINISH,

	// Steps in preloader (when spawning a worker process)
	PRELOADER_PREPARATION,
	PRELOADER_FORK_SUBPROCESS,
	PRELOADER_SEND_RESPONSE,
	PRELOADER_FINISH,

	// Steps in subprocess
	SUBPROCESS_BEFORE_FIRST_EXEC,
	SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL,
	SUBPROCESS_OS_SHELL,
	SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL,
	SUBPROCESS_EXEC_WRAPPER,
	SUBPROCESS_WRAPPER_PREPARATION,
	SUBPROCESS_APP_LOAD_OR_EXEC,
	SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER,
	SUBPROCESS_LISTEN,
	SUBPROCESS_FINISH,

	// Other
	UNKNOWN_JOURNEY_STEP
};

enum JourneyStepState {
	/**
	 * This step has not started yet. Will be visualized with an empty
	 * placeholder.
	 */
	STEP_NOT_STARTED,

	/**
	 * This step is currently in progress. Will be visualized with a spinner.
	 */
	STEP_IN_PROGRESS,

	/**
	 * This step has already been performed successfully. Will be
	 * visualized with a green tick.
	 */
	STEP_PERFORMED,

	/**
	 * This step has failed. Will be visualized with a red mark.
	 */
	STEP_ERRORED,

	UNKNOWN_JOURNEY_STEP_STATE
};


inline OXT_PURE StaticString journeyTypeToString(JourneyType type);
inline OXT_PURE StaticString journeyStepToString(JourneyStep step);
inline OXT_PURE string journeyStepToStringLowerCase(JourneyStep step);
inline OXT_PURE StaticString journeyStepStateToString(JourneyStepState state);
inline OXT_PURE JourneyStepState stringToJourneyStepState(const StaticString &value);

inline OXT_PURE JourneyStep getFirstCoreJourneyStep() { return SPAWNING_KIT_PREPARATION; }
inline OXT_PURE JourneyStep getLastCoreJourneyStep() { return SPAWNING_KIT_FINISH; }
inline OXT_PURE JourneyStep getFirstPreloaderJourneyStep() { return PRELOADER_PREPARATION; }
inline OXT_PURE JourneyStep getLastPreloaderJourneyStep() { return PRELOADER_FINISH; }
inline OXT_PURE JourneyStep getFirstSubprocessJourneyStep() { return SUBPROCESS_BEFORE_FIRST_EXEC; }
inline OXT_PURE JourneyStep getLastSubprocessJourneyStep() { return SUBPROCESS_FINISH; }


class JourneyStepInfo {
private:
	MonotonicTimeUsec getEndTime(const JourneyStepInfo *nextStepInfo) const {
		if (nextStepInfo != NULL && nextStepInfo->beginTime != 0) {
			return nextStepInfo->beginTime;
		} else {
			return endTime;
		}
	}

public:
	JourneyStep step, nextStep;
	JourneyStepState state;
	MonotonicTimeUsec beginTime;
	MonotonicTimeUsec endTime;

	JourneyStepInfo(JourneyStep _step, JourneyStepState _state = STEP_NOT_STARTED)
		: step(_step),
		  nextStep(UNKNOWN_JOURNEY_STEP),
		  state(_state),
		  beginTime(0),
		  endTime(0)
		{ }

	unsigned long long usecDuration(const JourneyStepInfo *nextStepInfo) const {
		if (getEndTime(nextStepInfo) >= beginTime) {
			return getEndTime(nextStepInfo) - beginTime;
		} else {
			return 0;
		}
	}

	Json::Value inspectAsJson(const JourneyStepInfo *nextStepInfo, MonotonicTimeUsec monoNow,
		unsigned long long now) const
	{
		Json::Value doc;

		doc["state"] = journeyStepStateToString(state).toString();
		if (beginTime != 0) {
			doc["begin_time"] = monoTimeToJson(beginTime, monoNow, now);
		}
		if (endTime != 0) {
			doc["end_time"] = monoTimeToJson(endTime, monoNow, now);
			doc["duration"] = usecDuration(nextStepInfo) / 1000000.0;
		}
		return doc;
	}
};


/**
 * For an introduction see README.md, sections:
 *
 *  - "The Journey class"
 *  - "Subprocess journey logging"
 */
class Journey {
public:
	typedef map<JourneyStep, JourneyStepInfo> Map;

private:
	JourneyType type;
	bool usingWrapper;
	Map steps;

	void insertStep(JourneyStep step, bool first = false) {
		steps.insert(make_pair(step, JourneyStepInfo(step)));
		if (!first) {
			Map::iterator prev = steps.end();
			prev--;
			prev--;
			prev->second.nextStep = step;
		}
	}

	void fillInStepsForSpawnDirectlyJourney() {
		insertStep(SPAWNING_KIT_PREPARATION, true);
		insertStep(SPAWNING_KIT_FORK_SUBPROCESS);
		insertStep(SPAWNING_KIT_HANDSHAKE_PERFORM);
		insertStep(SPAWNING_KIT_FINISH);

		insertStep(SUBPROCESS_BEFORE_FIRST_EXEC, true);
		insertStep(SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL);
		insertStep(SUBPROCESS_OS_SHELL);
		insertStep(SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL);
		if (usingWrapper) {
			insertStep(SUBPROCESS_EXEC_WRAPPER);
			insertStep(SUBPROCESS_WRAPPER_PREPARATION);
		}
		insertStep(SUBPROCESS_APP_LOAD_OR_EXEC);
		insertStep(SUBPROCESS_LISTEN);
		insertStep(SUBPROCESS_FINISH);
	}

	void fillInStepsForPreloaderStartJourney() {
		insertStep(SPAWNING_KIT_PREPARATION, true);
		insertStep(SPAWNING_KIT_FORK_SUBPROCESS);
		insertStep(SPAWNING_KIT_HANDSHAKE_PERFORM);
		insertStep(SPAWNING_KIT_FINISH);

		insertStep(SUBPROCESS_BEFORE_FIRST_EXEC, true);
		insertStep(SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL);
		insertStep(SUBPROCESS_OS_SHELL);
		insertStep(SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL);
		if (usingWrapper) {
			insertStep(SUBPROCESS_EXEC_WRAPPER);
			insertStep(SUBPROCESS_WRAPPER_PREPARATION);
		}
		insertStep(SUBPROCESS_APP_LOAD_OR_EXEC);
		insertStep(SUBPROCESS_LISTEN);
		insertStep(SUBPROCESS_FINISH);
	}

	void fillInStepsForSpawnThroughPreloaderJourney() {
		insertStep(SPAWNING_KIT_PREPARATION, true);
		insertStep(SPAWNING_KIT_CONNECT_TO_PRELOADER);
		insertStep(SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER);
		insertStep(SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER);
		insertStep(SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER);
		insertStep(SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER);
		insertStep(SPAWNING_KIT_HANDSHAKE_PERFORM);
		insertStep(SPAWNING_KIT_FINISH);

		insertStep(PRELOADER_PREPARATION, true);
		insertStep(PRELOADER_FORK_SUBPROCESS);
		insertStep(PRELOADER_SEND_RESPONSE);
		insertStep(PRELOADER_FINISH);

		insertStep(SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER, true);
		insertStep(SUBPROCESS_LISTEN);
		insertStep(SUBPROCESS_FINISH);
	}

	JourneyStepInfo &getStepInfoMutable(JourneyStep step) {
		Map::iterator it = steps.find(step);
		if (it == steps.end()) {
			throw RuntimeException("Invalid step " + journeyStepToString(step));
		}

		return it->second;
	}

public:
	Journey(JourneyType _type, bool _usingWrapper)
		: type(_type),
		  usingWrapper(_usingWrapper)
	{
		switch (_type) {
		case SPAWN_DIRECTLY:
			fillInStepsForSpawnDirectlyJourney();
			break;
		case START_PRELOADER:
			fillInStepsForPreloaderStartJourney();
			break;
		case SPAWN_THROUGH_PRELOADER:
			fillInStepsForSpawnThroughPreloaderJourney();
			break;
		default:
			P_BUG("Unknown journey type " << toString((int) _type));
			break;
		}
	}

	JourneyType getType() const {
		return type;
	}

	bool isUsingWrapper() const {
		return usingWrapper;
	}

	bool hasStep(JourneyStep step) const {
		Map::const_iterator it = steps.find(step);
		return it != steps.end();
	}

	const JourneyStepInfo &getStepInfo(JourneyStep step) const {
		Map::const_iterator it = steps.find(step);
		if (it == steps.end()) {
			throw RuntimeException("Invalid step " + journeyStepToString(step));
		}

		return it->second;
	}

	JourneyStep getFirstFailedStep() const {
		Map::const_iterator it, end = steps.end();
		for (it = steps.begin(); it != end; it++) {
			if (it->second.state == STEP_ERRORED) {
				return it->first;
			}
		}

		return UNKNOWN_JOURNEY_STEP;
	}

	void setStepNotStarted(JourneyStep step, bool force = false) {
		JourneyStepInfo &info = getStepInfoMutable(step);
		if (info.state == STEP_NOT_STARTED || info.state == STEP_IN_PROGRESS || force) {
			info.state = STEP_NOT_STARTED;
			info.beginTime = 0;
			info.endTime = 0;
		} else {
			throw RuntimeException("Unable to change state for journey step "
				+ journeyStepToString(step) + " because it wasn't already in progress");
		}
	}

	void setStepInProgress(JourneyStep step, bool force = false) {
		JourneyStepInfo &info = getStepInfoMutable(step);
		if (info.state == STEP_IN_PROGRESS) {
			return;
		} else if (info.state == STEP_NOT_STARTED || force) {
			info.state = STEP_IN_PROGRESS;
			// When `force` is true, we don't want to overwrite the previous endTime.
			if (info.endTime == 0) {
				info.beginTime =
					SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_10MSEC>();
			}
		} else {
			throw RuntimeException("Unable to change state for journey step "
				+ journeyStepToString(step)
				+ " because it was already in progress or completed");
		}
	}

	void setStepPerformed(JourneyStep step, bool force = false) {
		JourneyStepInfo &info = getStepInfoMutable(step);
		if (info.state == STEP_PERFORMED) {
			return;
		} else if (info.state == STEP_IN_PROGRESS || true) {
			info.state = STEP_PERFORMED;
			// When `force` is true, we don't want to overwrite the previous endTime.
			if (info.endTime == 0) {
				info.endTime =
					SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_10MSEC>();
				if (info.beginTime == 0) {
					info.beginTime = info.endTime;
				}
			}
		} else {
			throw RuntimeException("Unable to change state for journey step "
				+ journeyStepToString(step) + " because it wasn't already in progress");
		}
	}

	void setStepErrored(JourneyStep step, bool force = false) {
		JourneyStepInfo &info = getStepInfoMutable(step);
		if (info.state == STEP_ERRORED) {
			return;
		} else if (info.state == STEP_IN_PROGRESS || force) {
			info.state = STEP_ERRORED;
			// When `force` is true, we don't want to overwrite the previous endTime.
			if (info.endTime == 0) {
				info.endTime =
					SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_10MSEC>();
				if (info.beginTime == 0) {
					info.beginTime = info.endTime;
				}
			}
		} else {
			throw RuntimeException("Unable to change state for journey step "
				+ journeyStepToString(step) + " because it wasn't already in progress");
		}
	}

	void setStepBeginTime(JourneyStep step, MonotonicTimeUsec timestamp) {
		JourneyStepInfo &info = getStepInfoMutable(step);
		info.beginTime = timestamp;
	}

	void setStepEndTime(JourneyStep step, MonotonicTimeUsec timestamp) {
		JourneyStepInfo &info = getStepInfoMutable(step);
		info.endTime = timestamp;
	}

	void reset() {
		Map::iterator it, end = steps.end();
		for (it = steps.begin(); it != end; it++) {
			it->second.state = STEP_NOT_STARTED;
			it->second.beginTime = 0;
			it->second.endTime = 0;
		}
	}

	Json::Value inspectAsJson() const {
		Json::Value doc, steps;
		MonotonicTimeUsec monoNow = SystemTime::getMonotonicUsec();
		unsigned long long now = SystemTime::getUsec();

		doc["type"] = journeyTypeToString(type).toString();

		Map::const_iterator it, end = this->steps.end();
		for (it = this->steps.begin(); it != end; it++) {
			const JourneyStep step = it->first;
			const JourneyStepInfo &info = it->second;
			const JourneyStepInfo *nextStepInfo = NULL;
			if (info.nextStep != UNKNOWN_JOURNEY_STEP) {
				nextStepInfo = &this->steps.find(info.nextStep)->second;
			}
			steps[journeyStepToString(step).toString()] =
				info.inspectAsJson(nextStepInfo, monoNow, now);
		}
		doc["steps"] = steps;

		return doc;
	}
};


inline OXT_PURE StaticString
journeyTypeToString(JourneyType type) {
	switch (type) {
	case SPAWN_DIRECTLY:
		return P_STATIC_STRING("SPAWN_DIRECTLY");
	case START_PRELOADER:
		return P_STATIC_STRING("START_PRELOADER");
	case SPAWN_THROUGH_PRELOADER:
		return P_STATIC_STRING("SPAWN_THROUGH_PRELOADER");
	default:
		return P_STATIC_STRING("UNKNOWN_JOURNEY_TYPE");
	}
}

inline OXT_PURE StaticString
journeyStepToString(JourneyStep step) {
	switch (step) {
	case SPAWNING_KIT_PREPARATION:
		return P_STATIC_STRING("SPAWNING_KIT_PREPARATION");
	case SPAWNING_KIT_FORK_SUBPROCESS:
		return P_STATIC_STRING("SPAWNING_KIT_FORK_SUBPROCESS");
	case SPAWNING_KIT_CONNECT_TO_PRELOADER:
		return P_STATIC_STRING("SPAWNING_KIT_CONNECT_TO_PRELOADER");
	case SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER:
		return P_STATIC_STRING("SPAWNING_KIT_SEND_COMMAND_TO_PRELOADER");
	case SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER:
		return P_STATIC_STRING("SPAWNING_KIT_READ_RESPONSE_FROM_PRELOADER");
	case SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER:
		return P_STATIC_STRING("SPAWNING_KIT_PARSE_RESPONSE_FROM_PRELOADER");
	case SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER:
		return P_STATIC_STRING("SPAWNING_KIT_PROCESS_RESPONSE_FROM_PRELOADER");
	case SPAWNING_KIT_HANDSHAKE_PERFORM:
		return P_STATIC_STRING("SPAWNING_KIT_HANDSHAKE_PERFORM");
	case SPAWNING_KIT_FINISH:
		return P_STATIC_STRING("SPAWNING_KIT_FINISH");

	case PRELOADER_PREPARATION:
		return P_STATIC_STRING("PRELOADER_PREPARATION");
	case PRELOADER_FORK_SUBPROCESS:
		return P_STATIC_STRING("PRELOADER_FORK_SUBPROCESS");
	case PRELOADER_SEND_RESPONSE:
		return P_STATIC_STRING("PRELOADER_SEND_RESPONSE");
	case PRELOADER_FINISH:
		return P_STATIC_STRING("PRELOADER_FINISH");

	case SUBPROCESS_BEFORE_FIRST_EXEC:
		return P_STATIC_STRING("SUBPROCESS_BEFORE_FIRST_EXEC");
	case SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL:
		return P_STATIC_STRING("SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL");
	case SUBPROCESS_OS_SHELL:
		return P_STATIC_STRING("SUBPROCESS_OS_SHELL");
	case SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL:
		return P_STATIC_STRING("SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL");
	case SUBPROCESS_EXEC_WRAPPER:
		return P_STATIC_STRING("SUBPROCESS_EXEC_WRAPPER");
	case SUBPROCESS_WRAPPER_PREPARATION:
		return P_STATIC_STRING("SUBPROCESS_WRAPPER_PREPARATION");
	case SUBPROCESS_APP_LOAD_OR_EXEC:
		return P_STATIC_STRING("SUBPROCESS_APP_LOAD_OR_EXEC");
	case SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER:
		return P_STATIC_STRING("SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER");
	case SUBPROCESS_LISTEN:
		return P_STATIC_STRING("SUBPROCESS_LISTEN");
	case SUBPROCESS_FINISH:
		return P_STATIC_STRING("SUBPROCESS_FINISH");

	default:
		return P_STATIC_STRING("UNKNOWN_JOURNEY_STEP");
	}
}

inline OXT_PURE string
journeyStepToStringLowerCase(JourneyStep step) {
	StaticString stepString = journeyStepToString(step);
	DynamicBuffer stepStringLcBuffer(stepString.size());
	convertLowerCase((const unsigned char *) stepString.data(),
		(unsigned char *) stepStringLcBuffer.data, stepString.size());
	return string(stepStringLcBuffer.data, stepString.size());
}

inline OXT_PURE StaticString
journeyStepStateToString(JourneyStepState state) {
	switch (state) {
	case STEP_NOT_STARTED:
		return P_STATIC_STRING("STEP_NOT_STARTED");
	case STEP_IN_PROGRESS:
		return P_STATIC_STRING("STEP_IN_PROGRESS");
	case STEP_PERFORMED:
		return P_STATIC_STRING("STEP_PERFORMED");
	case STEP_ERRORED:
		return P_STATIC_STRING("STEP_ERRORED");
	default:
		return P_STATIC_STRING("UNKNOWN_JOURNEY_STEP_STATE");
	}
}

inline OXT_PURE JourneyStepState
stringToJourneyStepState(const StaticString &value) {
	if (value == P_STATIC_STRING("STEP_NOT_STARTED")) {
		return STEP_NOT_STARTED;
	} else if (value == P_STATIC_STRING("STEP_IN_PROGRESS")) {
		return STEP_IN_PROGRESS;
	} else if (value == P_STATIC_STRING("STEP_PERFORMED")) {
		return STEP_PERFORMED;
	} else if (value == P_STATIC_STRING("STEP_ERRORED")) {
		return STEP_ERRORED;
	} else {
		return UNKNOWN_JOURNEY_STEP_STATE;
	}
}


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_HANDSHAKE_JOURNEY_H_ */
