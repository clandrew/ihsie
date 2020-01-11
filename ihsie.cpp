// ihsie.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

// Constants
static const int tileWidth = 8;
static const int tilesWide = 16;
static const int tileHeight = 8;
static const int tilesHigh = 16;
static const int bitmapWidth = tileWidth * tilesWide;
static const int bitmapHeight = tileHeight * tilesHigh;
static const int bitSelectReference[] = { 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1 };
static const long baseOffset = 0x8010;

// Globals
ComPtr<IWICImagingFactory> wicImagingFactory;

struct Tile
{
	byte Data[16];
	int Palletized[8 * 8];
};

bool CheckCOMResult(HRESULT hr)
{
	if (FAILED(hr))
	{
		std::wcout << L"Encountered COM error 0x" << std::hex << hr << L".\n";
		__debugbreak();
		return false;
	}
	return true;
}

bool CheckErrno(errno_t err)
{
	if (err != 0)
	{
		std::wcout << L"Encountered error code: " << err << L".\n";
		__debugbreak();
		return false;
	}
	return true;
}

bool CheckZero(int errorCode)
{
	if (errorCode != 0)
	{
		std::wcout << L"Encountered error code: " << errorCode << L".\n";
		__debugbreak();
		return false;
	}
	return true;
}

int Decode(Tile const& tile, int firstByteIndex, int secondByteIndex, int bitSelect)
{
	byte b0 = tile.Data[firstByteIndex];
	byte b1 = tile.Data[secondByteIndex];

	byte f0 = b0 & bitSelect;
	byte f1 = b1 & bitSelect;

	int result = 0;
	if (f0 & f1)
	{
		result = 3;
	}
	else if (f1)
	{
		result = 2;
	}
	else if (f0)
	{
		result = 1;
	}
	return result;
}

bool Export(std::wstring romFilename, std::wstring imageFilename)
{
	// Load ROM file
	FILE* file = {};	
	if (!CheckErrno(_wfopen_s(&file, romFilename.c_str(), L"rb")))
	{
		return false;
	}

	std::vector<Tile> tiles;
	tiles.resize(256);

	for (int i = 0; i < 256; ++i)
	{
		long tileOffset = baseOffset + (i * 0x10);
		if (!CheckZero(fseek(file, tileOffset, SEEK_SET)))
		{
			return false;
		}
		size_t readCount = fread(tiles[i].Data, 1, 16, file);
		if (readCount != 16)
		{
			std::wcout << L"Encountered I/O error when reading a file.\n";
			__debugbreak();
			return false;
		}
	}

	if (fclose(file) != 0)
	{
		std::wcout << L"Encountered I/O error when closing a file.\n";
		__debugbreak();
		return false;
	}

	for (int i = 0; i < 256; ++i)
	{
		for (int y = 0; y < 8; ++y)
		{
			for (int x = 0; x < 8; ++x)
			{
				int pixelIndex = (y * 8) + x;
				tiles[i].Palletized[pixelIndex] = Decode(tiles[i], 0 + y, 8 + y, bitSelectReference[x]);
			}
		}
	}

	// Create WIC bitmap here.

	ComPtr<IWICStream> stream;
	if (!CheckCOMResult(wicImagingFactory->CreateStream(&stream)))
	{
		return false;
	}

	std::wstring destFilename = imageFilename;
	if (!CheckCOMResult(stream->InitializeFromFilename(destFilename.c_str(), GENERIC_WRITE)))
	{
		return false;
	}

	ComPtr<IWICBitmapEncoder> encoder;
	if (!CheckCOMResult(wicImagingFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder)))
	{
		return false;
	}

	if (!CheckCOMResult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
	{
		return false;
	}

	ComPtr<IWICBitmap> wicBitmap;
	if (!CheckCOMResult(wicImagingFactory->CreateBitmap(
		bitmapWidth,
		bitmapHeight,
		GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wicBitmap)))
	{
		return false;
	}

	ComPtr<IWICBitmapFrameEncode> frameEncode;
	if (!CheckCOMResult(encoder->CreateNewFrame(&frameEncode, nullptr)))
	{
		return false;
	}

	if (!CheckCOMResult(frameEncode->Initialize(nullptr)))
	{
		return false;
	}

	if (!CheckCOMResult(frameEncode->SetSize(bitmapWidth, bitmapHeight)))
	{
		return false;
	}

	if (!CheckCOMResult(frameEncode->SetResolution(96, 96)))
	{
		return false;
	}

	WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
	if (!CheckCOMResult(frameEncode->SetPixelFormat(&pixelFormat)))
	{
		return false;
	}

	{
		ComPtr<IWICBitmapLock> bitmapLock;
		WICRect rcLock = { 0, 0, bitmapWidth, bitmapHeight };
		if (!CheckCOMResult(wicBitmap->Lock(&rcLock, WICBitmapLockWrite, &bitmapLock)))
		{
			return false;
		}

		UINT cbBufferSize = 0;
		BYTE* pv = NULL;

		if (!CheckCOMResult(bitmapLock->GetDataPointer(&cbBufferSize, &pv)))
		{
			return false;
		}

		uint32_t* pixelData = reinterpret_cast<uint32_t*>(pv);

		for (int tileY = 0; tileY < tilesHigh; ++tileY)
		{
			int destYOrigin = tileY * tileHeight;

			for (int tileX = 0; tileX < tilesWide; ++tileX)
			{
				int destXOrigin = tileX * tileWidth;

				int tileIndex = (tileY * tilesWide) + tileX;
				Tile& tile = tiles[tileIndex];

				for (int y = 0; y < 8; ++y)
				{
					for (int x = 0; x < 8; ++x)
					{
						int palletizedIndex = (y * 8) + x;
						int palletizedColor = tile.Palletized[palletizedIndex];
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

						int destX = destXOrigin + x;
						int destY = destYOrigin + y;
						int destPixelIndex = (destY * bitmapWidth) + destX;
						pixelData[destPixelIndex] = rgba;
					}
				}
			}
		}
	}

	if (!CheckCOMResult(frameEncode->WriteSource(
		wicBitmap.Get(),
		NULL)))
	{
		return false;
	}

	if (!CheckCOMResult(frameEncode->Commit()))
	{
		return false;
	}

	if (!CheckCOMResult(encoder->Commit()))
	{
		return false;
	}

	if (!CheckCOMResult(stream->Commit(STGC_DEFAULT)))
	{
		return false;
	}

	return true;
}

