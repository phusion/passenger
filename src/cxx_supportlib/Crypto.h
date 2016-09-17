/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_CRYPTO_H_
#define _PASSENGER_CRYPTO_H_

#include <boost/function.hpp>
#include <boost/thread.hpp>
#include <modp_b64.h>

#if BOOST_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#else
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

namespace Passenger {

#if BOOST_OS_MACOS
typedef SecKeyRef PUBKEY_TYPE;
#else
typedef RSA* PUBKEY_TYPE;
#endif

using namespace std;
using namespace boost;
using namespace oxt;

class Crypto {
private:
	/**
	 * @returns new PUBKEY_TYPE; user is responsible for free (with freePubKey)
	 */
	PUBKEY_TYPE loadPubKey(const char *filename);

	/**
	 * free a PUBKEY_TYPE (loaded with loadPubKey); may be NULL
	 */
	void freePubKey(PUBKEY_TYPE);

	/**
	 * log prefix using P_ERROR, and (library-specific) detail from either additional or global query
	 */
#if BOOST_OS_MACOS
	// (additional needs to be defined as a CFErrorRef, void * won't work)
	void logErrorExtended(string prefix, CFErrorRef additional = NULL);
	CFDictionaryRef createQueryDict(const char *label);
	SecAccessRef createAccess(const char *cLabel);
	OSStatus lookupKeychainItem(const char *label, SecIdentityRef *oIdentity);
	OSStatus copyIdentityFromPKCS12File(const char *cPath, const char *cPassword, const char *cLabel, SecIdentityRef *oIdentity);
#else
	void logErrorExtended(string prefix);
#endif

public:
	Crypto();
	~Crypto();

	/**
	 * Generates a nonce consisting of a timestamp (usec) and a random (base64) part.
	 */
	void generateAndAppendNonce(string &nonce);

#if BOOST_OS_MACOS
	/**
	 * sets the permissions on the certificate so that curl doesn't prompt
	 */
	void preAuthKey(const char *path, const char *passwd, const char *cLabel);
	void killKey(const char *cLabel);
#endif

	/**
	 * @returns true if specified signature is from the entity known by its (public) key at signaturePubKeyPath,
	 * and valid for speficied data.
	 */
	bool verifySignature(string signaturePubKeyPath, char *signatureChars, int signatureLen, string data);

};

} // namespace Passenger

#endif /* _PASSENGER_CRYPTO_H_ */
