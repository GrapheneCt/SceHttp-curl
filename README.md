# SceHttp-curl
SceHttp reimplementation for PSP2 that uses libcurl internally.
Currently unfinished, but basic GET functionality is well tested and works fine.

Some ideas for future development:
1. For proper reimplementation, need to create internal heap during init (it uses sce_paf_ allocators currently).
2. Get rid of SceFiber since it eats 16KiB of memory per SceHttp object. Maybe curl_multi_ + pause?
