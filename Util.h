#pragma once

void DebugEvent();

int RgbToPalletized(uint32_t rgb);
uint32_t PalletizedToRgba(int palletizedColor);

bool CheckCOMResult(HRESULT hr);
bool CheckErrno(errno_t err);
bool CheckZero(int errorCode);