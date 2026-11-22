#include "bots-internal.hpp"
#include "maps.hpp"
#include "platform.hpp"
#include "../loader/hook.hpp"
#include <shlobj.h>
#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

#define MAPS_LOG(level, format, ...) Log::Write(level, "[Maps] " format, __VA_ARGS__)

namespace Bots {
namespace Maps {

static const uint32_t RVA_PACKAGE_FILE_CACHE_VTABLE = 0x00CF9100;
static const wchar_t* const MAP_INFO_INI = L"\\My Games\\Blacklight-Tango Down\\FoxGame\\Config\\FOXMapInfo.ini";
static const wchar_t* const SHADER_CACHE_DIR = L"\\My Games\\Blacklight-Tango Down\\FoxGame\\CookedPCConsole\\";
static const wchar_t* const SHADER_CACHE_NAME = L"LocalShaderCache-PC-D3D-SM3";
static const uint32_t PACKAGE_TAG = 0x9E2A83C1;
static const uint16_t PACKAGE_VERSION = 673;

typedef void(__fastcall* tCachePaths)(void* self, void* unused_edx);
typedef int(__fastcall* tCachePackage)(void* self, void* unused_edx, const wchar_t* path, int overrideDuplicate, int warnIfExists);

struct MenuEntry {
	const wchar_t* File;
	const char* Section;
	const char* Body;
};

static const MenuEntry MENU_ENTRIES[] = {
	{ L"Crossover.upk", "[Crossover FoxDataProvider_MapInfo]",
		"[Crossover FoxDataProvider_MapInfo]\r\nMapName=Crossover\r\nPresenceContext=18\r\nPreviewImageMarkup=UI_Main_Images.Content.t_cross\r\n\r\n" },
};

static tCachePaths original_cache_paths = 0;
static std::vector<std::wstring> packages;
static std::wstring menu_ini;
static wchar_t shader_cache[MAX_PATH];

static std::vector<std::wstring> FindPackages(const std::wstring& pattern)
{
	std::vector<std::wstring> found;
	std::wstring folder = pattern.substr(0, pattern.find_last_of(L'\\'));
	WIN32_FIND_DATAW entry;
	HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
	if (search == INVALID_HANDLE_VALUE) {
		return found;
	}
	do {
		if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			found.push_back(folder + L"\\" + entry.cFileName);
		}
	} while (FindNextFileW(search, &entry));
	FindClose(search);
	return found;
}

static const uint32_t RVA_GET_FULL_FILE_PATH = 0x009183E0;
static const int GET_FULL_FILE_PATH_DETOUR_LEN = 9;
static const uint8_t GET_FULL_FILE_PATH_PROLOGUE[GET_FULL_FILE_PATH_DETOUR_LEN] = { 0x8B, 0x44, 0x24, 0x04, 0x55, 0x8B, 0xE9, 0x85, 0xC0 };
static const uint32_t AK_SUCCESS = 1;
static const size_t AK_MAX_PATH = 0x104;
typedef uint32_t (__stdcall* tGetFullFilePath)(void* self, const wchar_t* name, void* flags, wchar_t* out);
static tGetFullFilePath get_full_file_path_original = 0;
static std::vector<std::wstring> banks;

static uint32_t __stdcall GetFullFilePathHook(void* self, const wchar_t* name, void* flags, wchar_t* out)
{
	uint32_t result = get_full_file_path_original(self, name, flags, out);
	if (result != AK_SUCCESS || !name || !out) {
		return result;
	}
	for (const std::wstring& bank : banks) {
		if (_wcsicmp(bank.c_str() + bank.find_last_of(L'\\') + 1, name) == 0) {
			wcscpy_s(out, AK_MAX_PATH, bank.c_str());
			break;
		}
	}
	return result;
}

static void HookBankPaths(uintptr_t title_base)
{
	uint8_t* address = (uint8_t*)(title_base + RVA_GET_FULL_FILE_PATH);
	if (memcmp(address, GET_FULL_FILE_PATH_PROLOGUE, GET_FULL_FILE_PATH_DETOUR_LEN) != 0) {
		MAPS_LOG(LOG_LEVEL_WARN, "%s", "The sound bank path code does not match, the banks in the maps folder stay unused.");
		return;
	}
	get_full_file_path_original = (tGetFullFilePath)DetourClassFunc(address, (BYTE*)GetFullFilePathHook, GET_FULL_FILE_PATH_DETOUR_LEN);
	MAPS_LOG(LOG_LEVEL_INFO, "%u sound bank(s) in the maps folder replace the game's banks.", (uint32_t)banks.size());
}

static const uint32_t RVA_FILE_MANAGER_VTABLE = 0x00FFC968;
static const int CREATE_READER_SLOT = 1;
static const uint32_t RVA_CREATE_READER = 0x0000AFD0;
static const int FILE_SIZE_SLOT = 30;
static const uint32_t RVA_FILE_SIZE = 0x00007850;
static const wchar_t MOVIE_FOLDER[] = L"\\Movies";
static const int MOVIE_FOLDER_LENGTH = 7;
typedef void* (__fastcall* tCreateReader)(void* self, void* unused_edx, const wchar_t* name, uint32_t flags, void* error);
typedef int32_t (__fastcall* tFileSize)(void* self, void* unused_edx, const wchar_t* name);
static tCreateReader create_reader_original = 0;
static tFileSize file_size_original = 0;
static std::vector<std::wstring> movies;

static const wchar_t* MovieFile(const wchar_t* path)
{
	const wchar_t* file = path ? wcsrchr(path, L'\\') : 0;
	if (!file || file - path < MOVIE_FOLDER_LENGTH || _wcsnicmp(file - MOVIE_FOLDER_LENGTH, MOVIE_FOLDER, MOVIE_FOLDER_LENGTH) != 0) {
		return path;
	}
	for (const std::wstring& movie : movies) {
		if (_wcsicmp(movie.c_str() + movie.find_last_of(L'\\') + 1, file + 1) == 0) {
			return movie.c_str();
		}
	}
	return path;
}

static void* __fastcall CreateReaderHook(void* self, void* unused_edx, const wchar_t* name, uint32_t flags, void* error)
{
	return create_reader_original(self, unused_edx, MovieFile(name), flags, error);
}

static int32_t __fastcall FileSizeHook(void* self, void* unused_edx, const wchar_t* name)
{
	return file_size_original(self, unused_edx, MovieFile(name));
}

static void HookMovieFiles(uintptr_t title_base)
{
	uint32_t* table = (uint32_t*)(title_base + RVA_FILE_MANAGER_VTABLE);
	if (table[CREATE_READER_SLOT] != title_base + RVA_CREATE_READER || table[FILE_SIZE_SLOT] != title_base + RVA_FILE_SIZE) {
		MAPS_LOG(LOG_LEVEL_WARN, "%s", "The file manager code does not match, the movies in the maps folder stay unused.");
		return;
	}
	DWORD protection;
	if (!VirtualProtect(table, sizeof(*table) * (FILE_SIZE_SLOT + 1), PAGE_READWRITE, &protection)) {
		MAPS_LOG(LOG_LEVEL_WARN, "%s", "The file manager vtable cannot be written, the movies in the maps folder stay unused.");
		return;
	}
	create_reader_original = (tCreateReader)table[CREATE_READER_SLOT];
	file_size_original = (tFileSize)table[FILE_SIZE_SLOT];
	table[CREATE_READER_SLOT] = (uint32_t)CreateReaderHook;
	table[FILE_SIZE_SLOT] = (uint32_t)FileSizeHook;
	VirtualProtect(table, sizeof(*table) * (FILE_SIZE_SLOT + 1), protection, &protection);
	MAPS_LOG(LOG_LEVEL_INFO, "%u movie file(s) in the maps folder replace the game's movies.", (uint32_t)movies.size());
}

static void __fastcall CachePathsHook(void* self, void* unused_edx)
{
	original_cache_paths(self, unused_edx);
	tCachePackage cachePackage = (tCachePackage)(*(void***)self)[1];
	uint32_t registered = 0;
	for (const std::wstring& package : packages) {
		if (cachePackage(self, 0, package.c_str(), 1, 0)) {
			registered++;
		}
		else {
			MAPS_LOG(LOG_LEVEL_WARN, "The package cache refused %ls.", package.c_str());
		}
	}
	MAPS_LOG(LOG_LEVEL_INFO, "%u of %u package(s) registered with the package cache.", registered, (uint32_t)packages.size());
	// The engine reads the map info ini after this call, so the menus list the added maps.
	AddMenuEntries();
}

static bool HasFile(const wchar_t* name)
{
	for (const std::wstring& package : packages) {
		size_t slash = package.rfind(L'\\');
		if (_wcsicmp(package.c_str() + (slash == std::wstring::npos ? 0 : slash + 1), name) == 0) {
			return true;
		}
	}
	return false;
}

static bool ReadText(const std::wstring& path, std::string& text)
{
	FILE* file = 0;
	if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
		return false;
	}
	char buffer[4096];
	size_t read;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		text.append(buffer, read);
	}
	fclose(file);
	return true;
}

