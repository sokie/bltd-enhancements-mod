#pragma once
#include <winsock2.h>
#include <Windows.h>
#include <string>

namespace Loader {

static const wchar_t* const DATA_FOLDER = L"bltd-bots";
static const wchar_t* const BOTS_NAME = L"bltd-bots";

void SetModule(HMODULE module);
std::wstring ModulePath();
std::wstring DataDir(bool create);
std::wstring LogPath();
bool TitleMatches(HMODULE title);

}
