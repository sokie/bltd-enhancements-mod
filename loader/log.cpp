#include "log.hpp"
#include <winsock2.h>
#include <Windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Log {

static HANDLE file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION lock;
static bool lock_ready = false;

static const char* LevelName(uint32_t level)
{
	switch (level & LOG_LEVEL_MASK) {
	case LOG_LEVEL_DEBUG: return "DEBUG";
	case LOG_LEVEL_INFO: return "INFO ";
	case LOG_LEVEL_WARN: return "WARN ";
	case LOG_LEVEL_ERROR: return "ERROR";
	case LOG_LEVEL_FATAL: return "FATAL";
	default: return "TRACE";
	}
}

void Open(const std::wstring& path)
{
	if (!lock_ready) {
		InitializeCriticalSection(&lock);
		lock_ready = true;
	}
	EnterCriticalSection(&lock);
	if (file != INVALID_HANDLE_VALUE) {
		CloseHandle(file);
	}
	file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	LeaveCriticalSection(&lock);
}

void Write(uint32_t level, const char* format, ...)
{
	if (!lock_ready || (level & LOG_LEVEL_MASK) == LOG_LEVEL_TRACE) {
		return;
	}
	char line[4096];
	SYSTEMTIME now;
	GetLocalTime(&now);
	int length = sprintf_s(line, sizeof(line), "%02u:%02u:%02u.%03u %s ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, LevelName(level));
	va_list args;
	va_start(args, format);
	int written = _vsnprintf_s(line + length, sizeof(line) - length - 2, _TRUNCATE, format, args);
	va_end(args);
	length += written < 0 ? (int)strlen(line + length) : written;
	line[length++] = '\r';
	line[length++] = '\n';
	line[length] = 0;
	EnterCriticalSection(&lock);
	if (file != INVALID_HANDLE_VALUE) {
		DWORD done = 0;
		WriteFile(file, line, (DWORD)length, &done, 0);
	}
	LeaveCriticalSection(&lock);
	OutputDebugStringA(line);
}

}
