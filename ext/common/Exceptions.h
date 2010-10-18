/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_EXCEPTIONS_H_
#define _PASSENGER_EXCEPTIONS_H_

#include <oxt/tracable_exception.hpp>
#include <string>
#include <sstream>
#include <cstring>

/**
 * @defgroup Exceptions Exceptions
 */

namespace Passenger {

using namespace std;

/**
 * Represents an error returned by a system call or a standard library call.
 *
 * Use the code() method to find out the value of <tt>errno</tt> at the time
 * the error occured.
 *
 * @ingroup Exceptions
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
	SystemException(const string &briefMessage, int errorCode) {
		stringstream str;
		
		str << strerror(errorCode) << " (" << errorCode << ")";
		systemMessage = str.str();
		
		setBriefMessage(briefMessage);
		m_code = errorCode;
	}
	
	virtual ~SystemException() throw() {}
	
	virtual const char *what() const throw() {
		return fullMessage.c_str();
	}
	
	void setBriefMessage(const string &message) {
		briefMessage = message;
		fullMessage = briefMessage + ": " + systemMessage;
	}
	
	/**
	 * The value of <tt>errno</tt> at the time the error occured.
	 */
	int code() const throw() {
		return m_code;
	}
	
	/**
	 * Returns a brief version of the exception message. This message does
	 * not include the system error description, and is equivalent to the
	 * value of the <tt>message</tt> parameter as passed to the constructor.
	 */
	string brief() const throw() {
		return briefMessage;
	}
	
	/**
	 * Returns the system's error message. This message contains both the
	 * content of <tt>strerror(errno)</tt> and the errno number itself.
	 */
	string sys() const throw() {
		return systemMessage;
	}
};

/**
 * A filesystem error, as returned by the operating system. This may include,
 * for example, permission errors.
 *
 * @ingroup Exceptions
 */
class FileSystemException: public SystemException {
private:
	string m_filename;
public:
	FileSystemException(const string &message, int errorCode,
		const string &filename)
		: SystemException(message, errorCode),
		  m_filename(filename) {}
	
	virtual ~FileSystemException() throw() {}
	
	/**
	 * The filename that's associated to the error.
	 */
	string filename() const throw() {
		return m_filename;
	}
};

/**
 * Unable to retrieve the system time using <tt>time()</tt>.
 *
 * @ingroup Exceptions
 */
class TimeRetrievalException: public SystemException {
public:
	TimeRetrievalException(const string &message, int errorCode)
		: SystemException(message, errorCode)
		{}
	virtual ~TimeRetrievalException() throw() {}
};

/**
 * Represents an error that occured during an I/O operation.
 *
 * @ingroup Exceptions
 */
class IOException: public oxt::tracable_exception {
private:
	string msg;
public:
	IOException(const string &message): msg(message) {}
	virtual ~IOException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

/**
 * Thrown when a certain file cannot be found.
 */
class FileNotFoundException: public IOException {
public:
	FileNotFoundException(const string &message): IOException(message) {}
	virtual ~FileNotFoundException() throw() {}
};

/**
 * An unexpected end-of-file I/O error.
 *
 * @ingroup Exceptions
 */
class EOFException: public IOException {
public:
	EOFException(const string &message): IOException(message) {}
	virtual ~EOFException() throw() {}
};

/**
 * Thrown when an invalid configuration is given.
 */
class ConfigurationException: public oxt::tracable_exception {
private:
	string msg;
public:
	ConfigurationException(const string &message): msg(message) {}
	virtual ~ConfigurationException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

/**
 * Thrown when SpawnManager or ApplicationPool fails to spawn an application
 * instance. The exception may contain an error page, which is a user-friendly
 * HTML page with details about the error.
 */
class SpawnException: public oxt::tracable_exception {
private:
	string msg;
	bool m_hasErrorPage;
	bool m_isHTML;
	string m_errorPage;
public:
	SpawnException(const string &message)
		: msg(message) {
		m_hasErrorPage = false;
		m_isHTML = false;
	}
	
	SpawnException(const string &message, const string &errorPage, bool isHTML = true)
		: msg(message), m_errorPage(errorPage)
	{
		m_hasErrorPage = true;
		m_isHTML = isHTML;
	}
	
	virtual ~SpawnException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
	
	/**
	 * Check whether an error page is available.
	 */
	bool hasErrorPage() const {
		return m_hasErrorPage;
	}
	
	/**
	 * Return the error page content.
	 *
	 * @pre hasErrorPage()
	 */
	const string getErrorPage() const {
		return m_errorPage;
	}
	
	/**
	 * Whether the error page content is HTML.
	 *
	 * @pre hasErrorPage()
	 */
	bool isHTML() const {
		return m_isHTML;
	}
};

/**
 * Indicates that a specified argument is incorrect or violates a requirement.
 *
 * @ingroup Exceptions
 */
class ArgumentException: public oxt::tracable_exception {
private:
	string msg;
public:
	ArgumentException(const string &message): msg(message) {}
	virtual ~ArgumentException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

/*
 * @ingroup Exceptions
 */
class InvalidModeStringException: public ArgumentException {
public:
	InvalidModeStringException(const string &message): ArgumentException(message) {}
};

/**
 * A generic runtime exception.
 *
 * @ingroup Exceptions
 */
class RuntimeException: public oxt::tracable_exception {
private:
	string msg;
public:
	RuntimeException(const string &message): msg(message) {}
	virtual ~RuntimeException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

/**
 * An exception indicating that some timeout expired.
 *
 * @ingroup Exceptions
 */
class TimeoutException: public oxt::tracable_exception {
private:
	string msg;
public:
	TimeoutException(const string &message): msg(message) {}
	virtual ~TimeoutException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

/**
 * Represents some kind of security error.
 *
 * @ingroup Exceptions
 */
class SecurityException: public oxt::tracable_exception {
private:
	string msg;
public:
	SecurityException(const string &message): msg(message) {}
	virtual ~SecurityException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

/**
 * @ingroup Exceptions
 */
class NonExistentUserException: public SecurityException {
public:
	NonExistentUserException(const string &message): SecurityException(message) {}
};

/**
 * @ingroup Exceptions
 */
class NonExistentGroupException: public SecurityException {
public:
	NonExistentGroupException(const string &message): SecurityException(message) {}
};

/**
 * The application pool is too busy and cannot fulfill a get() request.
 *
 * @ingroup Exceptions
 */
class BusyException: public oxt::tracable_exception {
private:
	string msg;
public:
	BusyException(const string &message): msg(message) {}
	virtual ~BusyException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

} // namespace Passenger

#endif /* _PASSENGER_EXCEPTIONS_H_ */
