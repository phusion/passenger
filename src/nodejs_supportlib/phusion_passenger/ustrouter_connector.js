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

var net = require('net');
var os = require("os");
var nbo = require('network-byte-order');
var codify = require('codify');
var microtime = require('microtime');

var ustRouterAddress; // standalone = "localhost:9344";
var ustRouterUser;
var ustRouterPass;
var ustGatewayKey;

var nodeName = os.hostname();
var appGroupName;

var routerConn;
var routerState = 0;
var pendingTxnBuf = [];

var inspection = false;

// some kind of discard timer? failure to connect?

function changeState(newRouterState) {
	if (inspection) console.log("routerState: " + routerState + " -> " + newRouterState);
	routerState = newRouterState;
}

exports.init = function(routerAddress, routerUser, routerPass, gatewayKey, groupName) {
	if (routerState != 0) {
		console.error("ERROR: trying to init when routerState not 0! (ignoring)");
		return;
	}
	ustRouterAddress = routerAddress;
	ustRouterUser = routerUser;
	ustRouterPass = routerPass;
	ustGatewayKey = gatewayKey;
	appGroupName = groupName;
	
	// createConnection doesn't understand the "unix:" prefix, but it does understand the path that follows.
	if (ustRouterAddress.indexOf("unix:") == 0) {
		ustRouterAddress = ustRouterAddress.substring(5);
	}
	console.log("initialized ustrouter_connector with [" + ustRouterAddress + "] [" + ustRouterUser + "] [" + ustRouterPass + "] [" + 
		ustGatewayKey + "] [" + appGroupName + "]");

	beginConnection();
}

function beginConnection() {
	changeState(1);

	routerConn = net.createConnection(ustRouterAddress);

	routerConn.on("connect", function() { changeState(2); });
	routerConn.on("error", onError);
	routerConn.on("end", function() { changeState(0); });

	routerConn.on("data", onData);
}

function LogTransaction(cat) {
	this.timestamp = microtime.now();
	this.category = cat;
	this.txnId = "";
	this.logBuf = [];
	this.state = 1;
}

function tryWriteLogs() {
	if (inspection) console.log("tryWriteLogs");
	if (routerState == 0) {
		// it disconnected or crashed somehow, reconnect
		beginConnection();
		return;
	} else if (routerState != 5) {
		return; // we're not idle
	}

	// we have an authenticated, active connection; see what we can send
	if (pendingTxnBuf.length == 0) {
		return; // no pending
	}
	
	if (pendingTxnBuf[0].state == 1) {
		// still need to open the txn
		changeState(6); // expect ok/txnid in onData()..
		if (inspection) console.log("open transaction(" + pendingTxnBuf[0].txnId + ")");
		writeLenArray(routerConn, "openTransaction\0" + pendingTxnBuf[0].txnId + "\0" + appGroupName + "\0" + nodeName + "\0" + 
			pendingTxnBuf[0].category +	"\0" + codify.toCode(pendingTxnBuf[0].timestamp) + "\0" + ustGatewayKey + "\0true\0true\0\0");
	} else {
		// txn is open, log the data & close
		if (inspection) console.log("log & close transaction(" + pendingTxnBuf[0].txnId + ")");
		txn = pendingTxnBuf.shift();
		for (i = 0; i < txn.logBuf.length; i++) {
			writeLenArray(routerConn, "log\0" + txn.txnId + "\0" + codify.toCode(txn.timestamp) + "\0");
			writeLenString(routerConn, txn.logBuf[i]);
		}

		changeState(7); // expect ok in onData()..
		writeLenArray(routerConn, "closeTransaction\0" + txn.txnId + "\0" + codify.toCode(microtime.now()) + "\0true\0");
	}	
}

exports.getTxnIdFromRequest = function(req) {
	return req.headers['passenger-txn-id'];
}

