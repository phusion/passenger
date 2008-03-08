#ifndef _PASSENGER_EXCEPTIONS_H_
#define _PASSENGER_EXCEPTIONS_H_

#include <exception>
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
class SystemException: public exception {
private:
	string briefMessage;
	string systemMessage;
	string fullMessage;
	int m_code;
public:
	/**
	 * Create a new SystemException.
	 *
	 * @param message A message describing the error.
	 * @param errorCode The error code, i.e. the value of errno right after the error occured.
	 * @note A system description of the error will be appended to the given message.
	 *    For example, if <tt>errorCode</tt> is <tt>EBADF</tt>, and <tt>message</tt>
	 *    is <em>"Something happened"</em>, then what() will return <em>"Something happened: Bad
	 *    file descriptor (10)"</em> (if 10 is the number for EBADF).
	 * @post code() == errorCode
	 * @post brief() == message
	 */
	SystemException(const string &message, int errorCode) {
		stringstream str;
		
		briefMessage = message;
		str << strerror(errorCode) << " (" << errorCode << ")";
		systemMessage = str.str();
		
		fullMessage = briefMessage + ": " + systemMessage;
		m_code = errorCode;
	}
	
	virtual ~SystemException() throw() {}
	
	virtual const char *what() const throw() {
		return fullMessage.c_str();
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
 * Represents an error that occured during an I/O operation.
 *
 * @ingroup Exceptions
 */
class IOException: public exception {
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
 * Thrown when SpawnManager or ApplicationPool fails to spawn an application
 * instance. The exception may contain an error page, which is a user-friendly
 * HTML page with details about the error.
 */
class SpawnException: public exception {
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

} // namespace Passenger

#endif /* _PASSENGER_EXCEPTIONS_H_ */
