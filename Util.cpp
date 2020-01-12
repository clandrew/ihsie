#include "pch.h"
#include "Util.h"

#if _DEBUG
void DebugEvent()
{
	__debugbreak();
}
#else
void DebugEvent()
{
}
#endif

int RgbToPalletized(uint32_t rgb)
{
	uint32_t palletizedColor = 0;
	if (rgb == 0xFF505050)
	{
		palletizedColor = 0;
	}
	else if (rgb == 0xFFE0A0C0)
	{
		palletizedColor = 1;
	}
	else if (rgb == 0xFF000000)
	{
		palletizedColor = 2;
	}
	else if (rgb == 0xFF60A0c0)
	{
		palletizedColor = 3;
	}
	else
	{
		palletizedColor = -1;
	}

	return palletizedColor;
}

uint32_t PalletizedToRgba(int palletizedColor)
{
	uint32_t rgba = 0;
	if (palletizedColor == 0)
	{
		rgba = 0xFF505050; // Background
	}
	if (palletizedColor == 1)
	{
		rgba = 0xFFE0A0C0; // Skin tone
	}
	else if (palletizedColor == 2)
	{
		rgba = 0xFF000000; // Outline
	}
	else if (palletizedColor == 3)
	{
		rgba = 0xFF60A0c0; // Equipment
	}

	return rgba;
}

bool CheckCOMResult(HRESULT hr)
{
	if (FAILED(hr))
	{
		std::wcout << L"Encountered COM error 0x" << std::hex << hr << L".\n";
		DebugEvent();
		return false;
	}
	return true;
}

bool CheckErrno(errno_t err)
{
	if (err != 0)
	{
		std::wcout << L"Encountered error code: " << err << L".\n";
		DebugEvent();
		return false;
	}
	return true;
}

bool CheckZero(int errorCode)
{
	if (errorCode != 0)
	{
		std::wcout << L"Encountered error code: " << errorCode << L".\n";
		DebugEvent();
		return false;
	}
	return true;
}