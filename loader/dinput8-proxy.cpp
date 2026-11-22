#include <Windows.h>
#include <unknwn.h>

static HMODULE real_dinput8 = 0;

static FARPROC RealProc(const char* name)
{
	if (!real_dinput8) {
		wchar_t path[MAX_PATH];
		UINT length = GetSystemDirectoryW(path, MAX_PATH);
		if (length == 0 || length + 13 > MAX_PATH) {
			return 0;
		}
		wcscat_s(path, MAX_PATH, L"\\dinput8.dll");
		real_dinput8 = LoadLibraryW(path);
	}
	return real_dinput8 ? GetProcAddress(real_dinput8, name) : 0;
}

extern "C" {

HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, void** out, IUnknown* outer)
{
	typedef HRESULT (WINAPI* tCreate)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
	tCreate create = (tCreate)RealProc("DirectInput8Create");
	return create ? create(instance, version, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI DllCanUnloadNow()
{
	typedef HRESULT (WINAPI* tCall)();
	tCall call = (tCall)RealProc("DllCanUnloadNow");
	return call ? call() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** out)
{
	typedef HRESULT (WINAPI* tCall)(REFCLSID, REFIID, void**);
	tCall call = (tCall)RealProc("DllGetClassObject");
	return call ? call(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer()
{
	typedef HRESULT (WINAPI* tCall)();
	tCall call = (tCall)RealProc("DllRegisterServer");
	return call ? call() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer()
{
	typedef HRESULT (WINAPI* tCall)();
	tCall call = (tCall)RealProc("DllUnregisterServer");
	return call ? call() : E_FAIL;
}

const void* WINAPI GetdfDIJoystick()
{
	typedef const void* (WINAPI* tCall)();
	tCall call = (tCall)RealProc("GetdfDIJoystick");
	return call ? call() : 0;
}

}
