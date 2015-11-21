/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013 Phusion Holding B.V.
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
#ifndef _PASSENGER_UTILS_CURL_H_
#define _PASSENGER_UTILS_CURL_H_

/**
 * Utilities for setting libcurl proxy information. Proxy information is contained
 * in a user-supplied string in the form of:
 *
 *     protocol://[username:password@]host[:port][?option1,option2]
 *
 * The address may also be `none`, which indicates that proxy usage should be
 * explicitly disabled even when environment variables such as "http_proxy" etc
 * are set.
 *
 * You are supposed to prepare a CurlProxyInfo object with prepareCurlProxy().
 * Keep this object alive as long as you're using the CURL handle.
 * Then, call setCurlProxy() to set the proxy information on the CURL handle.
 *
 * prepareCurlProxy() throws ArgumentException upon encountering an invalid
 * proxy address.
 *
 * If the address is an empty string, prepareCurlProxy() and setCurlProxy()
 * don't do anything.
 */

#include <string>
#include <cstring>
#include <curl/curl.h>
#include <boost/foreach.hpp>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;
using namespace boost;

#ifndef CURLINFO_RESPONSE_CODE
	#define CURLINFO_RESPONSE_CODE CURLINFO_HTTP_CODE
#endif

struct CurlProxyInfo {
	string hostAndPort;
	string credentials;
	long type;
	bool none;
	bool httpTunnel;

	CurlProxyInfo()
		: type(0),
		  none(false),
		  httpTunnel(false)
		{ }
};

inline const CurlProxyInfo
prepareCurlProxy(const string &address) {
	if (address.empty()) {
		return CurlProxyInfo();
	} else if (address == "none") {
		CurlProxyInfo result;
		result.none = true;
		return result;
	} else {
		CurlProxyInfo result;
		size_t protocolLen;
		vector<string> options;
		string::size_type pos;

		if (startsWith(address, "http://")) {
			protocolLen = strlen("http://");
			result.type = CURLPROXY_HTTP;
		} else if (startsWith(address, "socks5://")) {
			protocolLen = strlen("socks5://");
			result.type = CURLPROXY_SOCKS5;
		} else if (startsWith(address, "socks4://")) {
			protocolLen = strlen("socks4://");
			#if LIBCURL_VERSION_NUM >= 0x070A00
				result.type = CURLPROXY_SOCKS4;
			#else
				throw ArgumentException("Socks4 proxies are not supported because "
					"libcurl doesn't support it. Please upgrade libcurl to version "
					"7.10 or higher.");
			#endif
		} else if (startsWith(address, "socks4a://")) {
			protocolLen = strlen("socks4a://");
			#if LIBCURL_VERSION_NUM >= 0x071200
				result.type = CURLPROXY_SOCKS4A;
			#else
				throw ArgumentException("Socks4A proxies are not supported because "
					"libcurl doesn't support it. Please upgrade libcurl to version "
					"7.18.0 or higher.");
			#endif
		} else if (startsWith(address, "socks5h://")) {
			protocolLen = strlen("socks5h://");
			#if LIBCURL_VERSION_NUM >= 0x071200
				result.type = CURLPROXY_SOCKS5_HOSTNAME;
			#else
				throw ArgumentException("Socks5 proxies (with proxy DNS resolving) are "
					"not supported because libcurl doesn't support it. Please upgrade "
					"libcurl to version 7.18.0 or higher.");
			#endif
		} else if (address.find("://") == string::npos) {
			throw ArgumentException("Invalid proxy address: no protocol specified.");
		} else {
			throw ArgumentException("Only 'http' and 'socks5' proxies are supported.");
		}

		result.hostAndPort = address.substr(protocolLen);

		// Extract options.
		pos = result.hostAndPort.find("?");
		if (pos != string::npos) {
			string optionsString = result.hostAndPort.substr(pos + 1);
			result.hostAndPort.erase(pos);
			split(optionsString, ',', options);
		}

		// Extract authentication credentials.
		pos = result.hostAndPort.find("@");
		if (pos != string::npos) {
			result.credentials = result.hostAndPort.substr(0, pos);
			result.hostAndPort.erase(0, pos + 1);
		}

		if (result.hostAndPort.empty()) {
			throw ArgumentException("No proxy host name given.");
		}

		foreach (const string option, options) {
			if (option == "tunnel") {
				if (result.type == CURLPROXY_HTTP) {
					result.httpTunnel = true;
				} else {
					throw ArgumentException("The 'tunnel' option is only supported for HTTP proxies.");
				}
			} else {
				throw ArgumentException("Invalid proxy address option '" + option + "'.");
			}
		}

		return result;
	}
}

inline void
setCurlProxy(CURL *curl, const CurlProxyInfo &proxyInfo) {
	if (proxyInfo.hostAndPort.empty()) {
		return;
	} else if (proxyInfo.none) {
		curl_easy_setopt(curl, CURLOPT_PROXY, "");
	} else {
		curl_easy_setopt(curl, CURLOPT_PROXY, proxyInfo.hostAndPort.c_str());
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, proxyInfo.type);
		curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxyInfo.credentials.c_str());
		if (proxyInfo.httpTunnel) {
			curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1);
		}
	}
}

} // namespace Passenger

#endif /* _PASSENGER_UTILS_CURL_H_ */
