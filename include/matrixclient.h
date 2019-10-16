#ifndef _matrixclient_h_
#define _matrixclient_h_

#include <string>
#include <3ds.h>
#include <jansson.h>

class MatrixClient {
private:
	std::string hsUrl;
	std::string token;
public:
	MatrixClient(std::string homeserverUrl, std::string matrixToken);
	Result doRequest(json_t* content, const char* method, std::string path, json_t* body = NULL);
};

#endif // _matrixclient_h_
