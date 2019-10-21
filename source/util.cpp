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

Result httpcDownloadDataTimeout(httpcContext *context, u8* buffer, u32 size, u32 *downloadedsize, u64 timeout)
{
	Result ret=0;
	Result dlret=HTTPC_RESULTCODE_DOWNLOADPENDING;
	u32 pos=0, sz=0;
	u32 dlstartpos=0;
	u32 dlpos=0;

	if(downloadedsize)*downloadedsize = 0;

	ret=httpcGetDownloadSizeState(context, &dlstartpos, NULL);
	if(R_FAILED(ret))return ret;

	while(pos < size && dlret==HTTPC_RESULTCODE_DOWNLOADPENDING)
	{
		sz = size - pos;

		dlret=httpcReceiveDataTimeout(context, &buffer[pos], sz, timeout);

		ret=httpcGetDownloadSizeState(context, &dlpos, NULL);
		if(R_FAILED(ret))return ret;

		pos = dlpos - dlstartpos;
	}

	if(downloadedsize)*downloadedsize = pos;

	return dlret;
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
