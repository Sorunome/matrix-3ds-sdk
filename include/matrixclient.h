#ifndef _matrixclient_h_
#define _matrixclient_h_

#include <string>
#include <3ds.h>
#include <jansson.h>

namespace Matrix {

class Store {
public:
	virtual void setSyncToken(std::string token) = 0;
	virtual std::string getSyncToken() = 0;
};

class Client {
private:
public:
	std::string hsUrl;
	std::string token;
	Store* store;
	std::string userIdCache = "";
	int requestId = 0;
	bool stopSyncing = false;
	Thread syncThread;
	void processSync(json_t* sync);
	json_t* doSync(std::string token);
	json_t* doRequest(const char* method, std::string path, json_t* body = NULL);
public:
	Client(std::string homeserverUrl, std::string matrixToken, Store* clientStore = NULL);
	std::string getUserId();
	std::string resolveRoom(std::string alias);
	std::string sendEmote(std::string roomId, std::string text);
	std::string sendNotice(std::string roomId, std::string text);
	std::string sendText(std::string roomId, std::string text);
	std::string sendMessage(std::string roomId, json_t* content);
	std::string sendEvent(std::string roomId, std::string eventType, json_t* content);
	std::string sendStateEvent(std::string roomId, std::string type, std::string stateKey, json_t* content);
	std::string redactEvent(std::string roomId, std::string eventId, std::string reason = "");
	void startSyncLoop();
	void stopSyncLoop();
	void startSync();
};

}; // namespace Matrix

#endif // _matrixclient_h_
