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
	string shortMessage;
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
		shortMessage = message;
		str << message << ": " << strerror(errorCode) << " (" << errorCode << ")";
		fullMessage = str.str();
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
	 * A brief version of the exception message. This message does
	 * not include the system error description, and is equivalent to the
	 * value of the <tt>message</tt> parameter as passed to the constructor.
	 */
	string brief() const throw() {
		return shortMessage;
	}
};

/**
 * Represents an out-of-memory error.
 *
 * @ingroup Exceptions
 */
class MemoryException: public exception {
private:
	string message;
public:
	MemoryException(): message("Unable to allocate memory.") {}
	
	MemoryException(const string &msg): message(msg) {}
	
	virtual ~MemoryException() throw() {}
	
	virtual const char *what() const throw() {
		return message.c_str();
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

} // namespace Passenger

#endif /* _PASSENGER_EXCEPTIONS_H_ */
