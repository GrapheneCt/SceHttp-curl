#pragma once

#include <kernel.h>
#include <libhttp.h>

void *_sceHttpMalloc(unsigned int size);
void _sceHttpFree(void *ptr);
void *_sceHttpRealloc(void *ptr, unsigned int newsize);

int _sceHttpNamedObjectInit();
int _sceHttpNamedObjectTerm();
void *_sceHttpNamedObjectGet(int id);
int _sceHttpNamedObjectGetName(void *obj);
void _sceHttpNamedObjectRemove(int id);
int _sceHttpNamedObjectAdd(void *obj);

size_t _sceHttpResponseHeaderWriteCb(char *buffer, size_t size, size_t nitems, void *userdata);

SceInt32 _sceHttpCurlErrorToSceHttp(CURLcode err);

size_t _sceHttpGetMethodWriteCb(char *buffer, size_t size, size_t nitems, void *userdata);
SceVoid  _sceHttpFiberEntry(SceUInt32 argOnInitialize, SceUInt32 argOnRun);