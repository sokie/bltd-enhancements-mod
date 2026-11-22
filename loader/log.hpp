#pragma once
#include <stdint.h>
#include <string>

#define LOG_LEVEL_MASK		0x3F
#define LOG_LEVEL_TRACE		0x01
#define LOG_LEVEL_DEBUG		0x02
#define LOG_LEVEL_INFO		0x04
#define LOG_LEVEL_WARN		0x08
#define LOG_LEVEL_ERROR		0x10
#define LOG_LEVEL_FATAL		0x20

namespace Log {

void Open(const std::wstring& path);
void Write(uint32_t level, const char* format, ...);

}
