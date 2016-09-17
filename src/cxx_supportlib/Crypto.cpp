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

#include <Crypto.h>
#include <modp_b64.h>
#include <Logging.h>
#include <string>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

#if BOOST_OS_MACOS

Crypto::Crypto() {
}

Crypto::~Crypto() {
}

CFDictionaryRef Crypto::createQueryDict(const char *label) {
	if(kSecClassIdentity != NULL) {
		CFTypeRef keys[4];
		CFTypeRef values[4];
		CFDictionaryRef queryDict;
		CFStringRef cfLabel = CFStringCreateWithCString(NULL, label,
														 kCFStringEncodingUTF8);

		/* Set up our search criteria and expected results: */
		values[0] = kSecClassIdentity; /* we want a certificate and a key */
		keys[0] = kSecClass;
		values[1] = kCFBooleanTrue;    /* we need a reference */
		keys[1] = kSecReturnRef;
		values[2] = kSecMatchLimitOne; /* one is enough, thanks */
		keys[2] = kSecMatchLimit;
		/* identity searches need a SecPolicyRef in order to work */
		values[3] = SecPolicyCreateSSL(false, cfLabel);
		keys[3] = kSecMatchPolicy;
		queryDict = CFDictionaryCreate(NULL, (const void **)keys,
										(const void **)values, 4L,
										&kCFCopyStringDictionaryKeyCallBacks,
										&kCFTypeDictionaryValueCallBacks);
		CFRelease(values[3]);
		CFRelease(cfLabel);

		return queryDict;
	}
	return NULL;
}

OSStatus Crypto::lookupKeychainItem(const char *label, SecIdentityRef *oIdentity) {
	OSStatus status = errSecItemNotFound;

	CFDictionaryRef queryDict = createQueryDict(label);

	/* Do we have a match? */
	status = SecItemCopyMatching(queryDict, (CFTypeRef *)oIdentity);
	CFRelease(queryDict);

	return status;
}

SecAccessRef Crypto::createAccess(const char *cLabel) {
	SecAccessRef access=NULL;
	CFStringRef label = CFStringCreateWithCString(NULL, cLabel, kCFStringEncodingUTF8);
	if (SecAccessCreate(label, NULL, &access)){
		P_ERROR("SecAccessCreate failed.");
		CFRelease(label);
		return NULL;
	}
	CFRelease(label);
	return access;
}

OSStatus Crypto::copyIdentityFromPKCS12File(const char *cPath,
											const char *cPassword,
											const char *cLabel,
											SecIdentityRef *oIdentity) {
	OSStatus status = errSecItemNotFound;
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
																(const UInt8 *)cPath, strlen(cPath), false);
	CFStringRef password = cPassword ? CFStringCreateWithCString(NULL,
																 cPassword, kCFStringEncodingUTF8) : NULL;

	CFReadStreamRef cfrs = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
	SecTransformRef readTransform = SecTransformCreateReadTransformWithReadStream(cfrs);
	CFErrorRef error = NULL;
	CFDataRef pkcsData = (CFDataRef)SecTransformExecute(readTransform, &error);
	if (error != NULL) {
		logErrorExtended("ReadTransform", error);
		return status;
	}

	SecAccessRef access = createAccess(cLabel);
	const void *cKeys[] = {kSecImportExportPassphrase,kSecImportExportAccess};
	const void *cValues[] = {password,access};
	CFDictionaryRef options = CFDictionaryCreate(NULL, cKeys, cValues,
												 2L, NULL, NULL);
	CFArrayRef items = NULL;

	/* Here we go: */
	status = SecPKCS12Import(pkcsData, options, &items);
	if(status == noErr && items && CFArrayGetCount(items)) {
		CFDictionaryRef identityAndTrust = (CFDictionaryRef) CFArrayGetValueAtIndex(items, 0L);
		const void *tempIdentity = CFDictionaryGetValue(identityAndTrust,
														 kSecImportItemIdentity);

		/* Retain the identity; we don't care about any other data... */
		CFRetain(tempIdentity);
		*oIdentity = (SecIdentityRef)tempIdentity;
		CFRelease(identityAndTrust);
	}

	if(items) {
		CFRelease(items);
	}
	CFRelease(options);
	CFRelease(access);
	if(pkcsData){
		CFRelease(pkcsData);
	}
	CFRelease(readTransform);
	CFRelease(cfrs);
	if(password) {
		CFRelease(password);
	}
	CFRelease(url);
	return status;
}

