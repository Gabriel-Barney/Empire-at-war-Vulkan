# cmake -DMSG=<text> -P cmake/fail.cmake: prints MSG and exits non-zero (placeholder test entries).
message(FATAL_ERROR "${MSG}")
