#ifndef _UTIL_H_
#define _UTIL_H_

#include <string>
#include <3ds.h>
#include <jansson.h>

std::string urlencode(std::string str);

Result httpcDownloadDataTimeout(httpcContext *context, u8* buffer, u32 size, u32 *downloadedsize, u64 timeout);

char* json_object_get_string_value(json_t* obj, const char* key);

#endif // _UTIL_H_
