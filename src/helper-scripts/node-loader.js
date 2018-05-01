/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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

module.paths.unshift(__dirname + "/../src/nodejs_supportlib");
var EventEmitter = require('events').EventEmitter;
var os = require('os');
var fs = require('fs');
var http = require('http');
var util = require('util');

var nodeClusterErrCount = 0;
var meteorClusterErrCount = 0;

function badPackageError(packageName) {
	return "You required the " + packageName + ", which is incompatible with Passenger, a non-functional shim was returned and your app may still work. However, please remove the related code as soon as possible.";
}

// Logs failure to install shim + extended debug info, but with strict spamming protection.
function errorMockingRequire(packageName, error, args, count) {
	if (count > 2) {
		return; // spam protect against repeated warnings
	}
	var msg = "Failed to install shim to guard against the " + packageName + ". Due to: " + error.message + ". Your can safely ignore this warning if you are not using " + packageName;
	msg += "\n\tNode version: " + process.version + "\tArguments: " + args.length;
	for (i = 0; i < args.length; i++) {
		if (i > 9) { // limit the amount of array elements we log
			break;
		}
		msg += "\n\t[" + i + "] " + util.inspect(args[i]).substr(0, 200); // limit the characters per array element
	};
	console.error(msg);
}

//Mock out Node Cluster Module
var Module = require('module');
var originalRequire = Module.prototype.require;
Module.prototype.require = function() {
	try {
		if (arguments['0'] == 'cluster') {
			console.trace(badPackageError("Node Cluster module"));
			return {
				disconnect		 : function(){return false;},
				fork			 : function(){return false;},
				setupMaster		 : function(){return false;},
				isWorker		 : true,
				isMaster		 : false,
				schedulingPolicy : false,
				settings		 : false,
				worker			 : false,
				workers			 : false,
			};
		}
	} catch (e) {
		nodeClusterErrCount++;
		errorMockingRequire("Node Cluster module", e, arguments, nodeClusterErrCount);
	}
	return originalRequire.apply(this, arguments);
};

//Mock out Meteor Cluster Module
var vm = require('vm');
var orig_func = vm.runInThisContext;
vm.runInThisContext = function() {
	try {
		if (arguments.length > 1) {
			var scriptPath = arguments['1'];
			if (typeof scriptPath == 'object') {
				scriptPath = scriptPath['filename'];
			}
			if (scriptPath.indexOf('meteorhacks_cluster') != -1) {
				console.trace(badPackageError("Meteorhacks cluster package"));
				return (function() {
					Package['meteorhacks:cluster'] = {
						Cluster: {
							_publicServices				: {},
							_registeredServices			: {},
							_discoveryBackends			: { mongodb: {} },
							connect						: function(){return false;},
							allowPublicAccess			: function(){return false;},
							discoverConnection			: function(){return false;},
							register					: function(){return false;},
							_isPublicService			: function(){return false;},
							registerDiscoveryBackend	: function(){return false;},
							_blockCallAgain				: function(){return false;}
						}
					};
				});
			}
		}
	} catch (e) {
		meteorClusterErrCount++;
		errorMockingRequire("Meteorhacks Cluster package", e, arguments, meteorClusterErrCount);
	}
	return orig_func.apply(this, arguments);
};


var LineReader = require('phusion_passenger/line_reader').LineReader;

var instrumentModulePaths = [ 'phusion_passenger/log_express', 'phusion_passenger/log_mongodb'];
var instrumentedModules = [];

module.isApplicationLoader = true; // https://groups.google.com/forum/#!topic/compoundjs/4txxkNtROQg
global.PhusionPassenger = exports.PhusionPassenger = new EventEmitter();
var stdinReader = new LineReader(process.stdin);

recordJourneyStepEnd('SUBPROCESS_EXEC_WRAPPER', 'STEP_PERFORMED');
recordJourneyStepBegin('SUBPROCESS_WRAPPER_PREPARATION', 'STEP_IN_PROGRESS');
var options = readStartupArguments();
setupEnvironment(options);


function tryWriteFile(path, contents) {
	try {
		fs.writeFileSync(path, contents);
	} catch (e) {
		console.error('Warning: unable to write to ' + path + ': ' + e.message);
	}
}

