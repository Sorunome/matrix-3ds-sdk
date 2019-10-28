#include "util.h"
#include <string>
#include <sstream>
#include <3ds.h>
#include <jansson.h>

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

char* json_object_get_string_value(json_t* obj, const char* key) {
	if (!obj) {
		return NULL;
	}
	json_t* keyObj = json_object_get(obj, key);
	if (!keyObj) {
		return NULL;
	}
	return (char*)json_string_value(keyObj);
}
