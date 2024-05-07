#include <windows.h>
#include "tp_stub.h"

#define EXPORT(hr) extern "C" __declspec(dllexport) hr __stdcall

int WINAPI
DllEntryPoint(HINSTANCE /*hinst*/, unsigned long /*reason*/, void* /*lpReserved*/)
{
	return 1;
}

static tjs_int GlobalRefCountAtInit = 0;

extern bool onV2Link();
extern bool onV2Unlink();

EXPORT(HRESULT) V2Link(iTVPFunctionExporter *exporter)
{
	TVPInitImportStub(exporter);

	if (!onV2Link()) return E_FAIL;

	GlobalRefCountAtInit = TVPPluginGlobalRefCount;
	return S_OK;
}
EXPORT(HRESULT) V2Unlink()
{
	if (TVPPluginGlobalRefCount > GlobalRefCountAtInit ||
		!onV2Unlink()) return E_FAIL;

	TVPUninitImportStub();
	return S_OK;
}