function recordJourneyStepBegin(step, state) {
	var workDir = process.env['PASSENGER_SPAWN_WORK_DIR'];
	var stepDir = workDir + '/response/steps/' + step.toLowerCase();
	tryWriteFile(stepDir + '/state', 'STEP_IN_PROGRESS');
	tryWriteFile(stepDir + '/begin_time', Date.now() / 1000);
}

function recordJourneyStepEnd(step, state) {
	var workDir = process.env['PASSENGER_SPAWN_WORK_DIR'];
	var stepDir = workDir + '/response/steps/' + step.toLowerCase();
	tryWriteFile(stepDir + '/state', 'STEP_IN_PROGRESS');
	if (!fs.existsSync(stepDir + '/begin_time') && !fs.existsSync(stepDir + '/begin_time_monotonic')) {
		tryWriteFile(stepDir + '/begin_time', Date.now() / 1000);
	}
	tryWriteFile(stepDir + '/end_time', Date.now() / 1000);
}

function readStartupArguments() {
	var workDir = process.env['PASSENGER_SPAWN_WORK_DIR'];
	var doc = fs.readFileSync(workDir + '/args.json');
	return JSON.parse(doc);
}

function passengerToWinstonLogLevel(passengerLogLevel) {
	switch (passengerLogLevel) {
		case "1":
			return "error";
		case "2":
			return "warn";
		case "3": // notice
		case "4": // info
			return "info";
		case "5": // debug
			return "verbose";
		case "6": // debug2
			return "debug";
		case "7": // debug3
			return "silly";
		case "0": // crit
		default:
			break;
	}

	return "none";
}

function setupEnvironment(options) {
	PhusionPassenger.options = options;
	PhusionPassenger.configure = configure;
	PhusionPassenger._appInstalled = false;

	var logLevel = passengerToWinstonLogLevel(PhusionPassenger.options.log_level);
	var winston = require("vendor-copy/winston");
	var logger = new (winston.Logger)({
			transports: [
				new (winston.transports.Console)({ level: logLevel, debugStdout: true })
			]
	});

	process.title = 'Passenger NodeApp: ' + options.app_root;
	http.Server.prototype.originalListen = http.Server.prototype.listen;
	http.Server.prototype.listen = installServer;

	stdinReader.close();
	stdinReader = undefined;
	process.stdin.on('end', shutdown);
	process.stdin.resume();

	recordJourneyStepEnd('SUBPROCESS_WRAPPER_PREPARATION', 'STEP_PERFORMED');
	recordJourneyStepBegin('SUBPROCESS_APP_LOAD_OR_EXEC', 'STEP_IN_PROGRESS');
	loadApplication();
}

/**
 * PhusionPassenger.configure(options)
 *
 * Configures Phusion Passenger's behavior inside this Node application.
 *
 * Options:
 *   autoInstall (boolean, default true)
 *     Whether to install the first HttpServer object for which listen() is called,
 *     as the Phusion Passenger request handler.
 */
function configure(_options) {
	var options = {
		autoInstall: true
	};
	for (var key in _options) {
		options[key] = _options[key];
	}

    if (!options.autoInstall) {
		http.Server.prototype.listen = listenAndMaybeInstall;
	}
}

function loadApplication() {
	var appRoot = PhusionPassenger.options.app_root || process.cwd();
	var startupFile = PhusionPassenger.options.startup_file || (appRoot + '/' + 'app.js');
	require(startupFile);
}

function extractCallback(args) {
	if (args.length > 1 && typeof(args[args.length - 1]) == 'function') {
		return args[args.length - 1];
	}
}

function generateServerSocketPath() {
	var options = PhusionPassenger.options;
	var socketDir, socketPrefix, socketSuffix;

	if (options.socket_dir) {
		socketDir = options.socket_dir;
		socketPrefix = "node";
	} else {
		socketDir = os.tmpdir().replace(/\/$/, '');
		socketPrefix = "PsgNodeApp";
	}
	socketSuffix = ((Math.random() * 0xFFFFFFFF) & 0xFFFFFFF);

	var result = socketDir + "/" + socketPrefix + "." + socketSuffix.toString(36);
	var UNIX_PATH_MAX = options.UNIX_PATH_MAX || 100;
	return result.substr(0, UNIX_PATH_MAX);
}

