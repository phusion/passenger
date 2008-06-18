/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_EXCEPTIONS_H_
#define _PASSENGER_EXCEPTIONS_H_

#include <oxt/tracable_exception.hpp>
#include <string>
#include <sstream>

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
	string m_errorPage;
public:
	SpawnException(const string &message)
		: msg(message) {
		m_hasErrorPage = false;
	}
	
	SpawnException(const string &message, const string &errorPage)
		: msg(message), m_errorPage(errorPage) {
		m_hasErrorPage = true;
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