void AddMenuEntries()
{
	if (packages.empty()) {
		return;
	}
	wchar_t documents[MAX_PATH];
	if (FAILED(SHGetFolderPathW(0, CSIDL_PERSONAL, 0, SHGFP_TYPE_CURRENT, documents))) {
		return;
	}
	std::wstring path = std::wstring(documents) + MAP_INFO_INI;
	menu_ini = path;
	swprintf_s(shader_cache, L"%ls%ls%ls.upk", documents, SHADER_CACHE_DIR, SHADER_CACHE_NAME);
	std::string text;
	if (!ReadText(path, text)) {
		MAPS_LOG(LOG_LEVEL_INFO, "%ls does not exist yet, the menu entries wait for the next run.", path.c_str());
		return;
	}
	FILE* file = 0;
	bool changed = false;
	for (const MenuEntry& entry : MENU_ENTRIES) {
		if (!HasFile(entry.File) || text.find(entry.Section) != std::string::npos) {
			continue;
		}
		size_t version = text.find("[IniVersion]");
		text.insert(version == std::string::npos ? text.size() : version, entry.Body);
		changed = true;
		MAPS_LOG(LOG_LEVEL_INFO, "%s added to %ls.", entry.Section, path.c_str());
	}
	if (!changed) {
		return;
	}
	if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) {
		MAPS_LOG(LOG_LEVEL_WARN, "%ls cannot be written, the menu entries are missing.", path.c_str());
		return;
	}
	fwrite(text.data(), 1, text.size(), file);
	fclose(file);
}

