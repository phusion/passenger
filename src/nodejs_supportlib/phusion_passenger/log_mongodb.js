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

var codify = require('codify');
var microtime = require('microtime');

var getNamespace = require('continuation-local-storage').getNamespace;
var clStore = getNamespace('passenger-request-ctx');

var mongodb;
var ustLog;

// From http://docs.mongodb.org/manual/reference/method/js-collection/
var collectionMethods = [
	"aggregate", "count", "copyTo", "createIndex", "dataSize", "distinct", "drop", "dropIndex", "dropIndexes",
	"ensureIndex", "explain", "find", "findAndModify", "findOne", "getIndexes", "getShardDistribution", "getShardVersion",
	"group", "insert", "isCapped", "mapReduce", "reIndex", "remove", "renameCollection", "save", "stats",
	"storageSize", "totalSize", "totalIndexSize", "update", "validate"
];

function instrumentCollectionMethod(origParent, functionName, newFn) {
	var originalFn = origParent[functionName];

	// not used yet (unwrapping N/I)
	origParent["_passenger_wrapped_original_" + functionName] = originalFn;

	origParent[functionName] = function() {
		//console.log("INTERCEPTED COLLECTION FUNCTION CALLED:" + require('util').inspect(this, true, null));

		//console.log("NAME: " + clStore.get("attachToTxnId"));

		//console.log("THIS: " + require('util').inspect(this));
		//console.log("ARGS: " + require('util').inspect(arguments));
		//console.log("CLSTORE: " + clStore.get("attachToTxnId"));

		var databaseName;
		var collectionName;

		if (this.s) {
			// Plain driver
			databaseName = this.s.dbName;
			collectionName = this.s.name;
		} else if (this.db) {
			// Mongoskin 1.4.13
			databaseName = this.db.databaseName;
			collectionName = this.collectionName;
		} else if (this._collection_args) {
			// Mongoskin driver
			databaseName = this._skin_db._connect_args[0];
			collectionName = this._collection_args[0];
		}

		return newFn.call(this, arguments, databaseName, collectionName, functionName, originalFn);
	};
}

function collectionFn(origArguments, databaseName, collectionName, functionName, originalFn) {
	var friendlyName = databaseName + "." + collectionName + "." + functionName + "(..)";
	var query = "";
	for (i = 0; i < origArguments.length; i++) {
		if (typeof(origArguments[i]) != 'function') { // mongoskin
			query += (i > 0 ? "," : "") + JSON.stringify(origArguments[i]);
		}
	}
	query = "'" + databaseName + "'.collection['" + collectionName + "']." + functionName + "(" + query + ")";

	var tBegin = microtime.now();
	var rval = originalFn.apply(this, origArguments);
	var tEnd = microtime.now();

	var attachToTxnId = clStore.get("attachToTxnId");
	console.log("==== Instrumentation [MongoDB] ==== [" + query + "] (attach to txnId " + attachToTxnId + ")");

	var logBuf = [];
	var uniqueTag = codify.toCode(tBegin);
	logBuf.push("BEGIN: DB BENCHMARK: mongodb " + uniqueTag + " (" + codify.toCode(tBegin) + ") " + new Buffer(friendlyName + "\n" + query).toString('base64'));
	logBuf.push("END: DB BENCHMARK: mongodb " + uniqueTag + " (" + codify.toCode(tEnd) + ")");

	ustLog.logToUstTransaction("requests", logBuf, attachToTxnId);

	return rval;
}

exports.initPreLoad = function(appRoot, ustLogger) {
	ustLog = ustLogger;

	try {
		mongodb = require(appRoot + "/node_modules/mongodb");
	} catch (e) {
		console.log("MongoDB instrumentation error: " + e);
	}

	try {
		if (!mongodb) {
			// !!!
			mongodb = require(appRoot + "/node_modules/mongoskin/node_modules/mongodb");
		}
	} catch (e) {
		console.log("MongoDB instrumentation error: " + e);
	}

	try {
		console.log("==== Instrumentation [MongoDB] ====");

		//mongodb.Db.prototype.collectionOrig = mongodb.Db.prototype.collection;
		//mongodb.Db.prototype.collection = function() {
		//	return mongodb.Db.prototype.collectionOrig.apply(this, arguments);
		//}

		// !!!
		// mongo driver luistert op de socket in een eigen continuation.
		// de loop daarvan roept callbacks aan die gepushed zijn aan een array.
		// dus clStore.bind() is nodig.
		//
		// dit verklaart ook waarom we soms 2 verschillende txnIds zagen in dezelfde
		// request. de connectie was gemaakt in een vorig request met een andere txnId.
		mongodb.Db.prototype._origExecuteQueryCommand = mongodb.Db.prototype._executeQueryCommand;
		mongodb.Db.prototype._executeQueryCommand = function() {
			if (arguments.length > 0 && typeof(arguments[arguments.length - 1]) === 'function') {
				var callback = clStore.bind(arguments[arguments.length - 1]);
				var newArgs = [];
				for (var i = 0; i < arguments.length - 1; i++) {
					newArgs.push(arguments[i]);
				}
				newArgs.push(callback);
				this._origExecuteQueryCommand.apply(this, newArgs);
			} else {
				this._origExecuteQueryCommand.apply(this, arguments);
			}
		}

		mongodb.Db.prototype._origExecuteInsertCommand = mongodb.Db.prototype._executeInsertCommand;
		mongodb.Db.prototype._executeInsertCommand = function() {
			if (arguments.length > 0 && typeof(arguments[arguments.length - 1]) === 'function') {
				var callback = clStore.bind(arguments[arguments.length - 1]);
				var newArgs = [];
				for (var i = 0; i < arguments.length - 1; i++) {
					newArgs.push(arguments[i]);
				}
				newArgs.push(callback);
				this._origExecuteInsertCommand.apply(this, newArgs);
			} else {
				this._origExecuteInsertCommand.apply(this, arguments);
			}
		}

		for (i = 0; i < collectionMethods.length; i++) {
			instrumentCollectionMethod(mongodb.Collection.prototype, collectionMethods[i], collectionFn);
		}
	} catch (e) {
		console.log("MongoDB instrumentation error: " + e);
	}
}

exports.initPostLoad = function() {
	//if (!mongodb) {
	//	return;
	//}
}

