/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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

#include <Exceptions.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <cstring>
#include <cassert>


void
pp_error_init(PP_Error *error) {
	error->message = NULL;
	error->errnoCode = PP_NO_ERRNO;
	error->messageIsStatic = 0;
}

void
pp_error_destroy(PP_Error *error) {
	if (!error->messageIsStatic) {
		free(static_cast<void *>(const_cast<char *>(error->message)));
		error->message = NULL;
		error->messageIsStatic = 0;
	}
}

void
pp_error_set(const std::exception &ex, PP_Error *error) {
	const Passenger::SystemException *sys_e;

	if (error == NULL) {
		return;
	}

	if (error->message != NULL && !error->messageIsStatic) {
		free(static_cast<void *>(const_cast<char *>(error->message)));
	}

	error->message = strdup(ex.what());
	error->messageIsStatic = error->message == NULL;
	if (error->message == NULL) {
		error->message = "Unknown error message (unable to allocate memory for the message)";
	}

	sys_e = dynamic_cast<const Passenger::SystemException *>(&ex);
	if (sys_e != NULL) {
		error->errnoCode = sys_e->code();
	} else {
		error->errnoCode = PP_NO_ERRNO;
	}
}


namespace Passenger {

SystemException::SystemException(const string &briefMessage, int errorCode) {
	stringstream str;
	str << strerror(errorCode) << " (errno=" << errorCode << ")";
	systemMessage = str.str();
	setBriefMessage(briefMessage);
	m_code = errorCode;
}

SystemException::~SystemException() noexcept {}

const char *
SystemException::what() const noexcept {
	return fullMessage.c_str();
}

void
SystemException::setBriefMessage(const string &message) {
	briefMessage = message;
	fullMessage = briefMessage + ": " + systemMessage;
}

int
SystemException::code() const noexcept {
	return m_code;
}

string
SystemException::brief() const noexcept {
	return briefMessage;
}

string
SystemException::sys() const noexcept {
	return systemMessage;
}


FileSystemException::FileSystemException(const string &message, int errorCode, const string &filename)
	: SystemException(message, errorCode), m_filename(filename) {}

FileSystemException::~FileSystemException() noexcept {}

string
FileSystemException::filename() const noexcept {
	return m_filename;
}


TimeRetrievalException::TimeRetrievalException(const string &message, int errorCode)
	: SystemException(message, errorCode) {}

TimeRetrievalException::~TimeRetrievalException() noexcept {}


IOException::IOException(const string &message) : msg(message) {}

IOException::~IOException() noexcept {}

const char *
IOException::what() const noexcept {
	return msg.c_str();
}


FileNotFoundException::FileNotFoundException(const string &message) : IOException(message) {}

FileNotFoundException::~FileNotFoundException() noexcept {}


EOFException::EOFException(const string &message) : IOException(message) {}

EOFException::~EOFException() noexcept {}


ConfigurationException::ConfigurationException(const string &message) : msg(message) {}

ConfigurationException::~ConfigurationException() noexcept {}

const char *
ConfigurationException::what() const noexcept {
	return msg.c_str();
}


GetAbortedException::GetAbortedException(const string &message) : msg(message) {}

GetAbortedException::GetAbortedException(const oxt::tracable_exception::no_backtrace &tag)
	: oxt::tracable_exception(tag) {}

GetAbortedException::~GetAbortedException() noexcept {}

const char *
GetAbortedException::what() const noexcept {
	return msg.c_str();
}


RequestQueueFullException::RequestQueueFullException(unsigned int maxQueueSize)
	: GetAbortedException(oxt::tracable_exception::no_backtrace())
{
	stringstream str;
	str << "Request queue full (configured max. size: " << maxQueueSize << ")";
	msg = str.str();
}

RequestQueueFullException::~RequestQueueFullException() noexcept {}

const char *
RequestQueueFullException::what() const noexcept {
	return msg.c_str();
}


ArgumentException::ArgumentException(const string &message) : msg(message) {}

ArgumentException::~ArgumentException() noexcept {}

const char *
ArgumentException::what() const noexcept {
	return msg.c_str();
}


InvalidModeStringException::InvalidModeStringException(const string &message) : ArgumentException(message) {}


RuntimeException::RuntimeException(const string &message) : msg(message) {}

RuntimeException::~RuntimeException() noexcept {}

const char *
RuntimeException::what() const noexcept {
	return msg.c_str();
}


TimeoutException::TimeoutException(const string &message) : msg(message) {}

TimeoutException::~TimeoutException() noexcept {}

const char *
TimeoutException::what() const noexcept {
	return msg.c_str();
}


SecurityException::SecurityException(const string &message) : msg(message) {}

SecurityException::~SecurityException() noexcept {}

const char *
SecurityException::what() const noexcept {
	return msg.c_str();
}


NonExistentUserException::NonExistentUserException(const string &message) : SecurityException(message) {}

NonExistentGroupException::NonExistentGroupException(const string &message) : SecurityException(message) {}


SyntaxError::SyntaxError(const string &message) : msg(message) {}

SyntaxError::~SyntaxError() noexcept {}

const char *
SyntaxError::what() const noexcept {
	return msg.c_str();
}

} // namespace Passenger
