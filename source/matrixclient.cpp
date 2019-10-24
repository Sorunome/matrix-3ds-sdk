#include <matrixclient.h>
#include <inttypes.h>
#include <stdio.h>
#include <3ds.h>
#include <jansson.h>
#include <malloc.h>
#include <curl/curl.h>
#include <string.h>
#include "util.h"
#include "memorystore.h"
#include <string>
#include <vector>

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

#if DEBUG
PrintConsole* topScreenDebugConsole = NULL;
#endif

#if DEBUG
#define printf_top(f_, ...) do {consoleSelect(topScreenDebugConsole);printf((f_), ##__VA_ARGS__);} while(0)
#else
#define printf_top(f_, ...) do {} while(0)
#endif

namespace Matrix {

#define POST_BUFFERSIZE 0x100000

static u32 *SOC_buffer = NULL;
bool HTTPC_inited = false;

Client::Client(std::string homeserverUrl, std::string matrixToken, Store* clientStore) {
	hsUrl = homeserverUrl;
	token = matrixToken;
	if (!clientStore) {
		clientStore = new MemoryStore;
	}
	store = clientStore;
#if DEBUG
	if (!topScreenDebugConsole) {
		topScreenDebugConsole = new PrintConsole;
		consoleInit(GFX_TOP, topScreenDebugConsole);
	}
#endif
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

	const char* tokenCStr = json_object_get_string_value(ret, "access_token");
	if (!tokenCStr) {
		if (ret) json_decref(ret);
		return false;
	}
	token = tokenCStr;
	json_decref(ret);
	return true;
}

void Client::logout() {
	json_t* ret = doRequest("POST", "/_matrix/client/r0/logout");
	if (ret) {
		json_decref(ret);
	}
}

std::string Client::getUserId() {
	if (userIdCache != "") {
		return userIdCache;
	}
	json_t* ret = doRequest("GET", "/_matrix/client/r0/account/whoami");
	const char* userIdCStr = json_object_get_string_value(ret, "user_id");
	if (!userIdCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string userIdStr = userIdCStr;
	json_decref(ret);
	userIdCache = std::string(userIdStr);
	return userIdCache;
}

std::string Client::resolveRoom(std::string alias) {
	if (alias[0] == '!') {
		return alias; // this is already a room ID, nothing to do
	}
	json_t* ret = doRequest("GET", "/_matrix/client/r0/directory/room/" + urlencode(alias));
	const char* roomIdCStr = json_object_get_string_value(ret, "room_id");
	if (!roomIdCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string roomIdStr = roomIdCStr;
	printf_top("Room ID: %s\n", roomIdStr.c_str());
	json_decref(ret);
	return roomIdStr;
}

std::vector<std::string> Client::getJoinedRooms() {
	std::vector<std::string> rooms;
	json_t* ret = doRequest("GET", "/_matrix/client/r0/joined_rooms");
	json_t* roomsArr = json_object_get(ret, "joined_rooms");
	if (!roomsArr) {
		json_decref(ret);
		return rooms;
	}
	size_t index;
	json_t* value;
	json_array_foreach(roomsArr, index, value) {
		const char* val = json_string_value(value);
		if (val) {
			rooms.push_back(val);
		}
	}
	json_decref(ret);
	return rooms;
}

RoomInfo Client::getRoomInfo(std::string roomId) {
	// if we resolve the roomId here it only resolves once
	roomId = resolveRoom(roomId);
	RoomInfo info = {
		name: getRoomName(roomId),
		topic: getRoomTopic(roomId),
		avatarUrl: getRoomAvatar(roomId),
	};
	return info;
}

ExtraRoomInfo Client::getExtraRoomInfo(std::string roomId) {
	roomId = resolveRoom(roomId);
	ExtraRoomInfo info;
	info.canonicalAlias = getCanonicalAlias(roomId);
	
	// next fetch the members
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/joined_members";
	json_t* ret = doRequest("GET", path);
	if (!ret) {
		return info;
	}
	json_t* joined = json_object_get(ret, "joined");
	if (!joined || json_typeof(joined) != JSON_OBJECT) {
		json_decref(ret);
		return info;
	}
	
	const char* mxid;
	json_t* member;
	json_object_foreach(joined, mxid, member) {
		const char* displayname = json_object_get_string_value(member, "display_name");
		const char* avatarUrl = json_object_get_string_value(member, "avatar_url");
		MemberInfo memInfo;
		if (displayname) {
			memInfo.displayname = displayname;
		}
		if (avatarUrl) {
			memInfo.avatarUrl = avatarUrl;
		}
		info.members[mxid] = memInfo;
	}
	json_decref(ret);
	return info;
}

MemberInfo Client::getMemberInfo(std::string userId, std::string roomId) {
	std::string displayname = "";
	std::string avatarUrl = "";
	if (roomId != "") {
		// first try fetching fro the room
		json_t* ret = getStateEvent(roomId, "m.room.member", userId);
		if (ret) {
			char* valCStr;
			valCStr = json_object_get_string_value(ret, "displayname");
			if (valCStr) {
				displayname = valCStr;
			}
			valCStr = json_object_get_string_value(ret, "avatar_url");
			if (valCStr) {
				avatarUrl = valCStr;
			}
			json_decref(ret);
		}
	}
	if (displayname == "") {
		// then attempt the account
		std::string path = "/_matrix/client/r0/profile/" + urlencode(userId);
		json_t* ret = doRequest("GET", path);
		if (ret) {
			char* valCStr;
			valCStr = json_object_get_string_value(ret, "displayname");
			if (valCStr) {
				displayname = valCStr;
			}
			valCStr = json_object_get_string_value(ret, "avatar_url");
			if (valCStr) {
				avatarUrl = valCStr;
			}
			json_decref(ret);
		}
	}
	MemberInfo info = {
		displayname: displayname,
		avatarUrl: avatarUrl,
	};
	return info;
}

std::string Client::getRoomName(std::string roomId) {
	json_t* ret = getStateEvent(roomId, "m.room.name", "");
	const char* nameCStr = json_object_get_string_value(ret, "name");
	if (!nameCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string nameStr = nameCStr;
	json_decref(ret);
	return nameStr;
}

std::string Client::getRoomTopic(std::string roomId) {
	json_t* ret = getStateEvent(roomId, "m.room.topic", "");
	const char* topicCStr = json_object_get_string_value(ret, "topic");
	if (!topicCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string topicStr = topicCStr;
	json_decref(ret);
	return topicStr;
}

std::string Client::getRoomAvatar(std::string roomId) {
	json_t* ret = getStateEvent(roomId, "m.room.avatar", "");
	const char* urlCStr = json_object_get_string_value(ret, "url");
	if (!urlCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string urlStr = urlCStr;
	json_decref(ret);
	return urlStr;
}

std::string Client::getCanonicalAlias(std::string roomId) {
	json_t* ret = getStateEvent(roomId, "m.room.canonical_alias", "");
	const char* aliasCStr = json_object_get_string_value(ret, "alias");
	if (!aliasCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string aliasStr = aliasCStr;
	json_decref(ret);
	return aliasStr;
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
	const char* eventIdCStr = json_object_get_string_value(ret, "event_id");
	if (!eventIdCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string eventIdStr = eventIdCStr;
	json_decref(ret);
	return eventIdStr;
}

json_t* Client::getStateEvent(std::string roomId, std::string type, std::string stateKey) {
	roomId = resolveRoom(roomId);
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/state/" + urlencode(type) + "/" + urlencode(stateKey);
	return doRequest("GET", path);
}

std::string Client::sendStateEvent(std::string roomId, std::string type, std::string stateKey, json_t* content) {
	roomId = resolveRoom(roomId);
	std::string path = "/_matrix/client/r0/rooms/" + urlencode(roomId) + "/state/" + urlencode(type) + "/" + urlencode(stateKey);
	json_t* ret = doRequest("PUT", path, content);
	const char* eventIdCStr = json_object_get_string_value(ret, "event_id");
	if (!eventIdCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string eventIdStr = eventIdCStr;
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
	const char* eventIdCStr = json_object_get_string_value(ret, "event_id");
	if (!eventIdCStr) {
		if (ret) json_decref(ret);
		return "";
	}
	std::string eventIdStr = eventIdCStr;
	json_decref(ret);
	return eventIdStr;
}

void startSyncLoopWithoutClass(void* arg) {
	((Client*)arg)->syncLoop();
}

void Client::startSyncLoop() {
	stopSyncLoop(); // first we stop an already running sync loop
	isSyncing = true;
	stopSyncing = false;
	s32 prio = 0;
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

void Client::setEventCallback(eventCallback cb) {
	callbacks.event = cb;
}

void Client::setLeaveRoomCallback(eventCallback cb) {
	callbacks.leaveRoom = cb;
}

void Client::setInviteRoomCallback(eventCallback cb) {
	callbacks.inviteRoom = cb;
}

void Client::setRoomInfoCallback(roomInfoCallback cb) {
	callbacks.roomInfo = cb;
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
	
	if (leftRooms && callbacks.leaveRoom) {
		json_object_foreach(leftRooms, roomId, room) {
			// rooms that we left
			json_t* timeline = json_object_get(room, "timeline");
			if (!timeline) {
				continue;
			}
			json_t* events = json_object_get(timeline, "events");
			if (!events) {
				continue;
			}
			json_t* leaveEvent = NULL;
			json_array_foreach(events, index, event) {
				// check if the event type is m.room.member
				char* val;
				val = json_object_get_string_value(event, "type");
				if (!val) {
					continue;
				}
				if (strcmp(val, "m.room.member") != 0) {
					continue;
				}
				// check if it is actually us
				val = json_object_get_string_value(event, "state_key");
				if (!val) {
					continue;
				}
				if (strcmp(val, getUserId().c_str()) != 0) {
					continue;
				}
				// we do *not* check for event age as we don't have unsigned stuffs in our timeline due to our filter
				// so we just assume that the events arrive in the correct order (probably true)
				leaveEvent = event;
			}
			if (!leaveEvent) {
				printf_top("Left room %s without an event\n", roomId);
				continue;
			}
			callbacks.leaveRoom(roomId, leaveEvent);
		}
	}
	
	if (invitedRooms && callbacks.inviteRoom) {
		json_object_foreach(invitedRooms, roomId, room) {
			// rooms that we were invited to
			json_t* invite_state = json_object_get(room, "invite_state");
			if (!invite_state) {
				continue;
			}
			json_t* events = json_object_get(invite_state, "events");
			if (!events) {
				continue;
			}
			json_t* inviteEvent = NULL;
			json_array_foreach(events, index, event) {
				// check if the event type is m.room.member
				char* val;
				val = json_object_get_string_value(event, "type");
				if (!val) {
					continue;
				}
				if (strcmp(val, "m.room.member") != 0) {
					continue;
				}
				// check if it is actually us
				val = json_object_get_string_value(event, "state_key");
				if (!val) {
					continue;
				}
				if (strcmp(val, getUserId().c_str()) != 0) {
					continue;
				}
				// check for if it was an invite event
				json_t* content = json_object_get(event, "content");
				if (!content) {
					continue;
				}
				val = json_object_get_string_value(content, "membership");
				if (!val) {
					continue;
				}
				if (strcmp(val, "invite") != 0) {
					continue;
				}
				// we do *not* check for event age as we don't have unsigned stuffs in our timeline due to our filter
				// so we just assume that the events arrive in the correct order (probably true)
				inviteEvent = event;
			}
			if (!inviteEvent) {
				printf_top("Invite to room %s without an event\n", roomId);
				continue;
			}
			callbacks.inviteRoom(roomId, inviteEvent);
		}
	}
	
	if (joinedRooms) {
		json_object_foreach(joinedRooms, roomId, room) {
			// rooms that we are joined
			json_t* state = json_object_get(room, "state");
			if (callbacks.roomInfo && state) {
				json_t* events = json_object_get(state, "events");
				if (events) {
					RoomInfo info;
					bool addedInfo = false;
					json_array_foreach(events, index, event) {
						const char* typeCStr = json_object_get_string_value(event, "type");
						if (!typeCStr) {
							continue;
						}
						json_t* content = json_object_get(event, "content");
						if (!content) {
							continue;
						}
						if (strcmp(typeCStr, "m.room.name") == 0) {
							const char* nameCStr = json_object_get_string_value(content, "name");
							if (nameCStr) {
								info.name = nameCStr;
								addedInfo = true;
							}
						} else if (strcmp(typeCStr, "m.room.topic") == 0) {
							const char* topicCStr = json_object_get_string_value(content, "topic");
							if (topicCStr) {
								info.topic = topicCStr;
								addedInfo = true;
							}
						} else if (strcmp(typeCStr, "m.room.avatar") == 0) {
							const char* urlCStr = json_object_get_string_value(content, "url");
							if (urlCStr) {
								info.avatarUrl = urlCStr;
								addedInfo = true;
							}
						}
					}
					if (addedInfo) {
						callbacks.roomInfo(roomId, info);
					}
				}
			}
			json_t* timeline = json_object_get(room, "timeline");
			if (callbacks.event && timeline) {
				json_t* events = json_object_get(timeline, "events");
				if (events) {
					json_array_foreach(events, index, event) {
						callbacks.event(roomId, event);
					}
				}
			}
		}
	}
}

void Client::registerFilter() {
	static const char *json =
		"{"
		"	\"account_data\": {"
		"		\"types\": ["
		"			\"m.direct\""
		"		]"
		"	},"
		"	\"presence\": {"
		"		\"limit\": 0,"
		"		\"types\": [\"none\"]"
		"	},"
		"	\"room\": {"
		"		\"account_data\": {"
		"			\"limit\": 0,"
		"			\"types\": [\"none\"]"
		"		},"
		"		\"ephemeral\": {"
		"			\"limit\": 0,"
		"			\"types\": []"
		"		},"
		"		\"state\": {"
		"			\"limit\": 3,"
		"			\"types\": ["
		"				\"m.room.name\","
		"				\"m.room.topic\","
		"				\"m.room.avatar\""
		"			]"
		"		},"
		"		\"timeline\": {"
		"			\"limit\": 10,"
		"			\"lazy_load_members\": true"
		"		}"
		"	},"
		"	\"event_format\": \"client\","
		"	\"event_fields\": ["
		"		\"type\","
		"		\"content\","
		"		\"sender\","
		"		\"state_key\","
		"		\"event_id\","
		"		\"origin_server_ts\""
		"	]"
		"}";
	
	json_error_t error;
	json_t* filter = json_loads(json, 0, &error);
	if (!filter) {
		printf_top("PANIC!!!!! INVALID FILTER JSON!!!!\n");
		printf_top("%s\n", error.text);
		printf_top("At %d:%d (%d)\n", error.line, error.column, error.position);
		return;
	}
	std::string userId = getUserId();
	json_t* ret = doRequest("POST", "/_matrix/client/r0/user/" + urlencode(userId) + "/filter", filter);
	json_decref(filter);
	const char* filterIdCStr = json_object_get_string_value(ret, "filter_id");
	if (!filterIdCStr) {
		if (ret) json_decref(ret);
		return;
	}
	std::string filterIdStr = filterIdCStr;
	json_decref(ret);
	store->setFilterId(filterIdStr);
}

void Client::syncLoop() {
	u32 timeout = 60;
	while (true) {
		if (stopSyncing) {
			return;
		}
		std::string token = store->getSyncToken();
		std::string filterId = store->getFilterId();
		if (filterId == "") {
			registerFilter();
			filterId = store->getFilterId();
		}
		json_t* ret = doSync(token, filterId, timeout);
		if (ret) {
			timeout = 60;
			// set the token for the next batch
			const char* tokenCStr = json_object_get_string_value(ret, "next_batch");
			if (tokenCStr) {
				store->setSyncToken(tokenCStr);
			} else {
				store->setSyncToken("");
			}
			processSync(ret);
			json_decref(ret);
		} else {
			if (lastRequestError == RequestError::timeout) {
				timeout += 10*60;
				printf_top("Timeout reached, increasing it to %lu\n", timeout);
			}
		}
		svcSleepThread((u64)1000000ULL * (u64)200);
	}
}

json_t* Client::doSync(std::string token, std::string filter, u32 timeout) {
//	printf_top("Doing sync with token %s\n", token.c_str());
	
	std::string query = "?full_state=false&timeout=" + std::to_string(SYNC_TIMEOUT) + "&filter=" + urlencode(filter);
	if (token != "") {
		query += "&since=" + token;
	}
	return doRequest("GET", "/_matrix/client/r0/sync" + query, NULL, timeout);
}

size_t DoRequestWriteCallback(char *contents, size_t size, size_t nmemb, void *userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

bool doingCurlRequest = false;
bool doingHttpcRequest = false;

json_t* Client::doRequest(const char* method, std::string path, json_t* body, u32 timeout) {
	std::string url = hsUrl + path;
	requestId++;
	
	if (!doingCurlRequest) {
		doingCurlRequest = true;
		json_t* ret = doRequestCurl(method, url, body, timeout);
		doingCurlRequest = false;
		return ret;
	} else if (!doingHttpcRequest) {
		doingHttpcRequest = true;
		json_t* ret = doRequestHttpc(method, url, body, timeout);
		doingHttpcRequest = false;
		return ret;
	} else {
		return doRequestCurl(method, url, body, timeout);
	}
}

json_t* Client::doRequestCurl(const char* method, std::string url, json_t* body, u32 timeout) {
	printf_top("Opening Request %d with CURL\n%s\n", requestId, url.c_str());

	if (!SOC_buffer) {
		SOC_buffer = (u32*)memalign(0x1000, POST_BUFFERSIZE);
		if (!SOC_buffer) {
			return NULL;
		}
		if (socInit(SOC_buffer, POST_BUFFERSIZE) != 0) {
			return NULL;
		}
	}

	CURL* curl = curl_easy_init();
	CURLcode res;
	if (!curl) {
		printf_top("curl init failed\n");
		return NULL;
	}
	std::string readBuffer;
	struct curl_slist* headers = NULL;
	if (token != "") {
		headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
	}
	char* bodyStr = NULL;
	if (body) {
		headers = curl_slist_append(headers, "Content-Type: application/json");
		bodyStr = json_dumps(body, JSON_ENSURE_ASCII | JSON_ESCAPE_SLASH);
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
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	
//	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
//	curl_easy_setopt(curl, CURLOPT_STDERR, stdout);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (bodyStr) free(bodyStr);
	if (res != CURLE_OK) {
		printf_top("curl res not ok %d\n", res);
		if (res == CURLE_OPERATION_TIMEDOUT) {
			lastRequestError = RequestError::timeout;
		}
		return NULL;
	}

//	printf_top("%s\n", readBuffer.c_str());
	printf_top("Body size: %d\n", readBuffer.length());
	json_error_t error;
	json_t* content = json_loads(readBuffer.c_str(), 0, &error);
	if (!content) {
		printf_top("Failed to parse json\n");
		return NULL;
	}
	return content;
}

json_t* Client::doRequestHttpc(const char* method, std::string url, json_t* body, u32 timeout) {
	printf_top("Opening Request %d with HTTPC\n%s\n", requestId, url.c_str());

	if (!HTTPC_inited) {
		if (httpcInit(POST_BUFFERSIZE) != 0) {
			return NULL;
		}
		HTTPC_inited = true;
	}

	Result ret = 0;
	httpcContext context;
	HTTPC_RequestMethod methodReal = HTTPC_METHOD_GET;
	u32 statusCode = 0;
	u32 contentSize = 0, readsize = 0, size = 0;
	u8* buf, *lastbuf;
	if (strcmp(method, "GET") == 0) {
		methodReal = HTTPC_METHOD_GET;
	} else if (strcmp(method, "PUT") == 0) {
		methodReal = HTTPC_METHOD_PUT;
	} else if (strcmp(method, "POST") == 0) {
		methodReal = HTTPC_METHOD_POST;
	} else if (strcmp(method, "DELETE") == 0) {
		methodReal = HTTPC_METHOD_DELETE;
	}
	do {
		httpcOpenContext(&context, methodReal, url.c_str(), 1);
		httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
		httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
		httpcAddRequestHeaderField(&context, "User-Agent", "3ds");
		if (token != "") {
			httpcAddRequestHeaderField(&context, "Authorization", ("Bearer " + token).c_str());
		}
		httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
		char* bodyStr = NULL;
		if (body) {
			httpcAddRequestHeaderField(&context, "Content-Type", "application/json");
			bodyStr = json_dumps(body, JSON_ENSURE_ASCII | JSON_ESCAPE_SLASH);
			httpcAddPostDataRaw(&context, (u32*)bodyStr, strlen(bodyStr));
		}
		ret = httpcBeginRequest(&context);
		if (bodyStr) free(bodyStr);
		if (ret) {
			printf_top("Failed to perform request %ld\n", ret);
			httpcCloseContext(&context);
			return NULL;
		}
		httpcGetResponseStatusCode(&context, &statusCode);
		if ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308)) {
			char newUrl[0x100];
			ret = httpcGetResponseHeader(&context, "Location", newUrl, 0x100);
			url = std::string(newUrl);
		}
	} while ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308));
	ret = httpcGetDownloadSizeState(&context, NULL, &contentSize);
	if (ret != 0) {
		httpcCloseContext(&context);
		return NULL;
	}

	// Start with a single page buffer
	buf = (u8*)malloc(0x1000);
	if (!buf) {
		httpcCloseContext(&context); 
		return NULL;
	}

	u64 timeoutReal = timeout * 1000000000ULL;
	do {
		// This download loop resizes the buffer as data is read.
		u64 timeStartMs = osGetTime();
		ret = httpcDownloadDataTimeout(&context, buf+size, 0x1000, &readsize, timeoutReal);
		u64 timeDifMs = osGetTime() - timeStartMs;
		timeoutReal -= timeDifMs*1000000ULL;
		size += readsize;
		if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
			lastbuf = buf; // Save the old pointer, in case realloc() fails.
			buf = (u8*)realloc(buf, size + 0x1000);
			if (!buf) { 
				httpcCloseContext(&context);
				free(lastbuf);
				return NULL;
			}
		}
	} while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

	if (ret) {
		printf_top("httpc res not ok %lu\n", ret);
		// let's just assume it was a timeout...
		// TODO: better detection
		lastRequestError = RequestError::timeout;
		httpcCloseContext(&context);
		free(buf);
		return NULL;
	}

	// Resize the buffer back down to our actual final size
	lastbuf = buf;
	buf = (u8*)realloc(buf, size + 1); // +1 for zero-termination
	if (!buf) { // realloc() failed.
		httpcCloseContext(&context);
		free(lastbuf);
		return NULL;
	}
	buf[size] = '\0'; // zero-terminate

	httpcCloseContext(&context);

//	printf_top("%s\n", buf);
	printf_top("Body size: %lu\n", size);

	json_error_t error;
	json_t* content = json_loads((char*)buf, 0, &error);
	free(buf);
	if (!content) {
		printf_top("Failed to parse json\n");
		return NULL;
	}
	return content;
}

}; // namespace Matrix
