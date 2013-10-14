var util = require('util');
var EventEmitter = require('events').EventEmitter;
require('should');


function FakeStream() {
	EventEmitter.call(this);
	this.paused = false;
}

util.inherits(FakeStream, EventEmitter);

FakeStream.prototype.resume = function() {
	this.paused = false;
}

FakeStream.prototype.pause = function() {
	this.paused = true;
}

exports.FakeStream = FakeStream;


var Helper = {
};

exports.Helper = Helper;
