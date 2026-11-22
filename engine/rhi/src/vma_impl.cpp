// The single VMA implementation unit. Third-party code: its warnings are silenced locally so the
// rest of alamo_rhi stays /W4 clean.
#pragma warning(push, 0)

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#pragma warning(pop)
