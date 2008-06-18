// Dummy implementation for backtrace.hpp.
namespace oxt {
	
using namespace std;

class TracableException: public exception {
public:
	virtual string backtrace() const throw() {
		return "     (backtrace support disabled during compile time)\n";
	}
	
	virtual const char *what() const throw() {
		return "Passenger::TracableException";
	}
};

#define TRACE_POINT() do { /* nothing */ } while (false)
#define UPDATE_TRACE_POINT() do { /* nothing */ } while (false)

} // namespace oxt

