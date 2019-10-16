#include "../include/matrixclient.h"
#include <inttypes.h>
#include <stdio.h>
#include <3ds.h>
#include <jansson.h>
#include <malloc.h>
#include <curl/curl.h>
#include <string.h>

#include <sys/socket.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000

static u32 *SOC_buffer = NULL;

MatrixClient::MatrixClient(std::string homeserverUrl, std::string matrixToken) {
	hsUrl = homeserverUrl;
	token = matrixToken;
}

size_t DoRequestWriteCallback(char *contents, size_t size, size_t nmemb, void *userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

Result MatrixClient::doRequest(json_t* content, const char* method, std::string path, json_t* body) {
	std::string url = hsUrl + path;
	
	printf("Opening Request\n");
	printf(url.c_str());
	printf("\n");

	if (!SOC_buffer) {
		SOC_buffer = (u32*)memalign(0x1000, 0x100000);
		if (!SOC_buffer) {
			return -1;
		}
		if (socInit(SOC_buffer, 0x100000) != 0) {
			return -1;
		}
	}

	CURL* curl = curl_easy_init();
	CURLcode res;
	if (!curl) {
		printf("curl init failed\n");
		return -1;
	}
	std::string readBuffer;
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "3ds");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DoRequestWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	
//	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
//	curl_easy_setopt(curl, CURLOPT_STDERR, stdout);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		printf("curl res not ok %" PRId32 "\n", res);
		return res;
	}
	printf("Result successful!\n");
	curl_easy_cleanup(curl);

	printf(readBuffer.c_str());
	json_error_t error;
	content = json_loads(readBuffer.c_str(), 0, &error);
	if (!content) {
		return -3;
	}
	return 0;
}
