#pragma once
#include <string>

namespace Platform {

enum FileKind {
	FILE_INI,
	FILE_NAVCACHE,
	FILE_UI,
	FILE_MAP,
};

std::wstring FilePath(FileKind kind, const wchar_t* name, bool create_folder);

}
