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
var mongodb;

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
	origParent["_passenger_wrapped_" + functionName] = originalFn;

	origParent[functionName] = function() {
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
	var query = "";
	for (var i = 0; i < origArguments.length; i++) {
		if (typeof(origArguments[i]) != 'function') { // mongoskin
			query += (i > 0 ? "," : "") + JSON.stringify(origArguments[i]);
		}
	}
	query = "'" + databaseName + "'.collection['" + collectionName + "']." + functionName + "(" + query + ")";

	var tBegin = ustReporter.nowTimestamp();
	var rval = originalFn.apply(this, origArguments);
	var tEnd = ustReporter.nowTimestamp();

	log.verbose("==== Instrumentation [MongoDB] ==== [" + query + "] (attach to txnId " + ustReporter.getCurrentTxnId() + ")");

	ustReporter.logTimedActivityMongo("mongo: " + query, tBegin, tEnd, query);

	return rval;
}

exports.initPreLoad = function() {
	log = ustReporter.getPassengerLogger();
	var appRoot = ustReporter.getApplicationRoot();

	// See if the mongodb driver is used. It can also be used through mongoskin, in which case older mongoskin
	// versions will have it as part of their own node_modules.
	try {
		mongodb = require(appRoot + "/node_modules/mongodb");
	} catch (e1) {
		try {
			mongodb = require(appRoot + "/node_modules/mongoskin/node_modules/mongodb");
		} catch (e2) {
			log.verbose("Not instrumenting MongoDB (probably not used): (default) " + e1 + ", (mongoskin) " + e2);
			return;
		}
	}

	log.info("==== Instrumentation [MongoDB] ==== initialize");

	// The 1.4 mongo driver series uses a callback mechanism that breaks continuation-local-storage.
	wrapRepairCLSMongo14();

	// Newer mongoskin techniques break continuation-local-storage, so we need to skin the skinner there.
	wrapRepairCLSMongoskinUtils(appRoot);

	try {
		for (var i = 0; i < collectionMethods.length; i++) {
			instrumentCollectionMethod(mongodb.Collection.prototype, collectionMethods[i], collectionFn);
		}
	} catch (e) {
		log.error("Unable to instrument MongoDB due to error: " + e);
	}
};

function wrapRepairCLSMongo14() {
	try {
		if (!mongodb.Db.prototype._executeQueryCommand || mongodb.Db.prototype._executeInsertCommand) {
			log.verbose("Not using MongoDB 1.4.x, so don't need MongoDB continuation-local-storage workaround");
			return;
		}

		mongodb.Db.prototype._passenger_wrapped__executeQueryCommand = mongodb.Db.prototype._executeQueryCommand;
		mongodb.Db.prototype._executeQueryCommand = function() {
			if (arguments.length > 0 && typeof(arguments[arguments.length - 1]) === 'function') {
				var callback = ustReporter.getCLSWrappedCallback(arguments[arguments.length - 1]);
				var newArgs = [];
				for (var i = 0; i < arguments.length - 1; i++) {
					newArgs.push(arguments[i]);
				}
				newArgs.push(callback);
				this._passenger_wrapped__executeQueryCommand.apply(this, newArgs);
			} else {
				this._passenger_wrapped__executeQueryCommand.apply(this, arguments);
			}
		};

		mongodb.Db.prototype._passenger_wrapped__executeInsertCommand = mongodb.Db.prototype._executeInsertCommand;
		mongodb.Db.prototype._executeInsertCommand = function() {
			if (arguments.length > 0 && typeof(arguments[arguments.length - 1]) === 'function') {
				var callback = ustReporter.getCLSWrappedCallback(arguments[arguments.length - 1]);
				var newArgs = [];
				for (var i = 0; i < arguments.length - 1; i++) {
					newArgs.push(arguments[i]);
				}
				newArgs.push(callback);
				this._passenger_wrapped__executeInsertCommand.apply(this, newArgs);
			} else {
				this._passenger_wrapped__executeInsertCommand.apply(this, arguments);
			}
		};
		log.verbose("Using MongoDB 1.4.x continuation-local-storage workaround");
	} catch (e) {
		log.error("Not using MongoDB continuation-local-storage workaround: " + e);
	}
}

// Mongoskin utils creates skin (wrapper) classes and in the process introduce an emitter for supporting
// delayed open() (e.g. while the driver is still connecting).
// The emitter breaks continuation-local-storage, but since it is for top-level classes (Db, Collection, etc.)
// we can't bind it per request, so we need to bind the individual callbacks that get pushed onto it.
function wrapRepairCLSMongoskinUtils(appRoot) {
	var mongoskinUtils;
	try {
		mongoskinUtils = require(appRoot + "/node_modules/mongoskin/lib/utils");
	} catch (e) {
		log.verbose("Not using mongoskin continuation-local-storage workaround (either not used, old version, or new unsupported version): " + e);
		return;
	}

	try {
		// makeSkinClass is a factory, so need a double wrap: one to get the run-time factory output (skinClass),
		// and then one that hooks the actual method in that output.
		mongoskinUtils._passenger_wrapped_makeSkinClass = mongoskinUtils.makeSkinClass;
		mongoskinUtils.makeSkinClass = function(NativeClass, useNativeConstructor) {
			var skinClass = mongoskinUtils._passenger_wrapped_makeSkinClass(NativeClass, useNativeConstructor);

			skinClass.prototype._passenger_wrapped_open = skinClass.prototype.open;
			skinClass.prototype.open = function(callback) {
				// Finally we can bind the callback so that when the emitter calls it, the cls is mapped correctly.
				return skinClass.prototype._passenger_wrapped_open.call(this, ustReporter.getCLSWrappedCallback(callback));
			};

			return skinClass;
		};
		log.verbose("Using mongoskin continuation-local-storage workaround");
	} catch (e) {
		log.error("Not using mongoskin continuation-local-storage workaround (probably an unsupported version): " + e);
	}
}

exports.initPostLoad = function() {
	//if (!mongodb) {
	//	return;
	//}
};

