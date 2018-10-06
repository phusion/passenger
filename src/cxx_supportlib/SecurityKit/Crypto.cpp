/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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

#include <SecurityKit/Crypto.h>
#include <modp_b64.h>
#include <LoggingKit/LoggingKit.h>
#include <string>
#include <SystemTools/SystemTime.h>
#include <StrIntTools/StrIntUtils.h>

#if BOOST_OS_MACOS
#else
#include <openssl/aes.h>
#endif

#define AES_KEY_BYTESIZE (256/8)
#define AES_CBC_IV_BYTESIZE (128/8)

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

#if BOOST_OS_MACOS

Crypto::Crypto()
	:id(NULL) {
}

Crypto::~Crypto() {
}

CFDictionaryRef Crypto::createQueryDict(const char *label) {
	if (kSecClassIdentity != NULL) {
		const size_t size = 5L;
		CFStringRef cfLabel = CFStringCreateWithCString(NULL, label, kCFStringEncodingUTF8);
		SecPolicyRef policy = SecPolicyCreateSSL(false, NULL);
		CFTypeRef keys[] = {kSecClass, kSecReturnRef, kSecMatchLimit, kSecMatchPolicy, kSecMatchSubjectWholeString};
		CFTypeRef values[] = {kSecClassIdentity, kCFBooleanTrue, kSecMatchLimitOne, policy, cfLabel};

		CFDictionaryRef queryDict = CFDictionaryCreate(NULL, keys, values, size,
									   &kCFCopyStringDictionaryKeyCallBacks,
									   &kCFTypeDictionaryValueCallBacks);
		CFRelease(policy);
		CFRelease(cfLabel);

		return queryDict;
	}
	return NULL;
}

OSStatus Crypto::lookupKeychainItem(const char *label, SecIdentityRef *oIdentity) {
	OSStatus status = errSecItemNotFound;

	CFDictionaryRef queryDict = createQueryDict(label);
	if (queryDict) {
		/* Do we have a match? */
		status = SecItemCopyMatching(queryDict, (CFTypeRef *) oIdentity);
		CFRelease(queryDict);
	}
	return status;
}

SecAccessRef Crypto::createAccess(const char *cLabel) {
	SecAccessRef access = NULL;
	CFStringRef label = CFStringCreateWithCString(NULL, cLabel, kCFStringEncodingUTF8);
	if (SecAccessCreate(label, NULL, &access)) {
		logError("SecAccessCreate failed.");
		CFRelease(label);
		return NULL;
	}
	CFRelease(label);
	return access;
}

OSStatus Crypto::copyIdentityFromPKCS12File(const char *cPath,
											const char *cPassword,
											const char *cLabel) {
	OSStatus status = errSecItemNotFound;
	if (strlen(cPath) == 0) {
		return errSecMissingValue;
	}
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
														   (const UInt8 *) cPath, strlen(cPath), false);
	CFStringRef password = cPassword ? CFStringCreateWithCString(NULL,
																 cPassword, kCFStringEncodingUTF8) : NULL;

	CFReadStreamRef cfrs = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
	SecTransformRef readTransform = SecTransformCreateReadTransformWithReadStream(cfrs);
	CFErrorRef error = NULL;
	CFDataRef pkcsData = (CFDataRef) SecTransformExecute(readTransform, &error);
	if (error != NULL) {
		logFreeErrorExtended("ReadTransform", error);
		return status;
	}

	SecAccessRef access = createAccess(cLabel);
	if (access == NULL) {
		return status;
	}
	CFTypeRef cKeys[] = {kSecImportExportPassphrase, kSecImportExportAccess};
	CFTypeRef cValues[] = {password, access};
	CFDictionaryRef options = CFDictionaryCreate(NULL, cKeys, cValues, 2L, NULL, NULL);
	CFArrayRef items = NULL;

	/* Here we go: */
	status = SecPKCS12Import(pkcsData, options, &items);
	if (!(status == noErr && items && CFArrayGetCount(items))) {
		string suffix = string("Please check for a certificate labeled: ") + cLabel + " in your keychain, and remove the associated private key. For more help please read: https://www.phusionpassenger.com/library/admin/standalone/mac_keychain_popups.html";
		string prefix = "Loading Passenger Cert failed";
		if (status == noErr) {
			status = errSecAuthFailed;
			logError( prefix + ". " + suffix );
		}else{
			CFStringRef str = SecCopyErrorMessageString(status, NULL);
			logError( prefix + ": " + CFStringGetCStringPtr(str, kCFStringEncodingUTF8) + "\n" + suffix );
			CFRelease(str);
		}
	}else{
	  id = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)CFArrayGetValueAtIndex(items, 0),kSecImportItemKeyID);
	  CFRetain(id);
	}

	if (items) {
		CFRelease(items);
	}
	CFRelease(options);
	CFRelease(access);
	if (pkcsData) {
		CFRelease(pkcsData);
	}
	CFRelease(readTransform);
	CFRelease(cfrs);
	if (password) {
		CFRelease(password);
	}
	CFRelease(url);
	return status;
}

