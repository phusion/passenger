/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_ALGORITHMS_EXP_MOVING_AVERAGE_H_
#define _PASSENGER_ALGORITHMS_EXP_MOVING_AVERAGE_H_

#include <oxt/macros.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <utility>
#include <cmath>

namespace Passenger {

using namespace std;


/**
 * Implements discontiguous exponential moving averaging, as described by John C. Gunther
 * 1998. Can be used to compute moving exponentially decaying averages and standard
 * deviations. Unlike normal exponential moving average, this algorithm also works when
 * the data has gaps, and it also avoids initial value bias and postgap bias. See
 * http://www.drdobbs.com/tools/discontiguous-exponential-averaging/184410671
 *
 * ## Template parameters
 *
 * ### alpha
 *
 * Specifies by what factor data should decay. Its range is [0, 1000]. Higher values
 * cause the current value to have more weight (and thus the previous average
 * to decay more quickly), lower values have the opposite effect.
 *
 * ### alphaTimeUnit
 *
 * Specifies the time, in microseconds, after which the data should decay
 * by a factor of exactly `alpha`. For example, if `alpha = 0.5` and `alphaTimeUnit = 2000000`,
 * then data decays by 0.5 per 2 seconds.
 *
 * The default value is 1 second.
 *
 * ### maxAge
 *
 * Represents an educational guess as to how long (in microseconds) it takes
 * for the sampled data sequence to change significantly. If you don't expect large random
 * variations then you should set this to a large value. For a data sequence dominated by
 * large random variations, setting this to 1000000 (1 second) might be appropriate.
 *
 * If the time interval between updates is `dt`, using a `maxAge` of `N * dt` will cause
 * each update to fill in up to `N - 1` of any preceeding skipped updates with the current
 * data value.
 */
template<
	unsigned int alpha,
	unsigned long long alphaTimeUnit = 1000000,
	unsigned long long maxAge = 1000000
>
class DiscExpMovingAverage {
private:
	template<unsigned int, unsigned long long, unsigned long long>
	friend class DiscExpMovingAverageWithStddev;

	double sumOfWeights, sumOfData;
	unsigned long long prevTime;

	static BOOST_CONSTEXPR double floatingAlpha() {
		return alpha / 1000.0;
	}

	static BOOST_CONSTEXPR double newDataWeightUpperBound() {
		return pow(floatingAlpha(), maxAge / (double) alphaTimeUnit);
	}

	pair<double, double> internalUpdate(double value, unsigned long long now) {
		double weightReductionFactor = pow(1 - floatingAlpha(),
			(now - prevTime) / (double) alphaTimeUnit);
		double newDataWeight = std::min(1 - weightReductionFactor,
			newDataWeightUpperBound());
		sumOfWeights = weightReductionFactor * sumOfWeights + newDataWeight;
		sumOfData = weightReductionFactor * sumOfData + newDataWeight * value;
		prevTime = now;
		return make_pair(weightReductionFactor, newDataWeight);
	}

public:
	DiscExpMovingAverage(unsigned long long _prevTime = 0)
		: sumOfWeights(0),
		  sumOfData(0),
		  prevTime(_prevTime)
		{ }

	void update(double value, unsigned long long now) {
		if (OXT_LIKELY(now > prevTime)) {
			internalUpdate(value, now);
		}
	}

	bool available() const {
		return sumOfWeights > 0;
	}

	double completeness(unsigned long long now) const {
		return pow(floatingAlpha(), now - prevTime) * sumOfWeights;
	}

	double average() const {
		return sumOfData / sumOfWeights;
	}

	double average(unsigned long long now) const {
		DiscExpMovingAverage<alpha, alphaTimeUnit, maxAge> copy(*this);
		copy.update(0, now);
		return copy.average();
	}
};


/**
 * Like DescExpMovingAverage, but also keeps track of the standard deviation.
 */
template<
	unsigned int alpha,
	unsigned long long alphaTimeUnit = 1000000,
	unsigned long long maxAge = 1
>
class DiscExpMovingAverageWithStddev {
private:
	DiscExpMovingAverage<alpha, alphaTimeUnit, maxAge> dema;
	double sumOfSquaredData;

public:
	DiscExpMovingAverageWithStddev(unsigned long long prevTime = 0)
		: dema(prevTime),
		  sumOfSquaredData(0)
		{ }

	void update(double value, unsigned long long now) {
		if (OXT_UNLIKELY(now <= dema.prevTime)) {
			return;
		}

		pair<double, double> p = dema.internalUpdate(value, now);
		double weightReductionFactor = p.first;
		double newDataWeight = p.second;
		sumOfSquaredData = weightReductionFactor * sumOfSquaredData
			+ newDataWeight * pow(value, 2.0);
	}

	bool available() const {
		return dema.available();
	}

	double completeness(unsigned long long now) const {
		return dema.completeness(now);
	}

	double average() const {
		return dema.average();
	}

	double average(unsigned long long now) const {
		return dema.average(now);
	}

	double stddev() const {
		return sqrt(sumOfSquaredData / dema.sumOfWeights - pow(average(), 2));
	}

	double stddev(unsigned long long now) const {
		DiscExpMovingAverageWithStddev<alpha, alphaTimeUnit, maxAge> copy(*this);
		copy.update(0, now);
		return sqrt(copy.sumOfSquaredData / copy.sumOfWeights - pow(copy.average(), 2));
	}
};


/**
 * Calculates an exponential moving average. `alpha` determines how much weight the
 * current value has compared to the previous average. Higher values of `alpha`
 * cause the current value to have more weight (and thus the previous average
 * to decay more quickly), lower values have the opposite effect.
 *
 * This algorithm is not timing sensitive: it doesn't take into account gaps in the
 * data over time, and treats all values equally regardless of when the value was
 * collected. See also DiscExpMovingAverage.
 *
 * You should initialize the the average value with a value equal to `nullValue`.
 * If `prevAverage` equals `nullValue` then this function simply returns `currentValue`.
 */
inline double
expMovingAverage(double prevAverage, double currentValue, double alpha, double nullValue = -1) {
	if (OXT_UNLIKELY(prevAverage == nullValue)) {
		return currentValue;
	} else {
		return alpha * currentValue + (1 - alpha) * prevAverage;
	}
}


} // namespace Passenger

#endif /* _PASSENGER_ALGORITHMS_EXP_MOVING_AVERAGE_H_ */
