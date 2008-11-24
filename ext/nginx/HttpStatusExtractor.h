#include <string>

namespace Passenger {

using namespace std;

class HttpStatusExtractor {
private:
	static const char CR = '\x0D';
	static const char LF = '\x0A';

	string buffer;
	unsigned int searchStart;
	bool fullHeaderReceived;
	string statusLine;
	
	bool extractStatusLine() {
		static const char statusHeaderName[] = "Status: ";
		string::size_type start_pos, newline_pos;
		
		if (buffer.size() > sizeof(statusHeaderName) - 1
		 && memcmp(buffer.c_str(), statusHeaderName, sizeof(statusHeaderName) - 1) == 0) {
			// Status line starts at beginning of header.
			start_pos = sizeof(statusHeaderName) - 1;
			newline_pos = buffer.find("\x0D\x0A", 0, 2) + 2;
		} else {
			start_pos = buffer.find("\x0D\x0AStatus: ");
			if (start_pos != string::npos) {
				start_pos += 2 + sizeof(statusHeaderName) - 1;
				newline_pos = buffer.find("\x0D\x0A", start_pos, 2) + 2;
			}
		}
		if (start_pos != string::npos) {
			statusLine = buffer.substr(start_pos, newline_pos - start_pos);
			return true;
		} else {
			statusLine = "200 OK";
			return false;
		}
	}
	
public:
	HttpStatusExtractor() {
		searchStart = 0;
		fullHeaderReceived = false;
		statusLine = "200 OK";
	}
	
	bool feed(const char *data, unsigned int size) {
		if (fullHeaderReceived) {
			return true;
		}
		buffer.append(data, size);
		for (; searchStart < buffer.size() - 3; searchStart++) {
			if (buffer[searchStart] == CR &&
			    buffer[searchStart + 1] == LF &&
			    buffer[searchStart + 2] == CR &&
			    buffer[searchStart + 3] == LF) {
				fullHeaderReceived = true;
				extractStatusLine();
				return true;
			}
		}
		return false;
	}
	
	string getStatusLine() const {
		return statusLine;
	}
	
	string getBuffer() const {
		return buffer;
	}
};

} // namespace Passenger
