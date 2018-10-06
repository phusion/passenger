/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPEED_METER_H_
#define _PASSENGER_SPEED_METER_H_

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <limits>
#include <SystemTools/SystemTime.h>

namespace Passenger {

/**
 * Utility class which, when periodically fed with quantities, measures the
 * rate of change in the most recent time period. It does this by storing
 * samples of the most recent quantities, calculating a weighted average of
 * the differences between each sample, and extrapolating that over a time
 * period.
 *
 * For example,
 *
 *  * When periodically given the number of miles a car has driven so far,
 *    the average speed of the most recent 10 minutes can be calculated.
 *  * When periodically given the number of processes the OS has created
 *    so far, the average fork rate (per minute) of the most recent minute
 *    can be calculated.
 *
 * SpeedMeter is designed to use as little memory as possible, and designed
 * so that samples can be taken at erratic intervals. This is why it limits
 * the number of samples that can be stored in memory, and why it puts time
 * limits on the samples.
 *
 *
 * ## Template parameters
 *
 * ### `maxSamples`
 *
 * `maxSamples` dictates the sample buffer size: the maximum number of samples
 * to hold in memory. Defaults to 8.
 *
 * The more samples you have, the more historical data influences the results.
 * If you have few samples then recent data have the most impact on the results.
 *
 * ### `minAge`
 *
 * A new sample is only accepted if at least `minAge` microseconds
 * have passed since the last sample. This is to ensure that the sample
 * buffer contains enough historical data so that results are not skewed by
 * adding samples too quickly. A full sample buffer is guaranteed to cover
 * the last `minAge * maxSamples` microseconds.
 *
 * Defaults to 1 second (1 000 000 usec).
 *
 * To see why this is useful, consider when `maxSamples` is 5 and `minAge` is 0.
 * If you measure the number of processes 5 times with 10 ms in between,
 * then it's unlikely that the OS has spawned any processes in that 50 ms time
 * interval. However you've now filled the sample buffer with only data from
 * those 5 recent measurements, so SpeedMeter thinks the rate of change is 0.
 * If you set `minAge` to, say, 5 seconds, then the sample buffer will be updated
 * at most once every 5 seconds, guaranteeing that it can retain historical data
 * for the last 25 seconds.
 *
 * `minAge * maxSamples` should be larger than `window`.
 *
 * ### `maxAge`
 *
 * When calculating the result, samples older than `maxAge` usec ago are ignored.
 * This value should be larger than `window`. Defaults to 1 minute (60 000 000
 * usec).
 *
 * ### `window`
 *
 * When calculating the result, this value dictates the time interval over which
 * the rate of change should be calculated. Defaults to 1 second (1 000 000 usec),
 * meaning that by default the result describes the speed per second.
 *
 *
 * ## Interpreting the parameters
 *
 * Given parameters, the behavior of SpeedMeter can be interpreted as follows:
 *
 * All samples that have been collected in the last `minAge * maxSamples` usecs
 * (or the last `maxAge` usecs, if samples have been collected slowly), are used for
 * calculating the speed per `window` usecs.
 *
 * Thus, given the default parameter values, we can say:
 *
 * All samples that have been collected in the last 8 seconds (the last 1 minute,
 * if samples have been collected slowly), are used for calculating the speed
 * per second.
 */
template<
	typename ValueType = double,
	unsigned int maxSamples = 8,
	unsigned int minAge = 1000000,
	unsigned int maxAge = 60 * 1000000,
	unsigned int window = 1000000
>
class SpeedMeter {
private:
	struct Sample {
		unsigned long long timestamp;
		ValueType val;

		Sample()
			: timestamp(0),
			  val(0)
			{ }
	};

	unsigned short start, count;
	Sample samples[maxSamples];

	static int mod(int a, int b) {
		int r = a % b;
		return r < 0 ? r + b : r;
	}

	const Sample &getSample(int index) const {
		return samples[mod(start + index, maxSamples)];
	}

	const Sample &getLastSample() const {
		return getSample(count - 1);
	}

	unsigned long long getTimeThreshold() const {
		const unsigned long long currentTime = SystemTime::getUsec();
		if (currentTime > maxAge) {
			return currentTime - maxAge;
		} else {
			return 0;
		}
	}

	void resetOnClockSkew(unsigned long long timestamp) {
		if (getLastSample().timestamp > timestamp) {
			for (unsigned int i = 0; i < maxSamples; i++) {
				samples[i] = Sample();
			}
		}
	}

public:
	SpeedMeter()
		: start(0),
		  count(0)
		{ }

	bool addSample(ValueType val, unsigned long long timestamp = 0) {
		if (timestamp == 0) {
			timestamp = SystemTime::getUsec();
		}

		const Sample &lastSample = getLastSample();
		resetOnClockSkew(timestamp);

		if (lastSample.timestamp <= timestamp - minAge) {
			Sample &nextSample = samples[(start + count) % maxSamples];
			nextSample.timestamp = timestamp;
			nextSample.val = val;
			if (count == maxSamples) {
				start = (start + 1) % maxSamples;
			} else {
				count++;
			}
			return true;
		} else {
			return false;
		}
	}

	/** Number of items in the sample buffer. */
	unsigned int size() const {
		return count;
	}

	/** Current speed over the configured window. Returns unknownSpeed() if less than 2
	 * samples have been collected so far.
	 */
	double currentSpeed() const {
		const unsigned long long timeThreshold = getTimeThreshold();
		int begin = 0, i;
		unsigned long long interval;
		double avgWeight = 0, sum = 0;

		// Ignore samples that are too old.
		while (begin < (int) count - 1
		    && getSample(begin).timestamp <= timeThreshold)
		{
			begin++;
		}

		// Given the deltas of each sample and the next sample,
		// calculate the weighted average of all those deltas,
		// with time interval of each delta as the weight,
		// and calculate the average weight.
		for (i = begin; i < (int) count - 1; i++) {
			const Sample &cur  = getSample(i);
			const Sample &next = getSample(i + 1);
			ValueType delta = next.val - cur.val;
			unsigned long long weight = next.timestamp - cur.timestamp;
			sum += (double) delta * weight;
			avgWeight += weight;
		}
		avgWeight /= std::max(1, (int) count - 1 - begin);

		interval = getSample((int) count - 1).timestamp - getSample(begin).timestamp;
		if (interval > 0) {
			// sum / interval is the speed per average delta interval,
			// so we extrapolate that over the entire window interval.
			return (sum / interval) * (window / avgWeight);
		} else {
			return unknownSpeed();
		}
	}

	static ValueType unknownSpeed() {
		return numeric_limits<ValueType>::max();
	}

	#if 0
		void debug() const {
			const unsigned long long timeThreshold = getTimeThreshold();

			printf("---- begin debug ----\n");
			printf("timeThreshold: %llu\n", timeThreshold);
			for (int i = 0; i < (int) maxSamples; i++) {
				const Sample &sample = samples[i];
				printf("elem %u, sample %u: timestamp = %llu, val = %.0f%s%s%s\n",
					i,
					mod(i - start, maxSamples),
					sample.timestamp,
					(double) sample.val,
					(sample.timestamp <= timeThreshold) ? "  X" : "",
					(i == start) ? "  <-- start" : "",
					(count > 0 && i == mod((int) start + count - 1, maxSamples)) ? "  <-- last"   : "");
			}
			printf("---- end debug ----\n");
		}
	#endif
};

} // namespace Passenger

#endif /* _PASSENGER_SPEED_METER_H_ */