void Crypto::killKey(const char *cLabel){
	SecIdentityRef id = NULL;
	if (lookupKeychainItem(cLabel,&id) != errSecItemNotFound){

		CFArrayRef itemList = CFArrayCreate(NULL, (const void **)&id, 1, NULL);
		const void *keys2[]   = { kSecClass,  kSecMatchItemList,  kSecMatchLimit };
		const void *values2[] = { kSecClassIdentity, itemList, kSecMatchLimitAll };

		CFDictionaryRef dict = CFDictionaryCreate(NULL, keys2, values2, 3, NULL, NULL);
		OSStatus oserr = SecItemDelete(dict);
		if (oserr) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			P_ERROR("Removing Passenger Cert from keychain failed: " << CFStringGetCStringPtr(str,kCFStringEncodingUTF8) << " Please remove the private key from the certificate labeled " << cLabel << " in your keychain.");
			CFRelease(str);
		}
		CFRelease(dict);
		CFRelease(itemList);

	}
}

void Crypto::preAuthKey(const char *path, const char *passwd, const char *cLabel){
	SecIdentityRef id = NULL;
	if(lookupKeychainItem(cLabel,&id) == errSecItemNotFound){
		OSStatus oserr = SecKeychainSetUserInteractionAllowed(false);
		if (oserr) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			P_ERROR("Disabling GUI Keychain interaction failed: " << CFStringGetCStringPtr(str,kCFStringEncodingUTF8));
			CFRelease(str);
		}
		copyIdentityFromPKCS12File(path,passwd,cLabel,&id);
		if(id == NULL){
			P_ERROR("copyIdentityFromPKCS12File failed.");
			exit(-1);
		}
		oserr = SecKeychainSetUserInteractionAllowed(true);
		if (oserr) {
			//This is really bad, we should probably ask the user to reboot.
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			P_ERROR("Re-enabling GUI Keychain interaction failed with error: " << CFStringGetCStringPtr(str,kCFStringEncodingUTF8) << " Please reboot as soon as possible, thanks.");
			CFRelease(str);
		}
	}else{
		P_ERROR("Passenger certificate was found in the keychain unexpectedly, you may see keychain popups until you remove the private key from the certificate labeled " << cLabel << " in your keychain.");
	}
	if(id){
		CFRelease(id);
	}
}

void Crypto::generateAndAppendNonce(string &nonce) {
	nonce.append(toString(SystemTime::getUsec()));

	int rndLen = 16;
	unsigned char rndChars[rndLen];

	FILE *fp = fopen("/dev/random", "r");
	if(fp == NULL) {
		CFIndex errNum = errno;
		char* errMsg = strerror(errno);
		const UInt8 numKeys = 4;
		CFStringRef userInfoKeys[numKeys] = { kCFErrorFilePathKey,
											  kCFErrorLocalizedDescriptionKey,
											  kCFErrorLocalizedFailureReasonKey,
											  kCFErrorLocalizedRecoverySuggestionKey };
		CFStringRef userInfoValues[numKeys] = { CFSTR("/dev/random"),
												CFSTR("Couldn't open file for reading."),
												CFStringCreateWithCStringNoCopy(NULL, errMsg, kCFStringEncodingUTF8, NULL),
												CFSTR("Have you tried turning it off and on again?") };

		CFErrorRef error = CFErrorCreateWithUserInfoKeysAndValues(NULL, kCFErrorDomainOSStatus, errNum, (const void *const *)userInfoKeys, (const void *const *)userInfoValues, numKeys);
		logErrorExtended("SecVerifyTransformCreate", error);
	}
	for (int i=0; i<rndLen; i++) {
		rndChars[i] = fgetc(fp);
	}
	fclose(fp);

	char rndChars64[rndLen * 2];
	modp_b64_encode(rndChars64, (const char *) rndChars, rndLen);

	nonce.append(rndChars64);
}

