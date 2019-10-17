#include "../include/matrixclient.h"
#include <inttypes.h>
#include <stdio.h>
#include <3ds.h>
#include <jansson.h>
#include <malloc.h>
#include <curl/curl.h>
#include <string.h>
#include "util.h"
#include "memorystore.h"

#include <sys/socket.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000
#define SYNC_TIMEOUT 10000

#define DEBUG 0

#if DEBUG
#define D 
#else
#define D for(;0;)
#endif

namespace Matrix {

static u32 *SOC_buffer = NULL;

Client::Client(std::string homeserverUrl, std::string matrixToken, Store* clientStore) {
	hsUrl = homeserverUrl;
	token = matrixToken;
	if (!clientStore) {
		clientStore = new MemoryStore();
	}
	store = clientStore;
}

std::string Client::getToken() {
	return token;
}

bool Client::login(std::string username, std::string password) {
	json_t* request = json_object();
	json_object_set_new(request, "type", json_string("m.login.password"));
	json_t* identifier = json_object();
	json_object_set_new(identifier, "type", json_string("m.id.user"));
	json_object_set_new(identifier, "user", json_string(username.c_str()));
	json_object_set_new(request, "identifier", identifier);
	json_object_set_new(request, "password", json_string(password.c_str()));
	json_object_set_new(request, "initial_device_display_name", json_string("Nintendo 3DS"));

	json_t* ret = doRequest("POST", "/_matrix/client/r0/login", request);
	json_decref(request);

	if (!ret) {
		return false;
	}
	json_t* accessToken = json_object_get(ret, "access_token");
	if (!accessToken) {
		json_decref(ret);
		return false;
	}
	token = json_string_value(accessToken);
	json_decref(ret);
	return true;
}

std::string Client::getUserId() {
	if (userIdCache != "") {
		return userIdCache;
	}
	json_t* ret = doRequest("GET", "/_matrix/client/r0/account/whoami");
	if (!ret) {
		return "";
	}
	json_t* userId = json_object_get(ret, "user_id");
	if (!userId) {
		json_decref(ret);
		return "";
	}
	const char* userIdStr = json_string_value(userId);
	json_decref(ret);
	userIdCache = std::string(userIdStr);
	return userIdCache;
}

std::string Client::resolveRoom(std::string alias) {
	if (alias[0] == '!') {
		return alias; // this is already a room ID, nothing to do
	}
	json_t* ret = doRequest("GET", "/_matrix/client/r0/directory/room/" + urlencode(alias));
	if (!ret) {
		return "";
	}
	json_t* roomId = json_object_get(ret, "room_id");
	if (!roomId) {
		json_decref(ret);
		return "";
	}
	const char* roomIdStr = json_string_value(roomId);
	json_decref(ret);
	return roomIdStr;
}

std::string Client::sendEmote(std::string roomId, std::string text) {
	json_t* request = json_object();
	json_object_set_new(request, "msgtype", json_string("m.emote"));
	json_object_set_new(request, "body", json_string(text.c_str()));
	std::string eventId = sendMessage(roomId, request);
	json_decref(request);
	return eventId;
}

std::string Client::sendNotice(std::string roomId, std::string text) {
	json_t* request = json_object();
	json_object_set_new(request, "msgtype", json_string("m.notice"));
	json_object_set_new(request, "body", json_string(text.c_str()));
	std::string eventId = sendMessage(roomId, request);
	json_decref(request);
	return eventId;
}

std::string Client::sendText(std::string roomId, std::string text) {
	json_t* request = json_object();
	json_object_set_new(request, "msgtype", json_string("m.text"));
	json_object_set_new(request, "body", json_string(text.c_str()));
	std::string eventId = sendMessage(roomId, request);
	json_decref(request);
	return eventId;
}

std::string Client::sendMessage(std::string roomId, json_t* content) {
	return sendEvent(roomId, "m.room.message", content);
}

std::string Client::sendEvent(std::string roomId, std::string eventType, json_t* content) {
	roomId = resolveRoom(roomId);
	std::string txid = std::to_string(time(NULL)) + "_REQ_" + std::to_string(requestId);
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/send/" + urlencode(eventType) + "/" + urlencode(txid);
	json_t* ret = doRequest("PUT", path, content);
	if (!ret) {
		return "";
	}
	json_t* eventId = json_object_get(ret, "event_id");
	if (!eventId) {
		json_decref(ret);
		return "";
	}
	const char* eventIdStr = json_string_value(eventId);
	json_decref(ret);
	return eventIdStr;
}

std::string Client::sendStateEvent(std::string roomId, std::string type, std::string stateKey, json_t* content) {
	roomId = resolveRoom(roomId);
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/state/" + urlencode(type) + "/" + urlencode(stateKey);
	json_t* ret = doRequest("PUT", path, content);
	if (!ret) {
		return "";
	}
	json_t* eventId = json_object_get(ret, "event_id");
	if (!eventId) {
		json_decref(ret);
		return "";
	}
	const char* eventIdStr = json_string_value(eventId);
	json_decref(ret);
	return eventIdStr;
}

std::string Client::redactEvent(std::string roomId, std::string eventId, std::string reason) {
	roomId = resolveRoom(roomId);
	std::string txid = std::to_string(time(NULL)) + "_REQ_" + std::to_string(requestId);
	json_t* content = json_object();
	if (reason != "") {
		json_object_set_new(content, "reason", json_string(reason.c_str()));
	}
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/redact/" + urlencode(eventId) + "/" + txid;
	json_t* ret = doRequest("PUT", path, content);
	json_decref(content);
	if (!ret) {
		return "";
	}
	json_t* retEventId = json_object_get(ret, "event_id");
	if (!retEventId) {
		json_decref(ret);
		return "";
	}
	const char* eventIdStr = json_string_value(retEventId);
	json_decref(ret);
	return eventIdStr;
}

void startSyncLoopWithoutClass(void* arg) {
	((Client*)arg)->startSync();
}

void Client::startSyncLoop() {
	stopSyncLoop(); // first we stop an already running sync loop
	isSyncing = true;
	stopSyncing = false;
	s32 prio = 0;
	D printf("%lld\n", (u64)this);
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	syncThread = threadCreate(startSyncLoopWithoutClass, this, 8*1024, prio-1, -2, true);
}

void Client::stopSyncLoop() {
	stopSyncing = true;
	if (isSyncing) {
		threadJoin(syncThread, U64_MAX);
		threadFree(syncThread);
	}
	isSyncing = false;
}

void Client::setSyncEventCallback(void (*cb)(std::string roomId, json_t* event)) {
	sync_event_callback = cb;
}

void Client::processSync(json_t* sync) {
	json_t* rooms = json_object_get(sync, "rooms");
	if (!rooms) {
		return; // nothing to do
	}
	json_t* leftRooms = json_object_get(rooms, "leave");
	json_t* invitedRooms = json_object_get(rooms, "invite");
	json_t* joinedRooms = json_object_get(rooms, "join");
	
	const char* roomId;
	json_t* room;
	size_t index;
	json_t* event;
	
	if (leftRooms) {
		json_object_foreach(leftRooms, roomId, room) {
			// rooms that we left
			// emit leave event with roomId
		}
	}
	
	if (invitedRooms) {
		json_object_foreach(invitedRooms, roomId, room) {
			// rooms that we were invited to
		}
	}
	
	if (joinedRooms) {
		json_object_foreach(joinedRooms, roomId, room) {
			// rooms that we are joined
			D printf("%s:\n", roomId);
			json_t* timeline = json_object_get(room, "timeline");
			if (!timeline) {
				D printf("no timeline\n");
				continue;
			}
			json_t* events = json_object_get(timeline, "events");
			if (!events) {
				D printf("no events\n");
				continue;
			}
			json_array_foreach(events, index, event) {
				json_t* eventType = json_object_get(event, "type");
				D printf("%s\n", json_string_value(eventType));
				if (sync_event_callback) {
					sync_event_callback(roomId, event);
				}
			}
		}
	}
}

void Client::startSync() {
	while (true) {
		std::string token = store->getSyncToken();
		if (stopSyncing) {
			return;
		}
		json_t* ret = doSync(token);
		if (ret) {
			// set the token for the next batch
			json_t* token = json_object_get(ret, "next_batch");
			if (token) {
				D printf("Found next batch\n");
				store->setSyncToken(json_string_value(token));
			} else {
				D printf("No next batch\n");
				store->setSyncToken("");
			}
			processSync(ret);
			json_decref(ret);
		}
		svcSleepThread((u64)1000000ULL * (u64)200);
	}
}

json_t* Client::doSync(std::string token) {
	D printf("Doing sync with token %s\n", token.c_str());
	
	std::string query = "?full_state=false&timeout=" + std::to_string(SYNC_TIMEOUT);
	if (token != "") {
		query += "&since=" + token;
	}
	return doRequest("GET", "/_matrix/client/r0/sync" + query);
}

size_t DoRequestWriteCallback(char *contents, size_t size, size_t nmemb, void *userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

json_t* Client::doRequest(const char* method, std::string path, json_t* body) {
	std::string url = hsUrl + path;
	requestId++;
	
	D printf("Opening Request %d\n%s\n", requestId, url.c_str());

	if (!SOC_buffer) {
		SOC_buffer = (u32*)memalign(0x1000, 0x100000);
		if (!SOC_buffer) {
			return NULL;
		}
		if (socInit(SOC_buffer, 0x100000) != 0) {
			return NULL;
		}
	}

	CURL* curl = curl_easy_init();
	CURLcode res;
	if (!curl) {
		D printf("curl init failed\n");
		return NULL;
	}
	std::string readBuffer;
	struct curl_slist* headers = NULL;
	if (token != "") {
		headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
	}
	if (body) {
		headers = curl_slist_append(headers, "Content-Type: application/json");
		const char* bodyStr = json_dumps(body, JSON_ENSURE_ASCII | JSON_ESCAPE_SLASH);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr);
	}
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "3ds");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DoRequestWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	
//	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
//	curl_easy_setopt(curl, CURLOPT_STDERR, stdout);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		D printf("curl res not ok %d\n", res);
		return NULL;
	}

//	D printf("%s\n", readBuffer.c_str());
	json_error_t error;
	json_t* content = json_loads(readBuffer.c_str(), 0, &error);
	if (!content) {
		D printf("Failed to parse json\n");
		return NULL;
	}
	return content;
}

}; // namespace Matrix
