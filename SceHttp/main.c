#include <kernel.h>
#include <libhttp.h>
#include <stdbool.h>
#include <sce_fiber.h>
#include <psp2_compat/curl/curl.h>

#include "common.h"
#include "util.h"

char *s_ua = SCE_NULL;

#define SCE_HTTP_RESPONSE_HEADER_MAX_SIZE_DEFAULT	5000

static SceUInt32 s_recvTimeout = 10000000;
static SceUInt32 s_connectTimeout = 10000000;

int __module_start(SceSize args, const void * argp)
{
	if (_sceHttpNamedObjectInit() != SCE_OK) {
		return SCE_KERNEL_START_NO_RESIDENT;
	}

	return SCE_KERNEL_START_SUCCESS;
}

int __module_stop(SceSize args, const void * argp)
{
	_sceHttpNamedObjectTerm();

	return SCE_KERNEL_STOP_SUCCESS;
}

int sceHttpInit(SceSize poolSize)
{
	return SCE_OK;
}

int sceHttpTerm(void)
{
	return SCE_OK;
}

SceInt32 sceHttpCreateTemplate(const char*userAgent, SceInt32 httpVer, SceBool autoProxyConf)
{
	if (!s_ua) {
		SceInt32 len = sceClibStrnlen(userAgent, SCE_KERNEL_1MiB);
		s_ua = _sceHttpMalloc(len + 1);
		sceClibStrncpy(s_ua, userAgent, len + 1);
	}

	return 1;
}

SceInt32 sceHttpDeleteTemplate(SceInt32 templateId)
{
	return SCE_OK;
}

SceInt32 sceHttpCreateConnectionWithURL(
	SceInt32 tmplId,
	const char *url,
	SceBool enableKeepalive
)
{
	return 1;
}

SceInt32 sceHttpDeleteConnection(SceInt32 connId)
{
	return SCE_OK;
}

