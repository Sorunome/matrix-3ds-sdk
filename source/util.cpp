#include "util.h"
#include <string>
#include <sstream>

// from http://www.zedwood.com/article/cpp-urlencode-function
std::string urlencode(std::string s) {
	static const char lookup[]= "0123456789abcdef";
	std::stringstream e;
	for(int i = 0, ix = s.length(); i < ix; i++) {
		const char& c = s[i];
		if ( (48 <= c && c <= 57) ||//0-9
			 (65 <= c && c <= 90) ||//abc...xyz
			 (97 <= c && c <= 122) || //ABC...XYZ
			 (c=='-' || c=='_' || c=='.' || c=='~') 
		) {
			e << c;
		} else {
			e << '%';
			e << lookup[ (c&0xF0)>>4 ];
			e << lookup[ (c&0x0F) ];
		}
	}
	return e.str();
}
