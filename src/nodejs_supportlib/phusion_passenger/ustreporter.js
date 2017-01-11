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

var codify = require('vendor-copy/codify');

var reqNamespace = require('vendor-copy/continuation-local-storage').getNamespace('passenger-request-ctx');

// Assigned by init()
var ustLog;
var log;
var appRoot;

/**
 * @return logger set to Passenger's loglevel. For example, log.error("") maps to Passenger loglevel 1. All mappings:
 * 1: error
 * 2: warn
 * 3: notice
 * 4: info
 * 5: verbose
 * 6: debug
 * 7: silly
 * (other levels are mapped to critical)
 */
exports.getPassengerLogger = function() {
	return log;
};

/**
 * @return the application root path, needed for instrumenting an application's node modules.
 */
exports.getApplicationRoot = function() {
	return appRoot;
};

/**
 * @return timestamp in microseconds to be used as the start or end timestamp for timed logging (monotonic clock, not wall clock).
 */
exports.nowTimestamp = function() {
	var secAndUsec = process.hrtime();
	return Math.round((secAndUsec[0] * 1e6) + (secAndUsec[1] / 1e3));
};

/**
 * All Activity logs will be dropped unless they are done after this method, from within an execution chain starting with the callback. So it is
 * essential to call this function whenever you intercept a request, before code that might want to log can be reached. The function adds context
 * to the execution chain such that future logs can be correctly appended to the currently open request log, also for modules that don't
 * have access to the request object (such as database drivers).
 *
 * @param callback
 *			The "next" handler in line for processing the request. Logging will only work from this handler or within the same execution chain!
 */
exports.attachToRequest = function(request, response, callback) {
	try {
		log.debug("ustReporter: attachToRequest(" + request.method + " " + request.url + ")");
		var attachToTxnId = request.headers['!~passenger-txn-id'];
		if (!attachToTxnId) {
			log.warn("Dropping Union Station request log due to lack of txnId from Passenger Core (probably a temporary UstRouter failure)");
			return callback();
		}

		reqNamespace.bindEmitter(request);
		reqNamespace.bindEmitter(response);

		// The Passenger core has an open transaction associated with the request, to which we can attach info from node instrumentation.
		// However, logToUstTransaction() communicates async with the ustrouter, and is not guaranteed to deliver before the application response arrives
		// back to the core (at which point the core will close the transaction and later additions will not be taken into account).
		// That's why we intercept response.end() (from the doc: the method, response.end(), MUST be called on each response), so we can defer it
		// until we are sure the ustrouter is aware of any attachments generated during the request handling.
		response._passenger_wrapped_end = response.end;
		response.end = function() {
			return ustLog.deferIfPendingTxns(attachToTxnId, this, response._passenger_wrapped_end, arguments);
		};

		// Make request transaction ID available for other instrumentation modules, e.g. mongo doesn't know about requests (which is how the core passes
		// txn ID).
		reqNamespace.run(function() {
			reqNamespace.set("attachToTxnId", attachToTxnId);
			callback();
		});
	} catch (e) {
		log.error("Dropping Union Station request log due to error:\n" + e.stack);
	}
};

/**
 * Log a timed block, described by activityName, with an optional message to display (e.g. in a mouseover).
 */
exports.logTimedActivityGeneric = function(activityName, tBegin, tEnd, message) {
	logTimedActivity(activityName, tBegin, tEnd, "generic", message ? { "message": message } : undefined);
};

/**
 * Log a timed mongo database interaction block, described by activityName, with an optional query to display (e.g. in a mouseover).
 */
exports.logTimedActivityMongo = function(activityName, tBegin, tEnd, query) {
	logTimedActivity(activityName, tBegin, tEnd, "mongo", query ? { "query": query } : undefined);
};

/**
 * Log a timed SQL database interaction block, described by activityName, with an optional query to display (e.g. in a mouseover).
 */
