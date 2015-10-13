var ustLog = require('phusion_passenger/ustrouter_connector');

var should = require('should');
var net = require('net');

var nbo = require('vendor-copy/network-byte-order');
var winston = require("vendor-copy/winston");
var logger = new (winston.Logger)({
	transports: [ 
		new (winston.transports.Console)({ level: "none" })
	]
});

var server;

describe('ustrouter_connector', function() {
	afterEach(function() {
		ustLog.finit();
		ustLog.setDefaults();

		if (server) {
			server.close();
			server = null;
		}
	});
	
	it('handles missing user-configurable params and becomes disabled', function() {
		ustLog.init(logger);
		ustLog.isEnabled().should.equal(false);
	});
	
	it('can connect, auth & init successfully', function(done) {
		server = net.createServer(getServerSocketHandler(done, 1)).listen(8124);

		ustLog.init(logger, "tcp://localhost:8124", "username", "password", "ustkey", "appgroup");
	});

	it('times out on lack of version response, auto-recovers', function(done) {
		server = net.createServer(getServerSocketHandler(function() {
			ustLog.getRouterState().should.greaterThan(0);
			done();
		}, 2)).listen(8124);
		
		ustLog.setConnTimeoutMs(100);
		ustLog.setAutoRetryAfterMs(0);
		ustLog.init(logger, "tcp://localhost:8124", "username", "password", "ustkey", "appgroup");
	});
	
	it('handles non-ok init response, auto-recovers', function(done) {
		server = net.createServer(getServerSocketHandler(function() {
			ustLog.getRouterState().should.greaterThan(0);
			done();
		}, 3)).listen(8124);
		
		ustLog.setAutoRetryAfterMs(0);
		ustLog.init(logger, "tcp://localhost:8124", "username", "password", "ustkey", "appgroup");
	});
	
});

// behavior: 1 = normal, first time only failures: 2 = no version response, 3 = invalid init response
function getServerSocketHandler(callWhenDone, behavior) {
	return function(socket) {
		var state = 1;
		
		socket.on('error', function (e) {
			if (state != 3) { // ignore error if done
				throw e;
			}
		});

		if (behavior == 2) {
			behavior = 1;
			return;
		}
		writeLenArray(socket, "version\0" + "1\0");
		
		var dataBuf = "";
		socket.on('data', function (data) {
			dataBuf += data;
			
			switch(state) {
				case 1:
					var lenStrings = parseLenStringArray(dataBuf);
					if (lenStrings && lenStrings.length == 2) {
						lenStrings[0].should.equal("username");
						lenStrings[1].should.equal("password");
						
						if (behavior == 3) {
							behavior = 1;
							dataBuf = "";
							writeLenArray(socket, "status\0" + "fail\0");
							return;
						}
						writeLenArray(socket, "status\0" + "ok\0");
						dataBuf = "";
						state = 2;
					}
					break;
					
				case 2:
					var lenArray = parseLenArray(dataBuf);
					if (lenArray) {
						lenArray[0].should.equal("init");
						lenArray[1].should.equal(require('os').hostname());
						
						writeLenArray(socket, "status\0" + "ok\0");
						dataBuf = "";
						state = 3;
						callWhenDone();
					}
					break;
			}
		});
	}
}

function parseLenStringArray(str) {
	var lenStrings = [];
	while(str.length >= 4) {
		var len = nbo.ntohl(new Buffer(str), 0);
		if (str.length < 4 + len) {
			return;
		}
		lenStrings.push(str.substring(4, 4 + len));
		str = str.substring(4 + len);
	}
	return lenStrings;
}

function parseLenArray(str) {
	if (str.length < 2) {
		return;
	}
	var len = nbo.ntohs(new Buffer(str), 0);
	if (str.length < 2 + len) {	
		return;
	}
	str = str.substring(2, 2 + len);
	return str.split('\0');
}

function writeLenString(c, str) {
	len = new Buffer(4);
	nbo.htonl(len, 0, str.length);
	c.write(len);
	c.write(str);
}

function writeLenArray(c, str) {
	len = new Buffer(2);
	nbo.htons(len, 0, str.length);
	c.write(len);
	c.write(str);
}