SceInt32 sceHttpCreateRequestWithURL(
	SceInt32 connId,
	SceInt32 method,
	const char *url,
	SceULong64 contentLength
)
{
	SceInt32 ret = SCE_OK;
	SceHttpRequest *obj = SCE_NULL;

	obj = _sceHttpMalloc(sizeof(SceHttpRequest));
	if (!obj)
		return SCE_HTTP_ERROR_OUT_OF_MEMORY;
	sceClibMemset(obj, 0, sizeof(SceHttpRequest));

	obj->curl = curl_easy_init();
	if (!obj->curl) {
		_sceHttpFree(obj);
		return SCE_HTTP_ERROR_OUT_OF_MEMORY;
	}

	curl_easy_setopt(obj->curl, CURLOPT_URL, url);
	curl_easy_setopt(obj->curl, CURLOPT_USERAGENT, s_ua);
	curl_easy_setopt(obj->curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(obj->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(obj->curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(obj->curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(obj->curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(obj->curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(obj->curl, CURLOPT_HEADERFUNCTION, _sceHttpResponseHeaderWriteCb);
	curl_easy_setopt(obj->curl, CURLOPT_HEADERDATA, obj);
	curl_easy_setopt(obj->curl, CURLOPT_WRITEFUNCTION, _sceHttpGetMethodWriteCb);
	curl_easy_setopt(obj->curl, CURLOPT_WRITEDATA, obj);
	curl_easy_setopt(obj->curl, CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(obj->curl, CURLOPT_CONNECTTIMEOUT_MS, s_connectTimeout / 1000);
	//curl_easy_setopt(obj->curl, CURLOPT_TIMEOUT_MS, s_recvTimeout / 1000);

	//curl_easy_setopt(obj->curl, CURLOPT_VERBOSE, 1L);

	obj->responseHeaderMaxSize = SCE_HTTP_RESPONSE_HEADER_MAX_SIZE_DEFAULT;
	obj->responseHeaderStorage = _sceHttpMalloc(SCE_HTTP_RESPONSE_HEADER_MAX_SIZE_DEFAULT);
	if (!obj->responseHeaderStorage) {
		curl_easy_cleanup(obj->curl);
		_sceHttpFree(obj);
		return SCE_HTTP_ERROR_OUT_OF_MEMORY;
		
	}

	return _sceHttpNamedObjectAdd(obj);
}

SceInt32 sceHttpDeleteRequest(SceInt32 reqId)
{
	sceHttpAbortRequest(reqId);

	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	if (obj->curl)
		curl_easy_cleanup(obj->curl);

	if (obj->responseHeaderStorage) {
		_sceHttpFree(obj->responseHeaderStorage);
	}

	_sceHttpFree(obj);
	_sceHttpNamedObjectRemove(reqId);

	return SCE_OK;
}

SceInt32 sceHttpSendRequest(
	SceInt32 reqId,
	const void *postData,
	SceSize size
)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	SceUInt32 fiberBootRet = 1;
	sceFiberInitialize(&obj->fiber, "SceHttpFiber", _sceHttpFiberEntry, (SceUInt32)obj, obj->fiberCtx, SCE_KERNEL_16KiB, SCE_NULL);
	sceFiberRun(&obj->fiber, (SceUInt32)obj, &fiberBootRet);

	if (fiberBootRet != CURLE_OK) {
		sceFiberFinalize(&obj->fiber);
		return _sceHttpCurlErrorToSceHttp(fiberBootRet);
	}

	obj->cl = -1;
	CURLcode ret = curl_easy_getinfo(obj->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &obj->cl);

	if (obj->cl < 0)
		obj->cl = 0;

	return SCE_OK;
}

SceInt32 sceHttpAbortRequest(SceInt32 reqId)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	sceFiberRun(&obj->fiber, SCE_HTTP_FIBER_NEED_TERM, SCE_NULL);
	sceFiberFinalize(&obj->fiber);

	obj->pos = 0;

	return SCE_OK;
}

SceInt32 sceHttpReadData(
	SceInt32 reqId,
	void *data,
	SceSize size
)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	SceUInt32 curlFiberRet = 0;
	SceInt32 fiberErr = 0;
	obj->dst = (SceChar8 *)data;
	obj->toRead = size;

	while (obj->toRead != 0 && curlFiberRet == 0 && fiberErr == 0) {
		fiberErr = sceFiberRun(&obj->fiber, 0, &curlFiberRet);
	}

	if (curlFiberRet != 0) {
		sceFiberFinalize(&obj->fiber);
	}

	SceUInt32 bytesRead = size - obj->toRead;
	obj->dst = SCE_NULL;
	obj->toRead = 0;

	obj->pos += bytesRead;

	return bytesRead;
}

SceInt32 sceHttpGetResponseContentLength(SceInt32 reqId, SceULong64 *contentLength)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	if (obj->cl <= 0) {
		return SCE_HTTP_ERROR_NO_CONTENT_LENGTH;
	}

	*contentLength = obj->cl;

	return SCE_OK;
}

SceInt32 sceHttpGetStatusCode(SceInt32 reqId, SceInt32 *statusCode)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	long code = 0;
	CURLcode ret = curl_easy_getinfo(obj->curl, CURLINFO_RESPONSE_CODE, &code);
	if (ret != CURLE_OK) {
		return _sceHttpCurlErrorToSceHttp(ret);
	}

	*statusCode = code;

	return SCE_OK;
}

SceInt32 sceHttpGetAllResponseHeaders(SceInt32 reqId, char **header, SceSize *headerSize)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(reqId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	*header = (char *)obj->responseHeaderStorage;
	*headerSize = obj->responseHeaderSize;

	return SCE_OK;
}

int sceHttpSetResolveTimeOut(SceInt32 id, SceUInt32 usec)
{
	return SCE_OK;
}

int sceHttpSetResolveRetry(SceInt32 id, SceInt32 retry)
{
	return SCE_OK;
}

int sceHttpSetConnectTimeOut(SceInt32 id, SceUInt32 usec)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(id);
	if (!obj) {
		s_connectTimeout = usec;
		return SCE_OK;
	}

	curl_easy_setopt(obj->curl, CURLOPT_CONNECTTIMEOUT_MS, usec / 1000);

	return SCE_OK;
}

int sceHttpSetSendTimeOut(SceInt32 id, SceUInt32 usec)
{
	return SCE_OK;
}

int sceHttpSetRecvTimeOut(SceInt32 id, SceUInt32 usec)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(id);
	if (!obj) {
		s_recvTimeout = usec;
		return SCE_OK;
	}

	//curl_easy_setopt(obj->curl, CURLOPT_TIMEOUT_MS, usec / 1000);

	return SCE_OK;
}

SceInt32 sceHttpAddRequestHeader(SceInt32 id, const char *name, const char *value, SceUInt32 mode)
{
	if (!sceClibStrncasecmp("Range", name, 5)) {
		SceHttpRequest *obj = _sceHttpNamedObjectGet(id);
		if (!obj)
			return SCE_HTTP_ERROR_INVALID_ID;

		value += 6;

		curl_easy_setopt(obj->curl, CURLOPT_RANGE, value);
	}
	else {
		//sceClibPrintf("sceHttpAddRequestHeader: %s: %s\n", name, value);
		sceKernelExitProcess(0);
	}

	return SCE_OK;
}

SceInt32 sceHttpSetAutoRedirect(SceInt32 id, SceBool enable)
{
	return SCE_OK;
}

SceInt32 sceHttpSetCookieEnabled(SceInt32 id, SceBool enable)
{
	return SCE_OK;
}

int sceHttpSetRequestContentLength(SceInt32 id, SceULong64 contentLength)
{
	return SCE_OK;
}

int sceHttpsGetSslError(SceInt32 requestId, SceInt32 *errNum, SceUInt32 *detail)
{
	*errNum = 0;
	*detail = 0;

	return SCE_OK;
}

int sceHttpsSetSslCallback(
	SceInt32 id,
	SceInt32(*httpsCallback)(unsigned int, SceSslCert *const sslCert[], int, void*),
	void *userArg
)
{
	return SCE_OK;
}

int sceHttpGetLastErrno(
	SceInt32 requestId,
	SceInt32 *errNum
)
{
	SceHttpRequest *obj = _sceHttpNamedObjectGet(requestId);
	if (!obj)
		return SCE_HTTP_ERROR_INVALID_ID;

	*errNum = obj->lastError;

	return SCE_OK;
}

