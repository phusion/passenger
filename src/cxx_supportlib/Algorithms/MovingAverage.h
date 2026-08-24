/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2026 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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

namespace Passenger {

/**
 * Calculates an exponential moving average. `alpha` determines how much weight the
 * current value has compared to the previous average. Higher values of `alpha`
 * cause the current value to have more weight (and thus the previous average
 * to decay more quickly), lower values have the opposite effect.
 *
 * This algorithm is not timing sensitive: it doesn't take into account gaps in the
 * data over time, and treats all values equally regardless of when the value was
 * collected.
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
