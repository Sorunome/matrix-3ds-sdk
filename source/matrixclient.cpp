#include "../include/matrixclient.h"
#include <inttypes.h>
#include <stdio.h>
#include <3ds.h>
#include <jansson.h>

MatrixClient::MatrixClient(std::string homeserverUrl, std::string matrixToken) {
	hsUrl = homeserverUrl;
	token = matrixToken;
}

Result MatrixClient::doRequest(json_t* content, HTTPC_RequestMethod method, std::string path, json_t* body) {
	std::string url = hsUrl + path;
	Result ret = 0;
	httpcContext context;
	std::string newUrl = "";
	u32 statusCode = 0;
	u32 contentSize = 0, readsize = 0, size = 0;
	u8* buf, *lastbuf;
	do {
		printf("Opening Request\n");
		printf(url.c_str());
		printf("\n");
		ret = httpcOpenContext(&context, method, url.c_str(), 1);
		printf("return from httpcOpenContext: %" PRId32 "\n",ret);

		ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
		printf("return from httpcSetSSLOpt: %" PRId32 "\n",ret);

		ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
		printf("return from httpcSetKeepAlive: %" PRId32 "\n",ret);

//		ret = httpcAddRequestHeaderField(&context, "User-Agent", "httpc-example/1.0.0");
//		printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);
		ret = httpcAddRequestHeaderField(&context, "Authorization", ("Bearer " + token).c_str());
		printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);
		ret = httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
		printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);
		if (body) {
			// we have a body to send!
			ret = httpcAddRequestHeaderField(&context, "Content-Type", "application/json");
			printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);
		}

		printf("Do Request\n");
		ret = httpcBeginRequest(&context);
		printf("return from httpcBeginRequest: %" PRId32 "\n",ret);
		if (ret != 0) {
			printf("PANIC!!!\n");
			httpcCloseContext(&context);
			return ret;
		}

		ret = httpcGetResponseStatusCode(&context, &statusCode);
		printf("status code: %" PRId32 "\n", statusCode);
		if(ret!=0){
			httpcCloseContext(&context);
			return ret;
		}

		if ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308)) {
			char newUrl[0x100];
			ret = httpcGetResponseHeader(&context, "Location", newUrl, 0x100);
			url = std::string(newUrl);
		}
	} while ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308));

	if (statusCode < 200 || statusCode > 299) {
		printf("Non-200 status code: %" PRId32 "\n", statusCode);
		httpcCloseContext(&context);
		return -2;
	}

	ret = httpcGetDownloadSizeState(&context, NULL, &contentSize);
	if (ret != 0) {
		httpcCloseContext(&context);
		return ret;
	}

	printf("reported size: %" PRId32 "\n", contentSize);
	// Start with a single page buffer
	buf = (u8*)malloc(0x1000);
	if (buf == NULL) {
		httpcCloseContext(&context); 
		return -1;
	}

	do {
		// This download loop resizes the buffer as data is read.
		ret = httpcDownloadData(&context, buf+size, 0x1000, &readsize);
		size += readsize; 
		if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
			lastbuf = buf; // Save the old pointer, in case realloc() fails.
			buf = (u8*)realloc(buf, size + 0x1000);
			if (buf == NULL) { 
				httpcCloseContext(&context);
				free(lastbuf);
				return -1;
			}
		}
	} while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);
	printf("return from httpcDownloadData: %" PRId32 "\n",ret);

	if (ret != 0) {
		httpcCloseContext(&context);
		free(buf);
		return -1;
	}

	// Resize the buffer back down to our actual final size
	lastbuf = buf;
	buf = (u8*)realloc(buf, size);
	if (buf == NULL) { // realloc() failed.
		printf("WAT?!\n");
		httpcCloseContext(&context);
		free(lastbuf);
		return -1;
	}

	printf("downloaded size: %" PRId32 "\n",size);
	printf((const char*)buf);
	httpcCloseContext(&context);

	json_error_t error;
	content = json_loads((const char*)buf, 0, &error);
	if (!content) {
		free(buf);
		return -3;
	}
	free(buf);
	return 0;
}