static uint32_t NameCrc(const wchar_t* name)
{
	uint32_t crc = 0xFFFFFFFF;
	for (; *name; name++) {
		wchar_t c = *name >= L'a' && *name <= L'z' ? *name - (L'a' - L'A') : *name;
		const uint8_t bytes[2] = { (uint8_t)(c & 0xFF), (uint8_t)(c >> 8) };
		for (uint8_t byte : bytes) {
			crc ^= (uint32_t)byte << 24;
			for (int bit = 0; bit < 8; bit++) {
				crc = crc & 0x80000000 ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
			}
		}
	}
	return ~crc;
}

static uint32_t PackageSourceOffset(const uint8_t* header, uint32_t size)
{
	uint32_t at = 12;
	if (size < at + 4 || *(const uint32_t*)header != PACKAGE_TAG || *(const uint16_t*)(header + 4) != PACKAGE_VERSION) {
		return 0;
	}
	int32_t folder = *(const int32_t*)(header + at);
	at += 4 + (folder >= 0 ? folder : -2 * folder);
	at += 4 + 7 * 4 + 3 * 4 + 4 + 16;
	if (size < at + 4) {
		return 0;
	}
	at += 4 + *(const int32_t*)(header + at) * 12;
	at += 12;
	if (size < at + 4) {
		return 0;
	}
	at += 4 + *(const int32_t*)(header + at) * 16;
	return size < at + 4 ? 0 : at;
}

