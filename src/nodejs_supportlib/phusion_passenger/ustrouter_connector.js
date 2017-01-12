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

var log;
var net = require('net');
var nbo = require('vendor-copy/network-byte-order');
var codify = require('vendor-copy/codify');

var ustRouterAddress; // will normally be a unix sock, although there is a default standalone = "tcp://127.0.0.1:9344";
var ustRouterPort; // if not a unix sock
var ustRouterUser;
var ustRouterPass;
var ustGatewayKey;

var appGroupName;

var routerConn;
var routerState;
// -1: disabled (not initialized or unrecoverable error, won't try to reconnect without new init)
// 0: disconnected, ready to (re)try connect
// 1: awaiting connect
// 2: awaiting version
// 3: awaiting auth OK
// 4: awaiting init OK
// 5: idle / ready to send
// 6: awaiting openTransaction OK
// 7: awaiting closeTransaction OK

var pendingTxnBuf;
var pendingTxnBufMaxLength;
var connTimeoutMs;
var autoRetryAfterMs;

setDefaults();
function setDefaults() {
	routerState = -1;
	pendingTxnBuf = [];
	pendingTxnBufMaxLength = 5000;
	connTimeoutMs = 10000;
	autoRetryAfterMs = 30000;
}

// Call to initiate a connection with the UstRouter. If called with incomplete parameters the connector will just be
// disabled (isEnabled() will return false) and no further actions will be taken. If the connection fails, it is auto-retried
// whenever logToUstTransaction(..) is called.
exports.init = function(logger, routerAddress, routerUser, routerPass, gatewayKey, groupName) {
	log = logger;
	if (routerState > 0) {
		log.warn("Trying to init when routerState > 0! (ignoring)");
		return;
	}

	routerState = -1;
	ustRouterAddress = routerAddress;
	ustRouterUser = routerUser;
	ustRouterPass = routerPass;
	ustGatewayKey = gatewayKey;
	appGroupName = groupName;

	if (ustRouterAddress) {
		if (ustRouterAddress.indexOf("unix:") == 0) {
			// createConnection doesn't understand the "unix:" prefix, but it does understand the path that follows.
			ustRouterAddress = ustRouterAddress.substring(5);
		} else if (ustRouterAddress.indexOf("tcp://") == 0) {
			var hostAndPort = ustRouterAddress.substring(6).split(':');
			ustRouterAddress = hostAndPort[0];
			ustRouterPort = hostAndPort[1];
		}
	}
	log.debug("initialize ustrouter_connector with [routerAddress:" + ustRouterAddress + "] " +
		(ustRouterPort ? "[ustRouterPort:" + ustRouterPort + "] " : "") + "[user:" + ustRouterUser + "] [pass:" +
		ustRouterPass + "] [key:" +	ustGatewayKey + "] [app:" + appGroupName + "]");

	if (!ustRouterAddress || !ustRouterUser || !ustRouterPass || !ustGatewayKey || !appGroupName) {
		log.verbose("Union Station logging disabled (incomplete configuration).");
		return;
	}

	changeState(0, "Init approved");

	beginConnection();
};

exports.finit = function() {
	resetState("finit()");
};

exports.isEnabled = function() {
	return routerState >= 0;
};

function beginConnection() {
	changeState(1);

	setWatchdog(connTimeoutMs); // Watchdog for the entire connect-to-ustRouter process.

	if (ustRouterPort) {
		routerConn = net.createConnection(ustRouterPort, ustRouterAddress);
	} else {
		routerConn = net.createConnection(ustRouterAddress);
	}

	routerConn.on("connect", onConnect);
	routerConn.on("error", onError);
	routerConn.on("end", onEnd);
	routerConn.on("data", onData);
}

function onConnect() {
	changeState(2);
}

function onError(e) {
	if (routerState == 1) {
		log.error("Unable to connect to UstRouter at [" + ustRouterAddress +"], will auto-retry.");
	} else {
		// could be called when e.g. a write fails
		log.error("Uncategorized error in UstRouter connection: " + e + ", will auto-retry.");
	}
	resetState("onError");
}

function onEnd() {
	// e.g. when connection dies
	resetState("onEnd");
}

function getWallclockMicrosec() {
	// for lack of a more accurate clock
	return new Date().getTime() * 1000;
}

function LogTransaction(cat) {
	this.timestamp = getWallclockMicrosec();
	this.category = cat;
	this.txnId = "";
	this.logBuf = [];
	this.state = 0; // 0: untouched, 1: open sent, 2: close sent
}

function findLastPendingTxnForId(txnId) {
	for (var i = pendingTxnBuf.length - 1; i >= 0; i--) {
		if (pendingTxnBuf[i].txnId == txnId) {
			return pendingTxnBuf[i];
		}
	}
}