bool Crypto::verifySignature(string signaturePubKeyPath, char *signatureChars, int signatureLen, string data) {
	SecKeyRef rsaPubKey = NULL;
	bool result = false;

	CFErrorRef error = NULL;
	SecTransformRef verifier = NULL;
	CFNumberRef cfSize = NULL;
	do {
		rsaPubKey = loadPubKey(signaturePubKeyPath.c_str());
		if (rsaPubKey == NULL) {
			P_ERROR("Failed to load public key at " << signaturePubKeyPath);
			break;
		}

		CFDataRef signatureRef = CFDataCreateWithBytesNoCopy(NULL, (UInt8*)signatureChars, signatureLen, kCFAllocatorNull);
		//CFDataRef signatureRef = CFDataCreate(NULL, signatureChars, signatureLen);//this is safer but uses a bit more memory

		CFDataRef dataRef = CFDataCreateWithBytesNoCopy(NULL, (UInt8*)data.c_str(), data.length(), kCFAllocatorNull);
		//CFDataRef dataRef = CFDataCreate(NULL, (UInt8*)data.c_str(), data.length());

		verifier = SecVerifyTransformCreate(rsaPubKey, signatureRef, &error);
		if (error) {
			logErrorExtended("SecVerifyTransformCreate", error);
			result = -20;
			break;
		}

		SecTransformSetAttribute(verifier, kSecTransformInputAttributeName, dataRef, &error);
		if (error) {
			logErrorExtended("SecTransformSetAttribute InputName", error);
			result = -21;
			break;
		}

		SecTransformSetAttribute(verifier, kSecDigestTypeAttribute, kSecDigestSHA2, &error);
		if (error) {
			logErrorExtended("SecTransformSetAttribute DigestType", error);
			result = -22;
			break;
		}

		UInt32 size = kSecAES256; // c++ is dumb
		cfSize = CFNumberCreate(NULL, kCFNumberSInt32Type, &size);
		SecTransformSetAttribute(verifier, kSecDigestLengthAttribute, cfSize, &error);
		if (error) {
			logErrorExtended("SecTransformSetAttribute DigestLength", error);
			result = -23;
			break;
		}

		CFTypeRef verifyResult = SecTransformExecute(verifier, &error);
		if (error) {
			logErrorExtended("SecTransformExecute", error);
			result = -24;
			break;
		}

		result = (verifyResult == kCFBooleanTrue);
	} while(0);

	if (error) {
		CFRelease(error);
	}
	if (cfSize) {
		CFRelease(cfSize);
	}
	if (verifier) {
		CFRelease(verifier);
	}
	freePubKey(rsaPubKey);

	return result;
}

PUBKEY_TYPE Crypto::loadPubKey(const char *filename) {
	SecKeyRef pubKey = NULL;
	CFDataRef keyData = NULL;
	CFURLRef url = NULL;
	CFReadStreamRef cfrs = NULL;
	SecTransformRef readTransform = NULL;
	CFErrorRef error = NULL;
	CFArrayRef temparray = NULL;
	do {
		url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
				(UInt8*) filename, strlen(filename), false);
		if (url == NULL) {
			P_ERROR("CFURLCreateFromFileSystemRepresentation failed.");
			break;
		}

		cfrs = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
		if (cfrs == NULL) {
			P_ERROR("CFReadStreamCreateWithFile failed");
			break;
		}

		readTransform = SecTransformCreateReadTransformWithReadStream(cfrs);
		if (readTransform == NULL) {
			P_ERROR("SecTransformCreateReadTransformWithReadStream failed");
			break;
		}

		keyData = (CFDataRef) SecTransformExecute(readTransform, &error);
		if (keyData == NULL) {
			P_ERROR("SecTransformExecute failed to get keyData");
			break;
		}
		if (error) {
			logErrorExtended("SecTransformExecute", error);
			break;
		}

		SecExternalItemType itemType = kSecItemTypePublicKey;
		SecExternalFormat externalFormat = kSecFormatPEMSequence;
		OSStatus oserr = SecItemImport(keyData,
						   NULL, // filename or extension
						   &externalFormat, // See SecExternalFormat for details
						   &itemType, // item type
						   0, // See SecItemImportExportFlags for details, Note that PEM formatting
							  // is determined internally via inspection of the incoming data, so
							  // the kSecItemPemArmour flag is ignored.
						   NULL, //&params,
						   NULL, // Don't import into a keychain
						   &temparray);
		if (oserr) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			P_ERROR("SecItemImport: " << CFStringGetCStringPtr(str,kCFStringEncodingUTF8));
			CFRelease(str);
			break;
		}
		pubKey = (SecKeyRef)CFArrayGetValueAtIndex(temparray, 0);
		CFRetain(pubKey); //bump ref count, now we own this and need to release it eventually
	} while (0);

	if (keyData) {
		CFRelease(keyData); //might be wrong to release here, not sure if SecItemImport makes a copy of the bytes, it looks like it does
		//https://opensource.apple.com/source/libsecurity_keychain/libsecurity_keychain-55035/lib/SecImport.cpp
		//http://opensource.apple.com//source/libsecurity_keychain/libsecurity_keychain-14/lib/SecImportExportPem.cpp
	}
	if (temparray) {
		CFRelease(temparray);
	}
	if (readTransform) {
		CFRelease(readTransform);
	}
	if (cfrs) {
		CFRelease(cfrs);
	}
	if (url) {
		CFRelease(url);
	}
	if (error) {
		CFRelease(error);
	}

	return pubKey;
}