// Writes the CRC of the shader cache's name as its PackageSource. With another value the game takes the cache as user
// content and stops saving progress. Runs at process exit, so it uses no heap.
void StampShaderCache()
{
	if (!shader_cache[0]) {
		return;
	}
	HANDLE file = CreateFileW(shader_cache, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}
	uint8_t header[4096];
	DWORD size = 0;
	if (ReadFile(file, header, sizeof(header), &size, 0)) {
		uint32_t offset = PackageSourceOffset(header, size);
		uint32_t source = NameCrc(SHADER_CACHE_NAME);
		DWORD written = 0;
		if (offset && *(const uint32_t*)(header + offset) != source && SetFilePointer(file, offset, 0, FILE_BEGIN) == offset) {
			WriteFile(file, &source, sizeof(source), &written, 0);
		}
	}
	CloseHandle(file);
}

void RemoveMenuEntries()
{
	std::string text;
	if (menu_ini.empty() || !ReadText(menu_ini, text)) {
		return;
	}
	bool changed = false;
	for (const MenuEntry& entry : MENU_ENTRIES) {
		size_t start = text.find(entry.Section);
		if (start == std::string::npos) {
			continue;
		}
		size_t next = text.find("\n[", start);
		text.erase(start, next == std::string::npos ? std::string::npos : next + 1 - start);
		changed = true;
	}
	FILE* file = 0;
	if (!changed || _wfopen_s(&file, menu_ini.c_str(), L"wb") != 0 || !file) {
		return;
	}
	fwrite(text.data(), 1, text.size(), file);
	fclose(file);
}

bool Install(uintptr_t title_base)
{
	std::wstring pattern = Platform::FilePath(Platform::FILE_MAP, L"*.upk", false);
	if (!pattern.empty()) {
		packages = FindPackages(pattern);
		banks = FindPackages(Platform::FilePath(Platform::FILE_MAP, L"*.bnk", false));
		movies = FindPackages(Platform::FilePath(Platform::FILE_MAP, L"*.bik", false));
		std::vector<std::wstring> subtitles = FindPackages(Platform::FilePath(Platform::FILE_MAP, L"*.txt", false));
		movies.insert(movies.end(), subtitles.begin(), subtitles.end());
	}
	if (packages.empty() && banks.empty() && movies.empty()) {
		MAPS_LOG(LOG_LEVEL_INFO, "Nothing at %ls.", pattern.c_str());
		return true;
	}
	if (!banks.empty()) {
		HookBankPaths(title_base);
	}
	if (!movies.empty()) {
		HookMovieFiles(title_base);
	}
	if (packages.empty()) {
		return true;
	}
	uint32_t* slot = (uint32_t*)(title_base + RVA_PACKAGE_FILE_CACHE_VTABLE);
	DWORD protection;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
		MAPS_LOG(LOG_LEVEL_WARN, "%s", "The package cache vtable cannot be written, the maps are not registered.");
		return false;
	}
	original_cache_paths = (tCachePaths)*slot;
	*slot = (uint32_t)CachePathsHook;
	VirtualProtect(slot, sizeof(*slot), protection, &protection);
	MAPS_LOG(LOG_LEVEL_INFO, "%u package(s) at %ls wait for the package cache.", (uint32_t)packages.size(), pattern.c_str());
	return true;
}

}
}