int sceHttpAddCookie(
	const char *url,
	const char *cookie,
	SceSize cookieLength
	)
{
	return SCE_OK;
}

SceInt32 sceHttpCookieFlush(void)
{
	return SCE_OK;
}

SceInt32 sceHttpGetAuthEnabled(SceInt32 id, SceBool *enable)
{
	*enable = SCE_TRUE;
	return SCE_OK;
}

SceInt32 sceHttpGetAutoRedirect(
	SceInt32 id,
	SceBool *enable
)
{
	*enable = SCE_TRUE;
	return SCE_OK;
}

SceInt32 sceHttpGetCookieEnabled(
	SceInt32 id,
	SceBool *enable
)
{
	*enable = SCE_TRUE;
	return SCE_OK;
}

int sceHttpRemoveRequestHeader(
	SceInt32 id,
	const char *name
	)
{
	return SCE_OK;
}

SceInt32 sceHttpSetAuthEnabled(
	SceInt32 id,
	SceBool enable
)
{
	return SCE_OK;
}

SceInt32 sceHttpSetCookieRecvCallback(
	SceInt32 id,
	SceHttpCookieRecvCallback cbfunc,
	void *userArg
)
{
	return SCE_OK;
}

SceInt32 sceHttpSetCookieSendCallback(
	SceInt32 id,
	SceHttpCookieSendCallback cbfunc,
	void *userArg
)
{
	return SCE_OK;
}

SceInt32 sceHttpSetRedirectCallback(
	SceInt32 id,
	SceHttpRedirectCallback cbfunc,
	void *userArg
)
{
	return SCE_OK;
}

SceInt32 sceHttpParseResponseHeader(const char *header, SceSize headerLen, const char *fieldName, const char **fieldValue, SceSize *valueLen)
{
	char cVar1;
	bool bVar2;
	bool bVar3;
	unsigned char bVar4;
	unsigned int uVar5;
	int iVar6;
	char *pcVar7;
	unsigned int uVar8;
	unsigned int uVar9;
	char *pcVar10;
	unsigned int uVar11;

	bVar2 = true;
	bVar3 = false;
	if (header == NULL) {
		return SCE_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
	}
	if (((fieldName == NULL) || (fieldValue == NULL)) || (valueLen == 0)) {
		return SCE_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE;
	}
	uVar5 = sceClibStrnlen(fieldName, 0xfff);
	uVar9 = 0;
	if (headerLen != 0) {
		pcVar10 = header + uVar5;
		pcVar7 = header;
		uVar8 = uVar5;
		do {
			cVar1 = *pcVar7;
			if (bVar2) {
				bVar4 = sceClibLookCtypeTable(cVar1);
				if ((((bVar4 & 8) == 0) && (uVar5 < headerLen - uVar9)) &&
					((iVar6 = sceClibStrncasecmp(fieldName, pcVar7, uVar5), iVar6 == 0 && (*pcVar10 == ':'))))
				{
					if ((headerLen < uVar8) || (header[uVar8] != ':')) {
						return SCE_HTTP_ERROR_PARSE_HTTP_NOT_FOUND;
					}
					uVar8 = uVar8 + 1;
					if (headerLen <= uVar8) goto LAB_8100df2e;
					pcVar7 = header + uVar8;
					goto LAB_8100df18;
				}
				cVar1 = *pcVar7;
				bVar2 = bVar3;
			}
			if (cVar1 == '\n') {
				bVar2 = true;
			}
			uVar9 = uVar9 + 1;
			pcVar10 = pcVar10 + 1;
			uVar8 = uVar8 + 1;
			pcVar7 = pcVar7 + 1;
		} while (uVar9 < headerLen);
	}
	return SCE_HTTP_ERROR_PARSE_HTTP_NOT_FOUND;
	while (true) {
		uVar8 = uVar8 + 1;
		pcVar7 = pcVar7 + 1;
		if (headerLen <= uVar8) break;
	LAB_8100df18:
		bVar4 = sceClibLookCtypeTable(*pcVar7);
		if ((bVar4 & 8) == 0) break;
	}
LAB_8100df2e:
	uVar5 = uVar8;
	if (uVar8 < headerLen) {
		pcVar7 = header + uVar8;
		do {
			uVar9 = uVar5 + 1;
			if (*pcVar7 == '\n') {
				uVar11 = uVar5 - 1;
				if ((uVar11 != 0) && (pcVar7[-1] == '\r')) {
					bVar3 = true;
				}
				if (((uVar9 < headerLen) && (pcVar7[1] != ' ')) && (pcVar7[1] != '\t')) {
					headerLen = uVar5;
					if (bVar3) {
						uVar5 = uVar11;
						headerLen = uVar11;
					}
					break;
				}
			}
			pcVar7 = pcVar7 + 1;
			uVar5 = uVar9;
		} while (uVar9 < headerLen);
	}
	*fieldValue = header + uVar8;
	*valueLen = headerLen - uVar8;
	return uVar5;
}