void Crypto::freePubKey(PUBKEY_TYPE pubKey) {
	if (pubKey) {
		CFRelease(pubKey);
	}
}

void Crypto::logErrorExtended(string prefix, CFErrorRef error) {
	if (error) {
		CFStringRef description = CFErrorCopyDescription((CFErrorRef)error);
		CFStringRef failureReason = CFErrorCopyFailureReason((CFErrorRef)error);
		CFStringRef recoverySuggestion = CFErrorCopyRecoverySuggestion((CFErrorRef)error);

		P_ERROR(prefix
				<< ": "<< CFStringGetCStringPtr(description, kCFStringEncodingUTF8)
				<< "; "<< CFStringGetCStringPtr(failureReason, kCFStringEncodingUTF8)
				<< "; "<< CFStringGetCStringPtr(recoverySuggestion, kCFStringEncodingUTF8)
				);

		CFRelease(recoverySuggestion);
		CFRelease(failureReason);
		CFRelease(description);
		CFRelease(error);
	}
}

#else

Crypto::Crypto() {
	OpenSSL_add_all_algorithms();
}

Crypto::~Crypto() {
	EVP_cleanup();
}

void Crypto::generateAndAppendNonce(string &nonce) {
	nonce.append(toString(SystemTime::getUsec()));

	int rndLen = 16;
	unsigned char rndChars[rndLen];
	RAND_bytes(rndChars, rndLen);

	char rndChars64[rndLen * 2];
	modp_b64_encode(rndChars64, (const char *) rndChars, rndLen);

	nonce.append(rndChars64);
}

bool Crypto::verifySignature(string signaturePubKeyPath, char *signatureChars, int signatureLen, string data) {
	RSA *rsaPubKey = NULL;
	EVP_PKEY *rsaPubKeyEVP = NULL;
	EVP_MD_CTX *mdctx = NULL;
	bool result = false;

	do {
		rsaPubKey = loadPubKey(signaturePubKeyPath.c_str());
		if (rsaPubKey == NULL) {
			P_ERROR("Failed to load public key at " << signaturePubKeyPath);
			break;
		}

		rsaPubKeyEVP = EVP_PKEY_new();
		if (!EVP_PKEY_assign_RSA(rsaPubKeyEVP, rsaPubKey)) {
			freePubKey(rsaPubKey);
			logErrorExtended("EVP_PKEY_assign_RSA");
			break;
		}

		if (!(mdctx = EVP_MD_CTX_create())) {
			logErrorExtended("EVP_MD_CTX_create");
			break;
		}

		if (1 != EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, rsaPubKeyEVP)) {
			logErrorExtended("EVP_DigestVerifyInit");
			break;
		}

		if (1 != EVP_DigestVerifyUpdate(mdctx, data.c_str(), data.length())) {
			logErrorExtended("EVP_DigestVerifyUpdate");
			break;
		}

		if (1 != EVP_DigestVerifyFinal(mdctx, (unsigned char *) signatureChars, signatureLen)) {
			logErrorExtended("EVP_DigestVerifyFinal");
			break;
		}

		result = true;
	} while(0);

	if (mdctx) {
		EVP_MD_CTX_destroy(mdctx);
	}

	if (rsaPubKeyEVP) {
		EVP_PKEY_free(rsaPubKeyEVP);
		// freePubKey not needed, already free by EVP_PKEY_free.
	}

	return result;
}

PUBKEY_TYPE Crypto::loadPubKey(const char *filename) {
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) {
		return NULL;
	}

	RSA *rsa = RSA_new();
	rsa = PEM_read_RSA_PUBKEY(fp, &rsa, NULL, NULL);
	fclose(fp);
	return rsa;
}

void Crypto::freePubKey(PUBKEY_TYPE pubKey) {
	if (pubKey) {
		RSA_free(pubKey);
	}
}

void Crypto::logErrorExtended(string prefix) {
	char err[500];
	ERR_load_crypto_strings();
	ERR_error_string(ERR_get_error(), err);
	P_ERROR(prefix << ": " << err);
	ERR_free_strings();
}

#endif

} // namespace Passenger
