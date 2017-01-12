/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion
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

var ustReporter = global.phusion_passenger_ustReporter;

var log;
var express;

var applicationThis;

exports.initPreLoad = function() {
	log = ustReporter.getPassengerLogger();
	var appRoot = ustReporter.getApplicationRoot();

	try {
		express = require(appRoot + "/node_modules/express");
	} catch (e) {
		// express not present, no need to instrument.
		log.debug("Not instrumenting Express (probably not used): " + e);
		return;
	}

	try {
		log.info("==== Instrumentation [Express] ==== initialize");
		log.debug("hook application.init, to be the first in the use() line..");

		express.application.initOrig = express.application.init;
		express.application.init = function() {
				log.debug("Express application.init() called, chain and then be the first to use()..");
				var rval = express.application.initOrig.apply(this, arguments);

				this.use(logRequest);

				applicationThis = this; // store for initPostLoad use

				return rval;
			};

		log.debug("Express tap: application.use, to be as late as possible in the use() line, but before any other error handlers..");
		express.application.useOrig = express.application.use;
		express.application.use = function() {
			// Express recognizes error handlers by #params = 4
			if (arguments[0].length == 4) {
				express.application.useOrig.call(this, logException);
			}

			return express.application.useOrig.apply(this, arguments);
		};
	} catch (e) {
		log.error("Unable to instrument Express due to error: " + e);
	}
};

exports.initPostLoad = function() {
	if (!express) {
		return;
	}

	log.debug("add final error handler..");
	try {
		if (applicationThis) {
			express.application.useOrig.call(applicationThis, logException);
		}
	} catch (e) {
		log.error("Unable to instrument Express error flow due to error: " + e);
	}
};

function logRequest(req, res, next) {
	log.verbose("==== Instrumentation [Express] ==== REQUEST [" + req.method + " " + req.url + "] attach");
	ustReporter.attachToRequest(req, res, next);
}

function logException(err, req, res, next) {
	// We may have multiple exception handlers in the routing chain, ensure only the first one actually logs.
	if (!res.hasLoggedException) {
		log.verbose("==== Instrumentation [Express] ==== EXCEPTION + TRACE FOR [" + req.url + "]");

		ustReporter.logException(err.name, err.message, err.stack);

		res.hasLoggedException = true;
	}
	next(err);
}