exports.logToUstTransaction = function(category, lineArray, txnIfContinue) {
	logTxn = new LogTransaction(category);
	
	if (txnIfContinue) {
		logTxn.txnId = txnIfContinue;
	}
	logTxn.logBuf = lineArray;
	pendingTxnBuf.push(logTxn);
	
	tryWriteLogs();
}

var readBuf = "";
// N.B. newData may be partial!
function readLenArray(newData) {
	readBuf += newData;
if (inspection) console.log("read: total len = " + readBuf.length);
if (inspection) console.log(new Buffer(readBuf));
	if (readBuf.length < 2) {
if (inspection) console.log("need more header data..");
		return null; // expecting at least length bytes
	}
	lenRcv = nbo.ntohs(new Buffer(readBuf), 0);
if (inspection) console.log("read: lenRCv = " + lenRcv);
	if (readBuf.length < 2 + lenRcv) {
if (inspection) console.log("need more payload data..");
		return null; // not fully read yet
	}
	resultStr = readBuf.substring(2, lenRcv + 2);
	readBuf = readBuf.substring(lenRcv + 2); // keep any bytes read beyond length for next read
	
	return resultStr.split("\0");
//	setTimeout(function () { console.log('timeout..'); c.end() }, 100);
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

function onError(e) {
	if (routerState == 1) {
		console.error("Unable to connect to ustrouter at [" + ustRouterAddress +"], Union Station logging disabled.");
	} else {
		console.error("Unexpected error in ustrouter connection: " + e + ", Union Station logging disabled.");
	}
	changeState(0);
}

function onData(data) {
	if (inspection) console.log(data);
	rcvString = readLenArray(data);
	if (!rcvString) {
		return;
	}
	if (inspection) console.log("got: [" + rcvString + "]");

	if (routerState == 2) { // expect version 1
		if ("version" !== rcvString[0] || "1" !== rcvString[1]) {
			console.error("Unsupported ustrouter version [" + rcvString + "], Union Station logging disabled.");
			changeState(0);
			// TODO: close?
			return;
		}
		changeState(3);

		writeLenString(routerConn, ustRouterUser);
		writeLenString(routerConn, ustRouterPass);
	} else if (routerState == 3) { // expect OK from auth
		if ("ok" != rcvString[0]) {
			console.error("Error authenticating to ustrouter: [" + rcvString + "], Union Station logging disabled.");
			changeState(0);
			// TODO: close?
			return;
		}
		changeState(4);

		writeLenArray(routerConn, "init\0" + nodeName + "\0");
	} else if (routerState == 4) { // expect OK from init
		if ("ok" != rcvString[0]) {
			console.error("Error initializing ustrouter connection: [" + rcvString + "], Union Station logging disabled.");
			changeState(0);
			// TODO: close?
			return;
		}
	
		changeState(5);
		tryWriteLogs();
	 } else if (routerState == 5) { // not expecting anything
	 	console.log("unexpected data receive state (5)");
	 	tryWriteLogs();
	} else if (routerState == 6) { // expect OK transaction open
		if ("ok" != rcvString[0]) {
			console.error("Error opening ustrouter transaction: [" + rcvString + "], Union Station logging disabled.");
			changeState(0);
			// TODO: close?
			return;
		}
		
		pendingTxnBuf[0].state = 2;
		if (pendingTxnBuf[0].txnId.length == 0) {
			if (inspection) console.log("use rcvd back txnId: " + rcvString[1]);
			pendingTxnBuf[0].txnId = rcvString[1]; // fill in the txn from the ustrouter reply
		}

		changeState(5);
		tryWriteLogs();
	} else if (routerState == 7) { // expect OK transaction close
		if ("ok" != rcvString[0]) {
			console.error("Error closing ustrouter transaction: [" + rcvString + "], Union Station logging disabled.");
			changeState(0);
			// TODO: close?
			return;
		}
		
		changeState(5);
		tryWriteLogs();
	}
}