exports.deferIfPendingTxns = function(txnId, deferThis, deferFn, deferArgs) {
	var txn = findLastPendingTxnForId(txnId);

	if (!txn) {
		return deferFn.apply(deferThis, deferArgs);
	} else {
		log.debug("defer response.end() for txn " + txnId);
		txn.deferThis = deferThis;
		txn.deferFn = deferFn;
		txn.deferArgs = deferArgs;
	}
};

// Example categories are "requests", "exceptions". The lineArray is a specific format parsed by Union STation.
// txnIfContinue is an optional txnId and attaches the log to an existing transaction with the specified txnId.
// N.B. transactions will be dropped if the outgoing buffer limit is reached.
exports.logToUstTransaction = function(category, lineArray, txnIfContinue) {
	if (!this.isEnabled()) {
		return;
	}

	if (pendingTxnBuf.length < pendingTxnBufMaxLength) {
		var logTxn = new LogTransaction(category);

		if (txnIfContinue) {
			logTxn.txnId = txnIfContinue;
		}
		logTxn.logBuf = lineArray;

		pendingTxnBuf.push(logTxn);
	} else {
		log.debug("Dropping Union Station log due to outgoing buffer limit (" + pendingTxnBufMaxLength + ") reached");
	}

	pushPendingData();
};

function verifyOk(rcvString, topic) {
	if ("status" != rcvString[0] || "ok" != rcvString[1]) {
		log.error("Error with " + topic + ": [" + rcvString + "], will auto-retry.");
		resetState("not OK reply");
		return false;
	}
	return true;
}

function pushPendingData() {
	log.debug("pushPendingData");

	if (routerState == 0) {
		// it disconnected or crashed somehow, reconnect
		beginConnection();
		return;
	} else if (routerState != 5) {
		return; // we're not ready to send
	}

	// we have an authenticated, active connection; see what we can send
	if (pendingTxnBuf.length == 0) {
		return; // no pending
	}

	switch (pendingTxnBuf[0].state) {
		case 0:
			// still need to open the txn
			changeState(6); // expect ok/txnid in onData()..
			setWatchdog(connTimeoutMs);
			log.debug("open transaction(" + pendingTxnBuf[0].txnId + ")");
			pendingTxnBuf[0].state = 1;
			writeLenArray(routerConn, "openTransaction\0" + pendingTxnBuf[0].txnId + "\0" + appGroupName + "\0\0" +
				pendingTxnBuf[0].category +	"\0" + codify.toCode(pendingTxnBuf[0].timestamp) + "\0" + ustGatewayKey + "\0true\0true\0\0");
			break;

		case 1:
			// open was sent, still waiting for OK.
			break;

		case 2:
			// txn is open, log the data & close
			log.debug("log & close transaction(" + pendingTxnBuf[0].txnId + ")");
			var txn = pendingTxnBuf.shift();

			if (txn.deferFn) {
				var moveToTxn = findLastPendingTxnForId(txn.txnId);
				if (!moveToTxn) {
					log.debug("found deferred response.end(), no other queued attachments for txn " + txn.txnId + ", so calling now that it's safe");
					txn.deferFn.apply(txn.deferThis, txn.deferArgs);
				} else {
					log.debug("moving deferred response.end() because there are other relevant attachment(s) for txn " + txn.txnId);
					moveToTxn.deferThis = txn.deferThis;
					moveToTxn.deferFn = txn.deferFn;
					moveToTxn.deferArgs = txn.deferArgs;
				}
			}

			for (var i = 0; i < txn.logBuf.length; i++) {
				writeLenArray(routerConn, "log\0" + txn.txnId + "\0" + codify.toCode(txn.timestamp) + "\0");
				writeLenString(routerConn, txn.logBuf[i]);
			}

			changeState(7); // expect ok in onData()..
			setWatchdog(connTimeoutMs);
			writeLenArray(routerConn, "closeTransaction\0" + txn.txnId + "\0" + codify.toCode(getWallclockMicrosec()) + "\0true\0");
			log.debug("wrote log and close for " + txn.txnId);
			break;

		default:
			log.error("Unexpected pendingTxnBuf[0].state " + pendingTxnBuf[0].state + ", discarding it.");
			pendingTxnBuf.shift();
			break;
	}
}

var watchDogId;

function onWatchdogTimeout() {
	resetState("onWatchdogTimeout");
}

// Resets the connection if there is no progress in the next timeoutMs.
function setWatchdog(timeoutMs) {
	if (watchDogId) {
		resetWatchdog();
	}
	watchDogId = setTimeout(onWatchdogTimeout, timeoutMs);
}