exports.logTimedActivitySQL = function(activityName, tBegin, tEnd, query) {
	logTimedActivity(activityName, tBegin, tEnd, "sql", query ? { "query": query } : undefined);
};

/**
 * Internal, base for the public logTimedActivity...()
 * @param activityName
 *			the name to display for this activity
 * @param tBegin
 *			activity start timestamp, acquired using .nowTimestamp()
 * @param tEnd
 *			activity end timestamp, acquired using .nowTimestamp()
 * @param dataType
 *			the type of this activity, and the data in the dataObj; assumed to be one of "generic", "sql", "mongo", "view"
 * @param dataObj
 *			optional; usually content for a mouseover
 */
function logTimedActivity(activityName, tBegin, tEnd, dataType, dataObj) {
	try {
		log.debug("ustReporter: logTimedActivity(activityName: " + activityName + ")");
		if (!activityName || !tBegin || !tEnd) {
			log.error("ustReporter: logTimedActivity is missing name or begin/end timestamp, dropping.");
			return;
		}

		var attachToTxnId = getCurrentTxnId();
		if (!attachToTxnId) {
			log.warn("Dropping Union Station timed action log due to lack of txnId to attach to " +
				"(either request was not intercepted, cls context lost, or temporary UstRouter failure).\nCall stack: " + (new Error().stack));
			return;
		}

		var uniqueTag = codify.toCode(tBegin);
		var extraInfo;
		if (!dataType) {
			extraInfo = JSON.stringify({ "name": activityName });
		} else {
			extraInfo = JSON.stringify({ "name": activityName, "data_type": dataType, "data": dataObj });
		}
		var logBuf = [];
		logBuf.push("BEGIN: " + uniqueTag + " (" + codify.toCode(tBegin) + ") " + new Buffer(extraInfo).toString('base64'));
		logBuf.push("END: " + uniqueTag + " (" + codify.toCode(tEnd) + ")");
		ustLog.logToUstTransaction("requests", logBuf, attachToTxnId);
	} catch (e) {
		log.error("Dropping Union Station timed action log due to error:\n" + e.stack);
	}
}

/**
 * For logging intercepted exceptions. If the exception occurred in relation to a request (i.e. within an execution chain from the attachToRequest
 * callback), the exception will be associated with that request, otherwise it's sent as a standalone.
 *
 * @param name
 *			E.g. "NameError"
 * @param message
 *			E.g. "undefined local variable or method for .."
 * @param trace
 *			Backtrace
 */
exports.logException = function(name, message, trace) {
	try {
		log.debug("ustReporter: logException(name: " + name + ", message: " + message + ")");
		var logBuf = [];

		var requestTxnId = getCurrentTxnId();
		if (requestTxnId) {
			logBuf.push("Request transaction ID: " + requestTxnId);
		}

		logBuf.push("Message: " + new Buffer(message).toString('base64'));
		logBuf.push("Class: " + name);
		logBuf.push("Backtrace: " + new Buffer(trace).toString('base64'));

		ustLog.logToUstTransaction("exceptions", logBuf);
	} catch (e) {
		log.error("Dropping Union Station exception log due to error:\n" + e.stack);
	}
};

/**
 * Get the current request transaction id necessary to append logs to it. This is normally done automatically, but some modules break automatic attachment
 * in the execution chain (implemented by continuation-local-storage). This method can be used to trace until at what point the transaction id gets
 * lost, and patch that place with .getCLSWrappedCallback().
 */
function getCurrentTxnId() {
	return reqNamespace.get("attachToTxnId");
}
exports.getCurrentTxnId = getCurrentTxnId;

/**
 * For patching callback systems that are incompatible with continuation-local-storage.
 */
exports.getCLSWrappedCallback = function(origCallback) {
	return reqNamespace.bind(origCallback);
};

/**
 * For internal use. Called by Passenger loader, no need to call from anywhere else.
 */
exports.init = function(logger, applicationRoot, ustLogger) {
	log = logger;
	appRoot = applicationRoot;
	ustLog = ustLogger;
};