#include "max.h"


HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		// Max ships language packs, This selects the right one before we load any resources.
		MaxSDK::Util::UseLanguagePackLocale();

		hInstance = hinstDLL;
		// Thread attach and detach will not call Dllmain again.
		DisableThreadLibraryCalls(hInstance);
	}

	return TRUE;
}

__declspec(dllexport) const MCHAR* LibDescription()
{
	return _T("FormClay");
}

__declspec(dllexport) int LibNumberClasses()
{
	return 1;
}


__declspec(dllexport) ULONG LibVersion()
{
	return VERSION_3DSMAX;
}

extern ClassDesc2* GetFormClayDesc();
__declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
	switch (i)
	{
	case 0: return GetFormClayDesc();
	default: return nullptr;
	}
}
