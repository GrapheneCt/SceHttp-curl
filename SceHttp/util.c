#include <kernel.h>
#include <sce_atomic.h>
#include <paf/std/stdc.h>

#include "common.h"

#define SCE_HTTP_NAMED_OBJECT_MAX 8

static void **s_namedObjects = SCE_NULL;

void *_sceHttpMalloc(unsigned int size)
{
	return sce_paf_malloc(size);
}

void _sceHttpFree(void *ptr)
{
	sce_paf_free(ptr);
}

void *_sceHttpRealloc(void *ptr, unsigned int newsize)
{
	return sce_paf_realloc(ptr, newsize);
}

int _sceHttpNamedObjectInit()
{
	if (s_namedObjects == NULL) {
		s_namedObjects = (void **)_sceHttpMalloc(SCE_HTTP_NAMED_OBJECT_MAX * sizeof(void *));
		if (!s_namedObjects)
			return SCE_HTTP_ERROR_OUT_OF_MEMORY;
		sceClibMemset(s_namedObjects, 0, SCE_HTTP_NAMED_OBJECT_MAX * sizeof(void *));
	}

	return SCE_OK;
}

int _sceHttpNamedObjectTerm()
{
	if (s_namedObjects) {
		_sceHttpFree(s_namedObjects);
	}
	s_namedObjects = NULL;

	return SCE_OK;
}

void *_sceHttpNamedObjectGet(int id)
{
	return s_namedObjects[id];
}

int _sceHttpNamedObjectGetName(void *obj)
{
	for (int i = 1; i < SCE_HTTP_NAMED_OBJECT_MAX; i++) {
		if (s_namedObjects[i] == obj) {
			return i;
		}
	}

	return 0;
}

void _sceHttpNamedObjectRemove(int id)
{
	s_namedObjects[id] = 0;
}

int _sceHttpNamedObjectAdd(void *obj)
{
	int ret = -1;

	for (int i = 1; i < SCE_HTTP_NAMED_OBJECT_MAX; i++) {
		if (s_namedObjects[i] == 0) {
			s_namedObjects[i] = obj;
			ret = i;
			break;
		}
	}

	return ret;
}

size_t _sceHttpResponseHeaderWriteCb(char *buffer, size_t size, size_t nitems, void *userdata)
{
	SceHttpRequest *obj = (SceHttpRequest *)userdata;
	size_t toCopy = size * nitems;

	if (toCopy != 0) {
		if (toCopy > 3) {
			if (*(SceUInt32 *)buffer == 0x50545448) {
				sceClibMemset(obj->responseHeaderStorage, 0, obj->responseHeaderMaxSize);
				obj->responseHeaderSize = 0;
			}
		}

		if ((toCopy + obj->responseHeaderSize) > obj->responseHeaderMaxSize) {
			toCopy = obj->responseHeaderMaxSize - obj->responseHeaderSize;
		}

		sceClibMemcpy(obj->responseHeaderStorage + obj->responseHeaderSize, buffer, toCopy);
	}

	obj->responseHeaderSize += toCopy;

	return toCopy;
}

SceInt32 _sceHttpCurlErrorToSceHttp(CURLcode err)
{
	SceInt32 ret = SCE_OK;

	switch (err) {
	case CURLE_OK:
		break;
	case CURLE_COULDNT_RESOLVE_PROXY:
		ret = SCE_HTTP_ERROR_PROXY;
		break;
	case CURLE_FTP_ACCEPT_TIMEOUT:
	case CURLE_OPERATION_TIMEDOUT:
		ret = SCE_HTTP_ERROR_TIMEOUT;
		break;
	case CURLE_AGAIN:
		ret = SCE_HTTP_ERROR_EAGAIN;
		break;
	default:
		ret = SCE_HTTP_ERROR_UNKNOWN;
		break;
	}

	return ret;
}

size_t _sceHttpGetMethodWriteCb(char *buffer, size_t size, size_t nitems, void *userdata)
{
	SceHttpRequest *obj = (SceHttpRequest *)userdata;
	SceUInt32 curlCopySize = size * nitems;
	SceUInt32 fiberArg = 0;

copyTail:

	sceFiberReturnToThread(0, &fiberArg);
	if (fiberArg == SCE_HTTP_FIBER_NEED_TERM)
		return 0;

	if (obj->dst && obj->toRead > 0)
	{
		SceUInt32 copySize = curlCopySize;
		if (copySize > obj->toRead)
			copySize = obj->toRead;
		sceClibMemcpy(obj->dst, buffer, copySize);
		obj->toRead -= copySize;
		obj->dst += copySize;
		if (copySize != curlCopySize) {
			curlCopySize -= copySize;
			buffer += copySize;
			goto copyTail;
		}
	}

	return size * nitems;
}

SceVoid  _sceHttpFiberEntry(SceUInt32 argOnInitialize, SceUInt32 argOnRun)
{
	SceHttpRequest *obj = (SceHttpRequest *)argOnInitialize;
	CURLcode ret = curl_easy_perform(obj->curl);
	if (ret != CURLE_OK) {
		sceFiberReturnToThread(ret, SCE_NULL);
	}
	sceFiberReturnToThread(SCE_HTTP_FIBER_OK, SCE_NULL);
}