function resetWatchdog() {
	if (watchDogId) {
		clearTimeout(watchDogId);
		watchDogId = null;
	}
}

var readBuf = "";
// N.B. newData may be partial!
function readLenArray(newData) {
	readBuf += newData;
	log.silly("read: total len = " + readBuf.length);
	log.silly(new Buffer(readBuf).toString("hex"));

	if (readBuf.length < 2) {
		log.silly("need more header data..");
		return null; // expecting at least length bytes
	}

	var payloadLen = nbo.ntohs(new Buffer(readBuf), 0);
	log.silly("read: payloadLen = " + payloadLen);

	if (readBuf.length < 2 + payloadLen) {
		log.silly("need more payload data..");
		return null; // not fully read yet
	}
	var resultStr = readBuf.substring(2, payloadLen + 2);
	readBuf = readBuf.substring(payloadLen + 2); // keep any bytes read beyond length for next read

	return resultStr.split("\0");
}

function onData(data) {
	log.silly("onData [" + data + "] (len = " + data.length + ")");

	var rcvString = readLenArray(data);
	if (!rcvString) {
		return;
	}

	log.silly("got: [" + rcvString + "]");

	switch (routerState) {
		case 2:
			if ("version" == rcvString[0] && "1" == rcvString[1]) {
				changeState(3);

				writeLenString(routerConn, ustRouterUser);
				writeLenString(routerConn, ustRouterPass);
			} else {
				log.error("Error with UstRouter version: [" + rcvString + "], will auto-retry.");
				resetState("not OK version reply");
			}
			break;

		case 3:
			if (verifyOk(rcvString, "UstRouter authentication")) {
				changeState(4);
				writeLenArray(routerConn, "init\0");
			}
			break;

		case 4:
			if (verifyOk(rcvString, "UstRouter initialization")) {
				resetWatchdog(); // initialization done, lift the watchdog guard
				changeState(5);
				pushPendingData();
			}
			break;

		case 5:
			log.warn("unexpected data receive state (5)");
			pushPendingData();
			break;

		case 6:
			resetWatchdog();
			if (verifyOk(rcvString, "UstRouter openTransaction")) {
				pendingTxnBuf[0].state = 2;
				if (pendingTxnBuf[0].txnId.length == 0) {
					log.debug("use rcvd back txnId: " + rcvString[2]);
					pendingTxnBuf[0].txnId = rcvString[2]; // fill in the txn from the UstRouter reply
				}

				changeState(5);
				pushPendingData();
			}
			break;

		case 7:
			resetWatchdog();
			if (verifyOk(rcvString, "UstRouter closeTransaction")) {
				changeState(5);
				pushPendingData();
			}
			break;
	}
}

function resetState(reason) {
	changeState(0, reason);

	// When experiencing a mid-transaction failure (pending transaction state is increased once a transaction open has
	// been sent), we don't really know what the other side remembers about the transaction (e.g. nothing if it crashed).
	// It's even possible that the transaction itself is causing the problem (e.g. invalid category), so we choose to
	// drop it and disconnect. The drop avoids getting stuck on invalid txns and the disconnect cleans up remote resources.
	if (pendingTxnBuf.length > 0 && pendingTxnBuf[0].state != 0) {
		log.debug("Mid-transaction (id: " + pendingTxnBuf[0].txnId + ") failure: drop/skipping Union Station log");
		pendingTxnBuf.shift();
	}

	// ensure connection is finished and we don't get any outdated triggers
	resetWatchdog();

	if (routerConn) {
		routerConn.destroy();
	}

	setTimeout(function() { pushPendingData(); }, autoRetryAfterMs);
}

function changeState(newRouterState, optReason) {
	log.debug("routerState: " + routerState + " -> " + newRouterState + (optReason ? " due to: " + optReason : ""));

	routerState = newRouterState;
}

function writeLenString(c, str) {
	var len = new Buffer(4);
	nbo.htonl(len, 0, str.length);
	c.write(len);
	c.write(str);
}

function writeLenArray(c, str) {
	var len = new Buffer(2);
	nbo.htons(len, 0, str.length);
	c.write(len);
	c.write(str);
}

if (process.env.NODE_ENV === 'test') {
	exports.setDefaults = setDefaults;
	exports.pushPendingData = pushPendingData;

	exports.getRouterState = function() { return routerState; };
	exports.setPendingTxnBufMaxLength = function(val) { pendingTxnBufMaxLength = val; };
	exports.getPendingTxnBuf = function() { return pendingTxnBuf; };
	exports.setConnTimeoutMs = function(val) { connTimeoutMs = val; };
	exports.setAutoRetryAfterMs = function(val) { autoRetryAfterMs = val; };
}