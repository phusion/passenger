/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2025 Asynchronous B.V.
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
#ifndef _PASSENGER_EXCEPTIONS_H_
#define _PASSENGER_EXCEPTIONS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Provides helper structs and functions for translating C++ exceptions
 * into C error objects.
 */

#define PP_NO_ERRNO -1

struct PP_Error {
	/** The exception message. */
	const char *message;
	/** If the original exception was a SystemException, then this
	 * field is set to the corresponding errno value. Otherwise, it
	 * is set to PP_NO_ERRNO.
	 */
	int errnoCode;
	int messageIsStatic: 1;
};

typedef struct PP_Error PP_Error;

void pp_error_init(PP_Error *error);
void pp_error_destroy(PP_Error *error);

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus

#include <oxt/tracable_exception.hpp>
#include <string>
#include <exception>


/**
 * Use as follows:
 *
 *     try {
 *         ...
 *     } catch (const std::exception &e) {
 *         pp_error_set(e, error);
 *     }
 */
void pp_error_set(const std::exception &ex, PP_Error *error);


namespace Passenger {

using namespace std;

/**
 * An error returned by a system call or a standard library call.
 *
 * Use `code()` to find out the value of `errno` at the time
 * the error occured.
 */
class SystemException: public oxt::tracable_exception {
private:
	string briefMessage;
	string systemMessage;
	string fullMessage;
	int m_code;

public:
	/**
	 * Create a new SystemException.
	 *
	 * @param briefMessage A brief message describing the error.
	 * @param errorCode The error code, i.e. the value of errno right after the error occured.
	 * @note A system description of the error will be appended to the given message.
	 *    For example, if <tt>errorCode</tt> is <tt>EBADF</tt>, and <tt>briefMessage</tt>
	 *    is <em>"Something happened"</em>, then what() will return <em>"Something happened: Bad
	 *    file descriptor (10)"</em> (if 10 is the number for EBADF).
	 * @post code() == errorCode
	 * @post brief() == briefMessage
	 */
	SystemException(const string &briefMessage, int errorCode);
	virtual ~SystemException() noexcept;
	virtual const char *what() const noexcept;
	void setBriefMessage(const string &message);
	int code() const noexcept;
	string brief() const noexcept;
	string sys() const noexcept;
};

/**
 * A filesystem error, as returned by the operating system. This may include,
 * for example, permission errors.
 */
class FileSystemException: public SystemException {
private:
	string m_filename;

public:
	FileSystemException(const string &message, int errorCode, const string &filename);
	virtual ~FileSystemException() noexcept;
	string filename() const noexcept;
};

/**
 * Unable to retrieve the system time using <tt>time()</tt>.
 */
class TimeRetrievalException: public SystemException {
public:
	TimeRetrievalException(const string &message, int errorCode);
	virtual ~TimeRetrievalException() noexcept;
};

/**
 * Error during an I/O operation.
 */
class IOException: public oxt::tracable_exception {
private:
	string msg;

public:
	IOException(const string &message);
	virtual ~IOException() noexcept;
	virtual const char *what() const noexcept;
};

/**
 * Certain file cannot be found.
 */
class FileNotFoundException: public IOException {
public:
	FileNotFoundException(const string &message);
	virtual ~FileNotFoundException() noexcept;
};

/**
 * Unexpected end-of-file I/O error.
 */
class EOFException: public IOException {
public:
	EOFException(const string &message);
	virtual ~EOFException() noexcept;
};

/**
 * Invalid configuration is given.
 */
class ConfigurationException: public oxt::tracable_exception {
private:
	string msg;
public:
	ConfigurationException(const string &message);
	virtual ~ConfigurationException() noexcept;
	virtual const char *what() const noexcept;
};

/**
 * A Pool::get() or Pool::asyncGet() request was denied.
 * The request never reached a process. This could be because, before the
 * request could reach a process, the administrator detached the containing
 * group. Or maybe the request sat in the queue for too long.
 */
class GetAbortedException: public oxt::tracable_exception {
private:
	string msg;

public:
	GetAbortedException(const string &message);
	GetAbortedException(const oxt::tracable_exception::no_backtrace &tag);
	virtual ~GetAbortedException() noexcept;
	virtual const char *what() const noexcept;
};

/**
 * A Pool::get() or Pool::asyncGet() request was denied because
 * the getWaitlist queue was full.
 */
class RequestQueueFullException: public GetAbortedException {
private:
	string msg;

public:
	RequestQueueFullException(unsigned int maxQueueSize);
	virtual ~RequestQueueFullException() noexcept;
	virtual const char *what() const noexcept;
};

/**
 * A specified argument is incorrect or violates a requirement.
 */
class ArgumentException: public oxt::tracable_exception {
private:
	string msg;
public:
	ArgumentException(const string &message);
	virtual ~ArgumentException() noexcept;
	virtual const char *what() const noexcept;
};

class InvalidModeStringException: public ArgumentException {
public:
	InvalidModeStringException(const string &message);
};

/**
 * Generic runtime exception.
 */
class RuntimeException: public oxt::tracable_exception {
private:
	string msg;
public:
	RuntimeException(const string &message);
	virtual ~RuntimeException() noexcept;
	virtual const char *what() const noexcept;
};

/**
 * Some timeout expired.
 */
class TimeoutException: public oxt::tracable_exception {
private:
	string msg;
public:
	TimeoutException(const string &message);
	virtual ~TimeoutException() noexcept;
	virtual const char *what() const noexcept;
};

/**
 * Some kind of security error.
 */
class SecurityException: public oxt::tracable_exception {
private:
	string msg;
public:
	SecurityException(const string &message);
	virtual ~SecurityException() noexcept;
	virtual const char *what() const noexcept;
};

class NonExistentUserException: public SecurityException {
public:
	NonExistentUserException(const string &message);
};

class NonExistentGroupException: public SecurityException {
public:
	NonExistentGroupException(const string &message);
};

/**
 * A parser detected a syntax error.
 */
class SyntaxError: public oxt::tracable_exception {
private:
	string msg;
public:
	SyntaxError(const string &message);
	virtual ~SyntaxError() noexcept;
	virtual const char *what() const noexcept;
};

} // namespace Passenger

#endif /* __cplusplus */

#endif /* _PASSENGER_EXCEPTIONS_H_ */