#if PRE_HIGH_SIERRA
void Crypto::killKey(const char *cLabel) {
	SecIdentityRef id = NULL;
	OSStatus status = lookupKeychainItem(cLabel, &id);
	if (status != errSecItemNotFound) {

		CFArrayRef itemList = CFArrayCreate(NULL, (const void **) &id, 1, NULL);
		CFTypeRef keys[]   = { kSecClass,  kSecMatchItemList,  kSecMatchLimit };
		CFTypeRef values[] = { kSecClassCertificate, itemList, kSecMatchLimitOne };

		CFDictionaryRef dict = CFDictionaryCreate(NULL, keys, values, 3L, NULL, NULL);
		OSStatus oserr = SecItemDelete(dict);
		if (oserr) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			logError(string("Removing Passenger Cert from keychain failed: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
					". Please remove the certificate labeled " + cLabel + " in your keychain.");
			CFRelease(str);
		}
		CFRelease(dict);
		CFRelease(itemList);

		if(id){
			CFTypeRef keys2[]   = { kSecClass,  kSecAttrSubjectKeyID,  kSecMatchLimit };
			CFTypeRef values2[] = { kSecClassKey, id, kSecMatchLimitOne };
			dict = CFDictionaryCreate(NULL, keys2, values2, 3L, NULL, NULL);
			oserr = SecItemDelete(dict);
			if (oserr) {
				CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
				logError(string("Removing Passenger private key from keychain failed: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
						 ". Please remove the private key from the certificate labeled " + cLabel + " in your keychain.");
				CFRelease(str);
			}
			CFRelease(dict);
			CFRelease(id);
			id = NULL;
		}

	} else {
		CFStringRef str = SecCopyErrorMessageString(status, NULL);
		logError(string("Finding Passenger Cert failed: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8) );
		CFRelease(str);
	}
}

bool Crypto::preAuthKey(const char *path, const char *passwd, const char *cLabel) {
	SecIdentityRef id = NULL;
	if (lookupKeychainItem(cLabel, &id) == errSecItemNotFound) {
		OSStatus oserr = SecKeychainSetUserInteractionAllowed(false);
		if (oserr) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			logError(string("Disabling GUI Keychain interaction failed: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8));
			CFRelease(str);
		}
		oserr = copyIdentityFromPKCS12File(path, passwd, cLabel);
		bool success = (noErr == oserr);
		if (!success) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			logError(string("Pre authorizing the Passenger client certificate failed: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8));
			CFRelease(str);
		}
		oserr = SecKeychainSetUserInteractionAllowed(true);
		if (oserr) {
			//This is really bad, we should probably ask the user to reboot.
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			logError(string("Re-enabling GUI Keychain interaction failed with error: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8) +
					" Please reboot as soon as possible, thanks.");
			CFRelease(str);
		}
		return success;
	} else {
		logError(string("Passenger client certificate was found in the keychain unexpectedly, skipping security update check. Please remove the private key from the certificate labeled ") + cLabel + " in your keychain.");
		if (id) {
			CFRelease(id);
		}
		return false;
	}
}
#endif

bool Crypto::generateRandomChars(unsigned char *rndChars, int rndLen) {
	FILE *fPtr = fopen("/dev/random", "r");
	if (fPtr == NULL) {
		CFIndex errNum = errno;
		char* errMsg = strerror(errno);
		const UInt8 numKeys = 4;
		CFTypeRef userInfoKeys[numKeys] = { kCFErrorFilePathKey,
											kCFErrorLocalizedDescriptionKey,
											kCFErrorLocalizedFailureReasonKey,
											kCFErrorLocalizedRecoverySuggestionKey };
		CFTypeRef userInfoValues[numKeys] = { CFSTR("/dev/random"),
											  CFSTR("Couldn't open file for reading."),
											  CFStringCreateWithCStringNoCopy(NULL, errMsg, kCFStringEncodingUTF8, NULL),
											  CFSTR("Have you tried turning it off and on again?") };

		CFErrorRef error = CFErrorCreateWithUserInfoKeysAndValues(NULL, kCFErrorDomainOSStatus, errNum, userInfoKeys, userInfoValues, numKeys);
		logFreeErrorExtended("generateRandomChars failed", error);
		return false;
	}
	for (int i = 0; i < rndLen; i++) {
		rndChars[i] = fgetc(fPtr);
	}
	fclose(fPtr);

	return true;
}

bool Crypto::generateAndAppendNonce(string &nonce) {
	nonce.append(toString(SystemTime::getUsec()));

	int rndLen = 16;
	unsigned char rndChars[rndLen];

	if (generateRandomChars(rndChars, rndLen)) {
		char rndChars64[modp_b64_encode_len(rndLen)];
		modp_b64_encode(rndChars64, (const char *) rndChars, rndLen);

		nonce.append(rndChars64);
		return true;
	} else {
		return false;
	}
}

CFDataRef Crypto::genIV(size_t ivSize) {
	UInt8 *ivBytesPtr = (UInt8*) malloc(ivSize);//freed when iv is freed
	if (generateRandomChars(ivBytesPtr, ivSize)) {
		return CFDataCreateWithBytesNoCopy(NULL, ivBytesPtr, ivSize, kCFAllocatorMalloc);
	} else {
		return NULL;
	}
}

bool Crypto::getKeyBytes(SecKeyRef cryptokey, void **target, size_t &len) {
	const CSSM_KEY *cssmKey;
	CSSM_WRAP_KEY wrappedKey;

	CSSM_CSP_HANDLE cspHandle = 0;
	CSSM_CC_HANDLE ccHandle = 0;

	const CSSM_ACCESS_CREDENTIALS *creds;
	CSSM_RETURN error = SecKeyGetCredentials(cryptokey,
											 CSSM_ACL_AUTHORIZATION_EXPORT_WRAPPED,
											 kSecCredentialTypeDefault, &creds);
	if (error != CSSM_OK) { cssmPerror("SecKeyGetCredentials", error); }

	error = SecKeyGetCSSMKey(cryptokey, &cssmKey);
	if (error != CSSM_OK) { cssmPerror("SecKeyGetCSSMKey", error); }

	error = SecKeyGetCSPHandle(cryptokey, &cspHandle);
	if (error != CSSM_OK) { cssmPerror("SecKeyGetCSPHandle", error); }

	error = CSSM_CSP_CreateSymmetricContext(cspHandle,
											CSSM_ALGID_NONE,
											CSSM_ALGMODE_NONE,
											creds,
											NULL,
											NULL,
											CSSM_PADDING_NONE,
											0,
											&ccHandle);
	if (error != CSSM_OK) { cssmPerror("CSSM_CSP_CreateSymmetricContext",error); }

	memset(&wrappedKey, 0, sizeof(wrappedKey));
	error = CSSM_WrapKey(ccHandle,
				 creds,
				 cssmKey,
				 NULL,
				 &wrappedKey);
	if (error != CSSM_OK) { cssmPerror("CSSM_WrapKey", error); }

	CSSM_DeleteContext(ccHandle);

	len = wrappedKey.KeyData.Length;

	return innerMemoryBridge(wrappedKey.KeyData.Data,target,wrappedKey.KeyData.Length);
}

bool Crypto::encryptAES256(char *dataChars, size_t dataLen, AESEncResult &aesEnc) {
	CFErrorRef error = NULL;
	bool retVal = false;

	CFNumberRef cfSize = NULL;
	CFDictionaryRef parameters = NULL;
	SecKeyRef cryptokey = NULL;
	CFDataRef iv = NULL;
	SecTransformRef encrypt = NULL;
	CFDataRef message = NULL;
	CFDataRef enc = NULL;

	do {
		UInt32 size = kSecAES256; // c++ is dumb
		CFNumberRef cfSize = CFNumberCreate(NULL, kCFNumberSInt32Type, &size);
		CFTypeRef cKeys[] = {kSecAttrKeyType, kSecAttrKeySizeInBits};
		CFTypeRef cValues[] = {kSecAttrKeyTypeAES, cfSize};
		CFDictionaryRef parameters = CFDictionaryCreate(NULL, cKeys, cValues, 2L, NULL, NULL);
		if (parameters == NULL) {
			logError("CFDictionaryCreate failed.");
			retVal = false;
			break;
		}

		SecKeyRef cryptokey = SecKeyGenerateSymmetric(parameters, &error);
		if (error != NULL) {
			logFreeErrorExtended("SecKeyGenerateSymmetric", error);
			retVal = false;
			break;
		}

		if (!getKeyBytes(cryptokey, (void **) &aesEnc.key, aesEnc.keyLen)) {
			retVal = false;
			break;
		}

		CFDataRef iv = genIV(AES_CBC_IV_BYTESIZE);
		if (iv == NULL) {
			logError("genIV failed.");
			retVal = false;
			break;
		} else if (!memoryBridge(iv, (void **) &aesEnc.iv, aesEnc.ivLen)) {
			retVal = false;
			break;
		}

		SecTransformRef encrypt = SecEncryptTransformCreate(cryptokey, &error);
		if (error != NULL) {
			logFreeErrorExtended("SecEncryptTransformCreate", error);
			retVal = false;
			break;
		}
		SecTransformSetAttribute(encrypt, kSecIVKey, iv, &error);
		if (error != NULL) {
			logFreeErrorExtended("SecTransformSetAttribute", error);
			retVal = false;
			break;
		}
		CFDataRef message = CFDataCreateWithBytesNoCopy(NULL,
														(UInt8*) dataChars,
														dataLen,
														kCFAllocatorNull);
		SecTransformSetAttribute(encrypt, kSecTransformInputAttributeName, message, &error);
		if (error != NULL) {
			logFreeErrorExtended("SecTransformSetAttribute", error);
			retVal = false;
			break;
		}
		CFDataRef enc = (CFDataRef) SecTransformExecute(encrypt, &error);
		if (error != NULL) {
			logFreeErrorExtended("SecTransformExecute", error);
			retVal = false;
			break;
		}

		if (!memoryBridge(enc, (void **) &aesEnc.encrypted, aesEnc.encryptedLen)) {
			retVal = false;
			break;
		}
		retVal = true;
	} while (false);

	if (enc) { CFRelease(enc); }
	if (message) { CFRelease(message); }
	if (encrypt) { CFRelease(encrypt); }
	if (iv) { CFRelease(iv); }
	if (cryptokey) { CFRelease(cryptokey); }
	if (parameters) { CFRelease(parameters); }
	if (cfSize) { CFRelease(cfSize); }

	return retVal;
}

void Crypto::freeAESEncrypted(AESEncResult &aesEnc) {
	if (aesEnc.encrypted != NULL) {
		free(aesEnc.encrypted);
		aesEnc.encrypted = NULL;
	}
	if (aesEnc.iv != NULL) {
		free(aesEnc.iv);
		aesEnc.iv = NULL;
	}
	if (aesEnc.key != NULL) {
		memset(aesEnc.key, 0, aesEnc.keyLen);
		free(aesEnc.key);
		aesEnc.key = NULL;
	}
}

bool Crypto::encryptRSA(unsigned char *dataChars, size_t dataLen,
						string encryptPubKeyPath, unsigned char **encryptedCharsPtr, size_t &encryptedLen) {
	bool retVal = false;
	CFErrorRef error = NULL;
	SecKeyRef rsaPubKey = loadPubKey(encryptPubKeyPath.c_str());

	CFDataRef aesKeyData = NULL;
	SecTransformRef rsaEncryptContext = NULL;
	CFDataRef encryptedKey = NULL;

	do {
		if (rsaPubKey == NULL) {
			logError("loadPubKey failed");
			retVal = false;
			break;
		}

		aesKeyData = CFDataCreateWithBytesNoCopy(NULL, (UInt8*) dataChars, dataLen, kCFAllocatorNull);
		if (aesKeyData == NULL) {
			logError("CFDataCreateWithBytesNoCopy failed");
			retVal = false;
			break;
		}

		rsaEncryptContext = SecEncryptTransformCreate(rsaPubKey, &error);
		if (error) {
			logFreeErrorExtended("SecEncryptTransformCreate", error);
			retVal = false;
			break;
		}
		SecTransformSetAttribute(rsaEncryptContext,
								 kSecPaddingKey,
								 kSecPaddingOAEPKey,
								 &error);
		if (error) {
			logFreeErrorExtended("SecTransformSetAttribute", error);
			retVal = false;
			break;
		}

		SecTransformSetAttribute(rsaEncryptContext, kSecTransformInputAttributeName, aesKeyData, &error);
		if (error) {
			logFreeErrorExtended("SecTransformSetAttribute", error);
			retVal = false;
			break;
		}
		encryptedKey = (CFDataRef) SecTransformExecute(rsaEncryptContext, &error);
		if (error) {
			logFreeErrorExtended("SecTransformExecute", error);
			retVal = false;
			break;
		}

		if (!memoryBridge(encryptedKey, (void **) encryptedCharsPtr, encryptedLen)) {
			retVal = false;
			break;
		}
		retVal = true;
	} while (false);

	if (encryptedKey) { CFRelease(encryptedKey); }
	if (rsaEncryptContext) { CFRelease(rsaEncryptContext); }
	if (aesKeyData) { CFRelease(aesKeyData); }
	if (rsaPubKey) { CFRelease(rsaPubKey); }

	return retVal;
}

bool Crypto::memoryBridge(CFDataRef input, void **target, size_t &len) {
	len = CFDataGetLength(input);
	return innerMemoryBridge((void *) CFDataGetBytePtr(input), target, len);
}

bool Crypto::innerMemoryBridge(void *input, void **target, size_t len){
	*target = malloc(len);
	if (*target == NULL) {
		logError("malloc failed: " + toString(len));
		return false;
	}
	memcpy(*target, input, len);
	return true;
}

bool Crypto::verifySignature(string signaturePubKeyPath, char *signatureChars, int signatureLen, string data) {
	SecKeyRef rsaPubKey = NULL;
	bool result = false;

	SecTransformRef verifier = NULL;
	CFNumberRef cfSize = NULL;
	do {
		rsaPubKey = loadPubKey(signaturePubKeyPath.c_str());
		if (rsaPubKey == NULL) {
			logError("Failed to load public key at " + signaturePubKeyPath);
			break;
		}

		CFDataRef signatureRef = CFDataCreateWithBytesNoCopy(NULL, (UInt8*) signatureChars, signatureLen, kCFAllocatorNull);

		CFDataRef dataRef = CFDataCreateWithBytesNoCopy(NULL, (UInt8*) data.c_str(), data.length(), kCFAllocatorNull);

		CFErrorRef error = NULL;
		verifier = SecVerifyTransformCreate(rsaPubKey, signatureRef, &error);
		if (error) {
			logFreeErrorExtended("SecVerifyTransformCreate", error);
			result = -20;
			break;
		}

		SecTransformSetAttribute(verifier, kSecTransformInputAttributeName, dataRef, &error);
		if (error) {
			logFreeErrorExtended("SecTransformSetAttribute InputName", error);
			result = -21;
			break;
		}

		SecTransformSetAttribute(verifier, kSecDigestTypeAttribute, kSecDigestSHA2, &error);
		if (error) {
			logFreeErrorExtended("SecTransformSetAttribute DigestType", error);
			result = -22;
			break;
		}

		UInt32 size = kSecAES256; // c++ is dumb
		cfSize = CFNumberCreate(NULL, kCFNumberSInt32Type, &size);
		SecTransformSetAttribute(verifier, kSecDigestLengthAttribute, cfSize, &error);
		if (error) {
			logFreeErrorExtended("SecTransformSetAttribute DigestLength", error);
			result = -23;
			break;
		}

		CFTypeRef verifyResult = SecTransformExecute(verifier, &error);
		if (error) {
			logFreeErrorExtended("SecTransformExecute", error);
			result = -24;
			break;
		}

		result = (verifyResult == kCFBooleanTrue);
	} while(0);

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
	CFArrayRef temparray = NULL;
	do {
		url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
													  (UInt8*) filename, strlen(filename), false);
		if (url == NULL) {
			logError("CFURLCreateFromFileSystemRepresentation failed.");
			break;
		}

		cfrs = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
		if (cfrs == NULL) {
			logError("CFReadStreamCreateWithFile failed");
			break;
		}

		readTransform = SecTransformCreateReadTransformWithReadStream(cfrs);
		if (readTransform == NULL) {
			logError("SecTransformCreateReadTransformWithReadStream failed");
			break;
		}
		CFErrorRef error = NULL;
		keyData = (CFDataRef) SecTransformExecute(readTransform, &error);
		if (keyData == NULL) {
			logError("SecTransformExecute failed to get keyData");
			break;
		}
		if (error) {
			logFreeErrorExtended("SecTransformExecute", error);
			break;
		}

		SecExternalItemType itemType = kSecItemTypePublicKey;
		SecExternalFormat externalFormat = kSecFormatPEMSequence;
		OSStatus oserr = SecItemImport(keyData,
									   NULL, // filename or extension
									   &externalFormat, // See SecExternalFormat for details
									   &itemType,
									   0, // See SecItemImportExportFlags for details, Note that PEM formatting
									   // is determined internally via inspection of the incoming data, so
									   // the kSecItemPemArmour flag is ignored.
									   NULL,
									   NULL, // Don't import into a keychain
									   &temparray);
		if (oserr) {
			CFStringRef str = SecCopyErrorMessageString(oserr, NULL);
			logError(string("SecItemImport: ") + CFStringGetCStringPtr(str, kCFStringEncodingUTF8));
			CFRelease(str);
			break;
		}
		pubKey = (SecKeyRef) CFArrayGetValueAtIndex(temparray, 0);
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

	return pubKey;
}

void Crypto::freePubKey(PUBKEY_TYPE pubKey) {
	if (pubKey) {
		CFRelease(pubKey);
	}
}

void Crypto::logFreeErrorExtended(const StaticString &prefix, CFErrorRef &error) {
	if (error) {
		CFStringRef description = CFErrorCopyDescription((CFErrorRef) error);
		CFStringRef failureReason = CFErrorCopyFailureReason((CFErrorRef) error);
		CFStringRef recoverySuggestion = CFErrorCopyRecoverySuggestion((CFErrorRef) error);

		logError(prefix +
				": " + CFStringGetCStringPtr(description, kCFStringEncodingUTF8) +
				"; " + CFStringGetCStringPtr(failureReason, kCFStringEncodingUTF8) +
				"; " + CFStringGetCStringPtr(recoverySuggestion, kCFStringEncodingUTF8)
				);

		CFRelease(recoverySuggestion);
		CFRelease(failureReason);
		CFRelease(description);
		CFRelease(error);
		error = NULL;
	}
}

#else

Crypto::Crypto() {
	OpenSSL_add_all_algorithms();
}

Crypto::~Crypto() {
	EVP_cleanup();
}

bool Crypto::generateAndAppendNonce(string &nonce) {
	nonce.append(toString(SystemTime::getUsec()));

	int rndLen = 16;
	unsigned char rndChars[rndLen];

	if (1 != RAND_bytes(rndChars, rndLen)) {
		logErrorExtended("RAND_bytes failed for nonce");
		return false;
	}

	char rndChars64[modp_b64_encode_len(rndLen)];
	modp_b64_encode(rndChars64, (const char *) rndChars, rndLen);

	nonce.append(rndChars64);
	return true;
}

bool Crypto::encryptAES256(char *dataChars, size_t dataLen, AESEncResult &aesEnc) {
	assert(dataLen > 0 && dataChars != NULL);

	aesEnc.encrypted = NULL;
	aesEnc.key = NULL;
	aesEnc.iv = NULL;

	bool result = false;
	EVP_CIPHER_CTX *aesEncryptContext = NULL;

	do {
		// 1. Generate random key (secret) and init vector to be used for the encryption
		aesEnc.keyLen = AES_KEY_BYTESIZE;
		aesEnc.key = (unsigned char*) OPENSSL_malloc(aesEnc.keyLen);
		if (aesEnc.key == NULL) {
			logErrorExtended("OPENSSL_malloc failed");
			break;
		}

		aesEnc.ivLen = AES_CBC_IV_BYTESIZE;
		aesEnc.iv = (unsigned char*) malloc(aesEnc.ivLen); // not secret
		if (aesEnc.iv == NULL) {
			logError("malloc for IV failed");
			break;
		}

		if (1 != RAND_bytes(aesEnc.key, aesEnc.keyLen)) {
			logErrorExtended("RAND_bytes failed for AES key");
			break;
		}
		if (1 != RAND_bytes(aesEnc.iv, aesEnc.ivLen)) {
			logErrorExtended("RAND_bytes failed for IV");
			break;
		}

		// 2. Get ready to encrypt
		aesEncryptContext = EVP_CIPHER_CTX_new();
		if (aesEncryptContext == NULL) {
			logErrorExtended("EVP_CIPHER_CTX_new failed");
			break;
		}

		aesEnc.encrypted = (unsigned char*) malloc(dataLen + AES_BLOCK_SIZE); // not secret
		if (aesEnc.encrypted == NULL) {
			logError("malloc for encryptedChars failed " + toString(dataLen + AES_BLOCK_SIZE));
			break;
		}

		// 3. Let's go
		if (1 != EVP_EncryptInit_ex(aesEncryptContext, EVP_aes_256_cbc(), NULL, aesEnc.key, aesEnc.iv)) {
			logErrorExtended("EVP_EncryptInit_ex failed");
			break;
		}

		size_t blockLength = 0;
		size_t writeIdx = 0;
		if (1 != EVP_EncryptUpdate(aesEncryptContext, aesEnc.encrypted + writeIdx, (int *) &blockLength, (unsigned char*) dataChars, dataLen)) {
			logErrorExtended("EVP_EncryptUpdate failed");
			break;
		}
		writeIdx += blockLength;

		if (1 != EVP_EncryptFinal_ex(aesEncryptContext, aesEnc.encrypted + writeIdx, (int *) &blockLength)) {
			logErrorExtended("EVP_EncryptFinal_ex failed");
			break;
		}
		writeIdx += blockLength;
		aesEnc.encryptedLen = writeIdx;

		result = true;
	} while(0);

	if (!result) {
		// convenience: free the result if it's not valid anyway
		freeAESEncrypted(aesEnc);
	}

	if (aesEncryptContext != NULL) {
		EVP_CIPHER_CTX_free(aesEncryptContext);
	}

	return result;
}

void Crypto::freeAESEncrypted(AESEncResult &aesEnc) {
	if (aesEnc.encrypted != NULL) {
		free(aesEnc.encrypted);
		aesEnc.encrypted = NULL;
	}
	if (aesEnc.iv != NULL) {
		free(aesEnc.iv);
		aesEnc.iv = NULL;
	}

	// Secret parts were allocated differently
	if (aesEnc.key != NULL) {
		OPENSSL_free(aesEnc.key);
		aesEnc.key = NULL;
	}
}

bool Crypto::encryptRSA(unsigned char *dataChars, size_t dataLen,
		string encryptPubKeyPath, unsigned char **encryptedCharsPtr, size_t &encryptedLen) {
	RSA *rsaPubKey = NULL;
	EVP_PKEY *rsaPubKeyEVP = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	bool result = false;

	do {
		// 1. Get the RSA public key to encrypt with
		rsaPubKey = loadPubKey(encryptPubKeyPath.c_str());
		if (rsaPubKey == NULL) {
			logError("Failed to load public key at " + encryptPubKeyPath);
			break;
		}

		rsaPubKeyEVP = EVP_PKEY_new();
		if (1 != EVP_PKEY_assign_RSA(rsaPubKeyEVP, rsaPubKey)) {
			logErrorExtended("EVP_PKEY_assign_RSA");
			freePubKey(rsaPubKey); // since it's not tied to EVP key yet
			break;
		}

		// 2. Prepare for encryption
		ctx = EVP_PKEY_CTX_new(rsaPubKeyEVP, NULL);
		if (ctx == NULL) {
			logErrorExtended("EVP_PKEY_CTX_new");
			break;
		}
		if (1 != EVP_PKEY_encrypt_init(ctx)) {
			logErrorExtended("EVP_PKEY_encrypt_init");
			break;
		}
		if (1 != EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING)) {
			logErrorExtended("EVP_PKEY_CTX_set_rsa_padding");
			break;
		}

		// 3. First encrypt to get encryptedLen
		if (1 != EVP_PKEY_encrypt(ctx, NULL, &encryptedLen, (unsigned char *) dataChars, dataLen)) {
			logErrorExtended("EVP_PKEY_encrypt (first)");
			break;
		}

		*encryptedCharsPtr = (unsigned char *) malloc(encryptedLen); // not secret
		if (*encryptedCharsPtr == NULL) {
			logError("malloc for encryptedChars failed" + toString(encryptedLen));
			break;
		}

		if (1 != EVP_PKEY_encrypt(ctx, (unsigned char *) *encryptedCharsPtr, &encryptedLen,
				(unsigned char *) dataChars, AES_KEY_BYTESIZE)) {
			logErrorExtended("EVP_PKEY_encrypt (second)");
			break;
		}

		result = true;
	} while (0);

	if (ctx != NULL) {
		EVP_PKEY_CTX_free(ctx);
	}
	if (rsaPubKeyEVP != NULL) {
		EVP_PKEY_free(rsaPubKeyEVP); // also frees the rsaPubKey
	}

	return result;
}

bool Crypto::verifySignature(string signaturePubKeyPath, char *signatureChars, int signatureLen, string data) {
	RSA *rsaPubKey = NULL;
	EVP_PKEY *rsaPubKeyEVP = NULL;
	EVP_MD_CTX *mdctx = NULL;
	bool result = false;

	do {
		rsaPubKey = loadPubKey(signaturePubKeyPath.c_str());
		if (rsaPubKey == NULL) {
			logError("Failed to load public key at " + signaturePubKeyPath);
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

void Crypto::logErrorExtended(const StaticString &prefix) {
	char err[500];
	ERR_load_crypto_strings();
	ERR_error_string(ERR_get_error(), err);
	P_ERROR(prefix << ": " << err);
	ERR_free_strings();
}

#endif

void Crypto::logError(const StaticString &error) {
	P_ERROR(error);
}

} // namespace Passenger
