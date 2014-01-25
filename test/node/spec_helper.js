var util = require('util');
var EventEmitter = require('events').EventEmitter;
var assert = require('assert');
require('should');


function FakeStream() {
	EventEmitter.call(this);
	this.paused = false;
	this.connection = {};
}

util.inherits(FakeStream, EventEmitter);

FakeStream.prototype.resume = function() {
	this.paused = false;
	this.flowing = true;
}

FakeStream.prototype.pause = function() {
	this.paused = true;
	this.flowing = false;
}

FakeStream.prototype.on = function(event, listener) {
	EventEmitter.prototype.on.call(this, event, listener);
	// If listening to data, and it has not explicitly been paused,
	// then call resume to start the flow of data.
	if (event == 'data' && this.flowing !== false) {
		this.resume();
	}
}

exports.FakeStream = FakeStream;


var Helper = {
	eventually: function(timeout, check, done) {
		var startTime = new Date();
		var id = setInterval(function() {
			if (check()) {
				clearInterval(id);
				done();
			} else if (new Date() - startTime > timeout) {
				clearInterval(id);
				assert.fail("Something which should eventually happen never happened");
			}
		}, 10);
	},

	shouldNeverHappen: function(timeout, check, done) {
		var startTime = new Date();
		var id = setInterval(function() {
			if (check()) {
				clearInterval(id);
				assert.fail("Something which should never happen, happened anyway");
			} else if (new Date() - startTime > timeout) {
				clearInterval(id);
				done();
			}
		}, 10);
	}
};

exports.Helper = Helper;
