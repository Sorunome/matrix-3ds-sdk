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

namespace Matrix {

static u32 *SOC_buffer = NULL;

Client::Client(std::string homeserverUrl, std::string matrixToken, Store* clientStore) {
	hsUrl = homeserverUrl;
	token = matrixToken;
	requestId = 0;
	stopSyncing = false;
	if (!clientStore) {
		clientStore = new MemoryStore();
	}
	store = clientStore;
}

std::string Client::userId() {
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
	return userIdStr;
}

std::string Client::sendTextMessage(std::string roomId, std::string text) {
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
	std::string txid = std::to_string(time(NULL)) + "_REQ_" + std::to_string(requestId);
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/send/" + urlencode(eventType) + "/" + urlencode(txid);
	json_t* ret = doRequest("PUT", path, content);
	if (!ret) {
		return "";
	}
	json_t* eventId = json_object_get(ret, "event_id");
	free(ret);
	if (!eventId) {
		json_decref(ret);
		return "";
	}
	const char* eventIdStr = json_string_value(eventId);
	json_decref(ret);
	return eventIdStr;
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
	
	if (leftRooms) {
		json_object_foreach(leftRooms, roomId, room) {
			// rooms that we left
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
			printf(roomId);
			printf(":\n");
			json_t* timeline = json_object_get(room, "timeline");
			if (!timeline) {
				printf("no timeline\n");
				continue;
			}
			json_t* events = json_object_get(timeline, "events");
			if (!events) {
				printf("no events\n");
				continue;
			}
			size_t index;
			json_t* event;
			json_array_foreach(events, index, event) {
				json_t* eventType = json_object_get(event, "type");
				printf(json_string_value(eventType));
				printf("\n");
			}
		}
	}
}

void Client::startSync() {
	std::string token = store->getSyncToken();
	if (stopSyncing) {
		return;
	}
	json_t* ret = doSync(token);
	if (ret) {
		// set the token for the next batch
		json_t* token = json_object_get(ret, "next_batch");
		if (token) {
			printf("Found next batch\n");
			store->setSyncToken(json_string_value(token));
		} else {
			printf("No next batch\n");
			store->setSyncToken("");
		}
		processSync(ret);
		json_decref(ret);
	}
}

json_t* Client::doSync(std::string token) {
	printf("Doing sync with token ");
	printf(token.c_str());
	printf("\n");
	
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
	
	printf("Opening Request\n");
	printf(url.c_str());
	printf("\n");

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
		printf("curl init failed\n");
		return NULL;
	}
	std::string readBuffer;
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
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
	if (res != CURLE_OK) {
		printf("curl res not ok %" PRId32 "\n", res);
		return NULL;
	}
	curl_easy_cleanup(curl);

//	printf(readBuffer.c_str());
//	printf("\n");
	json_error_t error;
	json_t* content = json_loads(readBuffer.c_str(), 0, &error);
	if (!content) {
		printf("Failed to parse json\n");
		return NULL;
	}
	return content;
}

}; // namespace Matrix