bool Import(std::wstring sourceFilename, std::wstring romFilename)
{
	ComPtr<IWICBitmapDecoder> spDecoder;
	if (!CheckCOMResult(
		wicImagingFactory->CreateDecoderFromFilename(sourceFilename.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &spDecoder)))
	{
		return false;
	}

	ComPtr<IWICBitmapFrameDecode> spSource;
	if (!CheckCOMResult(spDecoder->GetFrame(0, &spSource)))
	{
		return false;
	}

	ComPtr<IWICFormatConverter> spConverter;
	if (!CheckCOMResult(wicImagingFactory->CreateFormatConverter(&spConverter)))
	{
		return false;
	}

	if (!CheckCOMResult(spConverter->Initialize(
		spSource.Get(),
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		NULL,
		0.f,
		WICBitmapPaletteTypeMedianCut)))
	{
		return false;
	}

	uint32_t refImageWidth, refImageHeight;
	if (!CheckCOMResult(spConverter->GetSize(&refImageWidth, &refImageHeight)))
	{
		return false;
	}

	std::vector<uint32_t> rgbData;
	rgbData.resize(refImageWidth * refImageHeight);
	const UINT srcImageSizeInBytes = refImageWidth * refImageHeight * 4;
	const UINT srcImageStride = refImageWidth * 4;
	if (!CheckCOMResult(spConverter->CopyPixels(NULL, srcImageStride, srcImageSizeInBytes, reinterpret_cast<BYTE*>(rgbData.data()))))
	{
		return false;
	}

	// Convert back to pallettized representation
	std::vector<uint32_t> palletizedImage;
	for (int i = 0; i < rgbData.size(); ++i)
	{
		uint32_t rgb = rgbData[i];
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
			assert(false);
		}
		palletizedImage.push_back(palletizedColor);
	}

	Tile tiles[256] = {};
	for (int i = 0; i < palletizedImage.size(); ++i)
	{
		int imageX = i % bitmapWidth;
		int imageY = i / bitmapWidth;
		int tileX = imageX / tileWidth;
		int tileY = imageY / tileHeight;
		int tileIndex = (tileY * tilesWide) + tileX;

		int xWithinTile = imageX % tileWidth;
		int yWithinTile = imageY % tileHeight;
		int indexWithinTile = (yWithinTile * tileWidth) + xWithinTile;
		
		tiles[tileIndex].Palletized[indexWithinTile] = palletizedImage[i];
	}

	for (int tileY = 0; tileY < tilesHigh; ++tileY)
	{
		for (int tileX = 0; tileX < tilesWide; ++tileX)
		{
			int tileIndex = (tileY * tilesWide) + tileX;
			Tile& tile = tiles[tileIndex];

			// Turn palletized into Data
			for (int y = 0; y < 8; ++y)
			{
				for (int x = 0; x < 8; ++x)
				{
					int pixelIndex = (y * 8) + x;
					uint32_t palletized = tiles[tileIndex].Palletized[pixelIndex];

					byte f0{};
					byte f1{};

					if (palletized == 0)
					{
						f0 = false;
						f1 = false;
					}
					else if (palletized == 1)
					{
						f0 = true;
					}
					else if (palletized == 2)
					{
						f1 = true;
					}
					else if (palletized == 3)
					{
						f0 = true;
						f1 = true;
					}

					int bitSelect = bitSelectReference[x];
					int firstByteIndex = 0 + y;
					int secondByteIndex = 8 + y;

					if (f0)
					{
						tile.Data[firstByteIndex] |= bitSelect;
					}

					if (f1)
					{
						tile.Data[secondByteIndex] |= bitSelect;
					}
				}
			}
		}
	}

	std::vector<byte> romData;
	{

		FILE* file = {};
		if (!CheckErrno(_wfopen_s(&file, romFilename.c_str(), L"rb")))
		{
			return false;
		}
		if (!CheckZero(fseek(file, 0, SEEK_END)))
		{
			return false;
		}

		long fileLength = ftell(file);
		if (fileLength == 0)
		{
			std::wcout << L"File " << sourceFilename.c_str() << L"is unexpectedly of length 0.\n";
			__debugbreak();
			return false;
		}

		romData.resize(fileLength);

		if (!CheckZero(fseek(file, 0, SEEK_SET)))
		{
			return false;
		}
		size_t readAmount = fread(romData.data(), 1, fileLength, file);
		if (readAmount != fileLength)
		{
			std::wcout << L"Encountered I/O error when reading a file.\n";
			__debugbreak();
			return false;
		}

		if (!CheckZero(fclose(file)))
		{
			return false;
		}
	}

	// Save tile data back into rom file
	for (int tileIndex = 0; tileIndex < 256; tileIndex++)
	{
		int romOffset = baseOffset + (tileIndex * 16);

		for (int i = 0; i < 16; ++i)
		{
			romData[romOffset + i] = tiles[tileIndex].Data[i];
		}
	}
	{
		FILE* file = {};
		if (!CheckErrno(_wfopen_s(&file, romFilename.c_str(), L"wb")))
		{
			return false;
		}
		size_t writeAmount = fwrite(romData.data(), 1, romData.size(), file);
		if (writeAmount != romData.size())
		{
			std::wcout << L"Encountered I/O error when writing a file.\n";
			__debugbreak();
			return false;
		}

		if (!CheckZero(fclose(file)))
		{
			return false;
		}
	}

	return true;
}

void PrintUsage()
{
	std::wcout
		<< L"Usage:\n"
		<< L"\tihsie export icehockey.nes image.png\n"
		<< L"or\n"
		<< L"\tihsie import image.png icehockey.nes\n";
}

int wmain(int argc, void** argv)
{
	if (argc < 4)
	{
		PrintUsage();
		return -1;
	}

	std::wstring op = reinterpret_cast<wchar_t*>(argv[1]);
	std::wstring filename1 = reinterpret_cast<wchar_t*>(argv[2]);
	std::wstring filename2 = reinterpret_cast<wchar_t*>(argv[3]);
	
	if (!CheckCOMResult(CoInitialize(nullptr)))
	{
		return -1;
	}

	if (!CheckCOMResult(CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&wicImagingFactory)))
	{
		return -1;
	}

	if (op == L"export")
	{
		if (!Export(filename1, filename2))
		{
			return -1;
		}
	}
	else if (op == L"import")
	{
		if (!Import(filename1, filename2))
		{
			return -1;
		}
	}
	else
	{
		PrintUsage();
		return -1;
	}
}

