#ifndef _matrixclient_h_
#define _matrixclient_h_

#include <string>
#include <3ds.h>
#include <jansson.h>

class MatrixClient {
private:
	std::string hsUrl;
	std::string token;
	int requestId;
public:
	MatrixClient(std::string homeserverUrl, std::string matrixToken);
	std::string sendTextMessage(std::string roomId, std::string text);
	std::string sendMessage(std::string roomId, json_t* content);
	std::string sendEvent(std::string roomId, std::string eventType, json_t* content);
	json_t* doRequest(const char* method, std::string path, json_t* body = NULL);
};

#endif // _matrixclient_h_
