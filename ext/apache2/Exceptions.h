#ifndef _PASSENGER_EXCEPTIONS_H_
#define _PASSENGER_EXCEPTIONS_H_

#include <apr_strings.h>
#include <exception>
#include <string>
#include <cstring>

namespace Passenger {

using namespace std;

class SystemException: public exception {
private:
	string msg;
	int m_code;
public:
	SystemException(const string &message, int errorCode) {
		char buffer[10];
		
		msg.assign(message);
		msg.append(": ");
		msg.append(strerror(errorCode));
		if (apr_snprintf(buffer, sizeof(buffer), " (%d)", errorCode) > 0) {
			msg.append(buffer);
		}
		m_code = errorCode;
	}
	
	virtual ~SystemException() throw() {
	}
	
	virtual const char *what() const throw() {
		return msg.c_str();
	}
	
	int code() const throw() {
		return m_code;
	}
};

class MemoryException: public exception {
public:
	MemoryException() {}
	virtual ~MemoryException() throw() {}
	virtual const char *what() const throw() {
		return "Unable to allocate memory.";
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

} // namespace Passenger

#endif /* _PASSENGER_EXCEPTIONS_H_ */
