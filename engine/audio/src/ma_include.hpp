// Includes the vendored miniaudio header with its warnings silenced (third-party code; rule 1 covers
// our code only). The include dir is also SYSTEM in CMake. MA_* configuration macros come from the
// alamo_miniaudio target so every includer sees the same struct layouts.
#pragma once
#pragma warning(push, 0)
#include "miniaudio.h"
#pragma warning(pop)
