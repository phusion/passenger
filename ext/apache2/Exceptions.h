#ifndef _PASSENGER_EXCEPTIONS_H_
#define _PASSENGER_EXCEPTIONS_H_

#include <apr_errno.h>

#include <exception>
#include <string>
#include <sstream>

namespace Passenger {

using namespace std;

class SystemException: public exception {
private:
	string msg;
	int m_code;
public:
	SystemException(const string &message, int errorCode) {
		stringstream str;
		str << message << ": " << strerror(errorCode) << " (" << errorCode << ")";
		msg = str.str();
		m_code = errorCode;
	}
	
	virtual ~SystemException() throw() {}
	
	virtual const char *what() const throw() {
		return msg.c_str();
	}
	
	int code() const throw() {
		return m_code;
	}
};

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

class IOException: public exception {
private:
	string msg;
public:
	IOException(const string &message): msg(message) {}
	virtual ~IOException() throw() {}
	virtual const char *what() const throw() { return msg.c_str(); }
};

class APRException: public exception {
private:
	string msg;
public:
	APRException(const string &message, apr_status_t status) {
		stringstream str;
		char buf[1024];
		str << message << ": " << apr_strerror(status, buf, sizeof(buf)) << " (" << status << ")";
		msg = str.str();
	}
	
	virtual ~APRException() throw() {}
	
	virtual const char *what() const throw() {
		return msg.c_str();
	}
};

} // namespace Passenger

#endif /* _PASSENGER_EXCEPTIONS_H_ */
