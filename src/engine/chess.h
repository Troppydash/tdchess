#pragma once

// use pext extension for performance, this include must be first
#if defined(__BMI2__)
#define CHESS_USE_PEXT
#endif

#define CHESS_NO_EXCEPTIONS
#include "../hpplib/chess.h"