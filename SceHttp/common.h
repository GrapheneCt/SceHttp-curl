#pragma once

#include <kernel.h>
#include <libhttp.h>
#include <sce_fiber.h>
#include <psp2_compat/curl/curl.h>

#define SCE_HTTP_FIBER_OK 1000
#define SCE_HTTP_FIBER_NEED_TERM 2000

typedef struct SceHttpRequest {
	CURL *curl;

	SceSize responseHeaderMaxSize;
	ScePVoid responseHeaderStorage;
	SceSize responseHeaderSize;

	SceFiber fiber;
	SceChar8 fiberCtx[SCE_KERNEL_16KiB] __attribute__((aligned(SCE_FIBER_CONTEXT_ALIGNMENT)));
	curl_off_t contentLength;
	SceChar8 *dst;
	SceUInt32 toRead;
	SceOff pos;
	SceOff cl;
	SceInt32 lastError;

} SceHttpRequest;