//
// Created by jack on 3/19/24.
//
#include "gsm.h"

#ifndef TIME_UTILS_H
#define TIME_UTILS_H

const char* getCurrentDateTimeWithMillis();
extern gsm global_gsm_state_4encode;
extern gsm global_gsm_state_4decode;
unsigned int crc32b(unsigned char *message, size_t len);
#endif // TIME_UTILS_H