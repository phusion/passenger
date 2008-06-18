// Actual implementation for backtrace.hpp.
#include <boost/thread.hpp>
#include <sstream>
#include <list>

namespace oxt {

using namespace std;
using namespace boost;
class TracePoint;
class TracableException;
class ThreadRegistration;

boost::mutex &_getBacktraceMutex();
list<TracePoint *> *_getCurrentBacktrace();
extern boost::mutex _threadRegistrationMutex;
extern list<ThreadRegistration *> _registeredThreads;

/**
 * @internal
 */
class TracePoint {
private:
	friend class TracableException;

	std::string function;
	std::string source;
	unsigned int line;
	bool detached;

	void detach() {
		detached = true;
	}
public:
	TracePoint(const string &function, const string &source, unsigned int line) {
		this->function = function;
		this->source = source;
		this->line = line;
		detached = false;
		boost::mutex::scoped_lock l(_getBacktraceMutex());
		_getCurrentBacktrace()->push_back(this);
	}

	~TracePoint() {
		if (!detached) {
			boost::mutex::scoped_lock l(_getBacktraceMutex());
			_getCurrentBacktrace()->pop_back();
		}
	}

	void update(const std::string &source, unsigned int line) {
		this->source = source;
		this->line = line;
	}
};

class TracableException: public exception {
private:
	list<TracePoint> backtraceCopy;
	
	void copyBacktrace() {
		boost::mutex::scoped_lock l(_getBacktraceMutex());
		list<TracePoint *>::const_iterator it;
		list<TracePoint *> *bt = _getCurrentBacktrace();
		for (it = bt->begin(); it != bt->end(); it++) {
			backtraceCopy.push_back(**it);
			(*it)->detach();
		}
	}
public:
	TracableException() {
		copyBacktrace();
	}
	
	virtual string backtrace() const throw() {
		stringstream result;
		list<TracePoint>::const_iterator it;
		
		for (it = backtraceCopy.begin(); it != backtraceCopy.end(); it++) {
			result << "     in '" << it->function << "' "
				"(" << it->source << ":" << it->line << ")" <<
				std::endl;
		}
		return result.str();
	}
};

#define TRACE_POINT() Passenger::TracePoint __p(__PRETTY_FUNCTION__, __FILE__, __LINE__)
#define UPDATE_TRACE_POINT() __p.update(__FILE__, __LINE__)

/**
 * @internal
 */
struct ThreadRegistration {
	string name;
	boost::mutex *backtraceMutex;
	list<TracePoint *> *backtrace;
};

class RegisterThreadWithBacktrace {
private:
	ThreadRegistration *registration;
	list<ThreadRegistration *>::iterator it;
public:
	RegisterThreadWithBacktrace(const string &name) {
		registration = new ThreadRegistration();
		registration->name = name;
		registration->backtraceMutex = &_getBacktraceMutex();
		registration->backtrace = _getCurrentBacktrace();
		
		boost::mutex::scoped_lock l(_threadRegistrationMutex);
		_registeredThreads.push_back(registration);
		it = _registeredThreads.end();
		it--;
	}
	
	~RegisterThreadWithBacktrace() {
		boost::mutex::scoped_lock l(_threadRegistrationMutex);
		_registeredThreads.erase(it);
		delete registration;
	}
};

} // namespace oxt