function addListenerAtBeginning(emitter, event, callback) {
	var listeners = emitter.listeners(event);
	var i;

	emitter.removeAllListeners(event);
	emitter.on(event, callback);
	for (i = 0; i < listeners.length; i++) {
		emitter.on(event, listeners[i]);
	}
}

function doListen(server, listenTries, callback) {
	function errorHandler(error) {
		if (error.errno == 'EADDRINUSE') {
			if (listenTries == 100) {
				server.emit('error', new Error(
					'Phusion Passenger could not find suitable socket address to bind on'));
			} else {
				// Try again with another socket path.
				listenTries++;
				doListen(server, listenTries, callback);
			}
		} else {
			server.emit('error', error);
		}
	}

	var socketPath = PhusionPassenger.options.socket_path = generateServerSocketPath();
	server.once('error', errorHandler);
	server.originalListen(socketPath, function() {
		server.removeListener('error', errorHandler);
		doneListening(server, callback);
		process.nextTick(finalizeStartup);
	});
}

function doneListening(server, callback) {
	if (callback) {
		server.once('listening', callback);
	}
	server.emit('listening');
}

function installServer() {
	var server = this;
	if (!PhusionPassenger._appInstalled) {
		PhusionPassenger._appInstalled = true;
		PhusionPassenger._server = server;

		recordJourneyStepEnd('SUBPROCESS_APP_LOAD_OR_EXEC', 'STEP_PERFORMED');
		recordJourneyStepBegin('SUBPROCESS_LISTEN', 'STEP_IN_PROGRESS');

		// Ensure that req.connection.remoteAddress and remotePort return something
		// instead of undefined. Apps like Etherpad expect it.
		// See https://github.com/phusion/passenger/issues/1224
		addListenerAtBeginning(server, 'request', function(req) {
			req.connection.__defineGetter__('remoteAddress', function() {
				return '127.0.0.1';
			});
			req.connection.__defineGetter__('remotePort', function() {
				return 0;
			});
		});

		var listenTries = 0;
		doListen(server, listenTries, extractCallback(arguments));

		return server;
	} else {
		throw new Error("http.Server.listen() was called more than once, which " +
			"is not allowed because Phusion Passenger is in auto-install mode. " +
			"This means that the first http.Server object for which listen() is called, " +
			"is automatically installed as the Phusion Passenger request handler. " +
			"If you want to create and listen on multiple http.Server object then " +
			"you should disable auto-install mode. Please read " +
			"http://stackoverflow.com/questions/20645231/phusion-passenger-error-http-server-listen-was-called-more-than-once/20645549");
	}
}

function listenAndMaybeInstall(port) {
	if (port === 'passenger' || port == '/passenger') {
		if (!PhusionPassenger._appInstalled) {
			return installServer.apply(this, arguments);
		} else {
			throw new Error("You may only call listen('passenger') once. Please read http://stackoverflow.com/questions/20645231/phusion-passenger-error-http-server-listen-was-called-more-than-once/20645549");
		}
	} else {
		return this.originalListen.apply(this, arguments);
	}
}

function finalizeStartup() {
	recordJourneyStepEnd('SUBPROCESS_LISTEN', 'STEP_PERFORMED');

	var workDir = process.env['PASSENGER_SPAWN_WORK_DIR'];

	fs.writeFileSync(workDir + '/response/properties.json', JSON.stringify({
		sockets: [
			{
				name: 'main',
				address: 'unix:' + PhusionPassenger.options.socket_path,
				protocol: 'http',
				concurrency: 0,
				accept_http_requests: true
			}
		]
	}));

	// fs.writeFileSync() does not work on FIFO files
	var stream = fs.createWriteStream(workDir + '/response/finish');
	stream.write('1');
	stream.close();
}

function shutdown() {
	if (PhusionPassenger.shutting_down) {
		return;
	}

	PhusionPassenger.shutting_down = true;
	try {
		fs.unlinkSync(PhusionPassenger.options.socket_path);
	} catch (e) {
		// Ignore error.
	}
	if (PhusionPassenger.listeners('exit').length > 0) {
		PhusionPassenger.emit('exit');
	} else if (process.listeners('message').length > 0) {
		process.emit('message', 'shutdown');
	} else {
		process.exit(0);
	}
}
