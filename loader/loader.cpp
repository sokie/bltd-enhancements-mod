#include "loader.hpp"
#include "../bltd-bots/platform.hpp"
#include <stdint.h>

namespace Loader {

static const uint32_t TITLE_TIME_DATE_STAMP = 0x4CDC6F6D;
static const uint32_t TITLE_SIZE_OF_IMAGE = 0x0140E000;

static HMODULE this_module = 0;

void SetModule(HMODULE module)
{
	this_module = module;
}

std::wstring ModulePath()
{
	wchar_t path[MAX_PATH];
	DWORD length = GetModuleFileNameW(this_module, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return L"";
	}
	return std::wstring(path, length);
}

std::wstring DataDir(bool create)
{
	std::wstring path = ModulePath();
	size_t slash = path.find_last_of(L'\\');
	if (path.empty() || slash == std::wstring::npos) {
		return L"";
	}
	std::wstring folder = path.substr(0, slash);
	size_t parent = folder.find_last_of(L'\\');
	std::wstring folderName = parent == std::wstring::npos ? folder : folder.substr(parent + 1);
	if (_wcsicmp(folderName.c_str(), DATA_FOLDER) != 0) {
		folder += L"\\";
		folder += DATA_FOLDER;
		if (create) {
			CreateDirectoryW(folder.c_str(), 0);
		}
	}
	return folder;
}

std::wstring LogPath()
{
	std::wstring folder = DataDir(true);
	if (folder.empty()) {
		return L"";
	}
	return folder + L"\\" + BOTS_NAME + L".log";
}

bool TitleMatches(HMODULE title)
{
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)title;
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return false;
	}
	const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)((const uint8_t*)title + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.TimeDateStamp == TITLE_TIME_DATE_STAMP
		&& nt->OptionalHeader.SizeOfImage == TITLE_SIZE_OF_IMAGE;
}

}

std::wstring Platform::FilePath(FileKind kind, const wchar_t* name, bool create_folder)
{
	std::wstring folder = Loader::DataDir(create_folder);
	if (folder.empty()) {
		return L"";
	}
	if (kind == FILE_INI) {
		return folder + L"\\" + Loader::BOTS_NAME + L".ini";
	}
	folder += kind == FILE_NAVCACHE ? L"\\navcache" : kind == FILE_MAP ? L"\\maps" : L"\\ui";
	if (create_folder) {
		CreateDirectoryW(folder.c_str(), 0);
	}
	return folder + L"\\" + name;
}
