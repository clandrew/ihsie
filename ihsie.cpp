// ihsie.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

// Constants
static const int g_tileWidthInPixels = 8;
static const int g_tileXCount = 16;
static const int g_tileHeightInPixels = 8;
static const int g_tileYCount = 16;
static const int g_exportedImageWidthInPixels = g_tileWidthInPixels * g_tileXCount;
static const int g_exportedImageHeightInPixels = g_tileHeightInPixels * g_tileYCount;
static const int g_bitSelectionReference[] = { 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1 };
static const long g_imageDataOffset = 0x8010;

// Globals
ComPtr<IWICImagingFactory> g_wicImagingFactory;

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

struct RawTileData
{
	byte Data[16];
};

struct Tile
{
	RawTileData RawData;
	int Palletized[8 * 8];
};

class TileGrid
{
	int m_tileXCount;
	int m_tileYCount;
	std::vector<Tile> m_tiles;

public:
	TileGrid()
	{
		m_tileXCount = 16;
		m_tileYCount = 16;
		m_tiles.resize(m_tileXCount * m_tileYCount);
	}

	void SetPalletizedValue(int tileIndex, int xPosition, int yPosition, int value)
	{
		int pixelIndex = (yPosition * 8) + xPosition;
		m_tiles[tileIndex].Palletized[pixelIndex] = value;
	}

	int GetPalletizedValue(int tileIndex, int xPosition, int yPosition) const
	{
		int pixelIndex = (yPosition * 8) + xPosition;
		return m_tiles[tileIndex].Palletized[pixelIndex];
	}

	void SetRawDataField(int tileIndex, int byteIndex, int bitSelection)
	{
		m_tiles[tileIndex].RawData.Data[byteIndex] |= bitSelection;
	}

	byte GetRawDataField(int tileIndex, int byteIndex) const
	{
		return m_tiles[tileIndex].RawData.Data[byteIndex];
	}

	Tile* GetTile(int tileIndex)
	{
		return &m_tiles[tileIndex];
	}

	void CopyPalletizedValue(TileGrid& from, int fromIndex, int toIndex)
	{
		memcpy(m_tiles[toIndex].Palletized, from.m_tiles[fromIndex].Palletized, sizeof(int) * 64);
	}
};

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

int Decode(RawTileData const& rawTileData, int firstByteIndex, int secondByteIndex, int bitSelect)
{
	byte b0 = rawTileData.Data[firstByteIndex];
	byte b1 = rawTileData.Data[secondByteIndex];

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

bool ExportPalletizedTilesToImage(TileGrid const& tiles, std::wstring destFilename)
{
	ComPtr<IWICStream> stream;
	if (!CheckCOMResult(g_wicImagingFactory->CreateStream(&stream)))
	{
		return false;
	}

	if (!CheckCOMResult(stream->InitializeFromFilename(destFilename.c_str(), GENERIC_WRITE)))
	{
		return false;
	}

	ComPtr<IWICBitmapEncoder> encoder;
	if (!CheckCOMResult(g_wicImagingFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder)))
	{
		return false;
	}

	if (!CheckCOMResult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
	{
		return false;
	}

	ComPtr<IWICBitmap> wicBitmap;
	if (!CheckCOMResult(g_wicImagingFactory->CreateBitmap(
		g_exportedImageWidthInPixels,
		g_exportedImageHeightInPixels,
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

	if (!CheckCOMResult(frameEncode->SetSize(g_exportedImageWidthInPixels, g_exportedImageHeightInPixels)))
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
		WICRect rcLock = { 0, 0, g_exportedImageWidthInPixels, g_exportedImageHeightInPixels };
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

		for (int tileY = 0; tileY < g_tileYCount; ++tileY)
		{
			int destYOrigin = tileY * g_tileHeightInPixels;

			for (int tileX = 0; tileX < g_tileXCount; ++tileX)
			{
				int destXOrigin = tileX * g_tileWidthInPixels;

				int tileIndex = (tileY * g_tileXCount) + tileX;

				for (int y = 0; y < 8; ++y)
				{
					for (int x = 0; x < 8; ++x)
					{
						int palletizedColor = tiles.GetPalletizedValue(tileIndex, x, y);

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
						int destPixelIndex = (destY * g_exportedImageWidthInPixels) + destX;
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

bool Export(std::wstring romFilename, std::wstring imageFilename)
{
	// Load ROM file
	FILE* file = {};	
	if (!CheckErrno(_wfopen_s(&file, romFilename.c_str(), L"rb")))
	{
		return false;
	}

	TileGrid tiles;

	for (int i = 0; i < 256; ++i)
	{
		long tileOffset = g_imageDataOffset + (i * 0x10);
		if (!CheckZero(fseek(file, tileOffset, SEEK_SET)))
		{
			return false;
		}
		size_t readCount = fread(tiles.GetTile(i)->RawData.Data, 1, 16, file);
		if (readCount != 16)
		{
			std::wcout << L"Encountered I/O error when reading a file.\n";
			DebugEvent();
			return false;
		}
	}

	if (fclose(file) != 0)
	{
		std::wcout << L"Encountered I/O error when closing a file.\n";
		DebugEvent();
		return false;
	}

	for (int i = 0; i < 256; ++i)
	{
		for (int y = 0; y < 8; ++y)
		{
			for (int x = 0; x < 8; ++x)
			{
				int decodedValue = Decode(tiles.GetTile(i)->RawData, 0 + y, 8 + y, g_bitSelectionReference[x]);
				tiles.SetPalletizedValue(i, x, y, decodedValue);
			}
		}
	}

	ExportPalletizedTilesToImage(tiles, imageFilename);

	TileGrid reformattedTiles;

	reformattedTiles.CopyPalletizedValue(tiles, 0x4, 0x0);
	reformattedTiles.CopyPalletizedValue(tiles, 0x5, 0x1);
	reformattedTiles.CopyPalletizedValue(tiles, 0x6, 0x10);
	reformattedTiles.CopyPalletizedValue(tiles, 0x7, 0x11);


	reformattedTiles.CopyPalletizedValue(tiles, 0x44, 0x2);
	reformattedTiles.CopyPalletizedValue(tiles, 0x45, 0x3);
	reformattedTiles.CopyPalletizedValue(tiles, 0x46, 0x12);
	reformattedTiles.CopyPalletizedValue(tiles, 0x47, 0x13);
	

	reformattedTiles.CopyPalletizedValue(tiles, 0x80, 0x05);
	reformattedTiles.CopyPalletizedValue(tiles, 0x85, 0x14);
	reformattedTiles.CopyPalletizedValue(tiles, 0x86, 0x15);
	reformattedTiles.CopyPalletizedValue(tiles, 0x87, 0x24);
	reformattedTiles.CopyPalletizedValue(tiles, 0x88, 0x25);

	ExportPalletizedTilesToImage(reformattedTiles, L"reformatted2.png");

	return true;
}

bool Import(std::wstring sourceFilename, std::wstring romFilename)
{
	ComPtr<IWICBitmapDecoder> spDecoder;
	if (!CheckCOMResult(
		g_wicImagingFactory->CreateDecoderFromFilename(sourceFilename.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &spDecoder)))
	{
		return false;
	}

	ComPtr<IWICBitmapFrameDecode> spSource;
	if (!CheckCOMResult(spDecoder->GetFrame(0, &spSource)))
	{
		return false;
	}

	ComPtr<IWICFormatConverter> spConverter;
	if (!CheckCOMResult(g_wicImagingFactory->CreateFormatConverter(&spConverter)))
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
	for (size_t i = 0; i < rgbData.size(); ++i)
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
			std::wcout << L"Encountered an unexpected color in the image to be imported.";
			DebugEvent();
			return false;
		}
		palletizedImage.push_back(palletizedColor);
	}

	TileGrid tiles;
	for (size_t i = 0; i < palletizedImage.size(); ++i)
	{
		int imageX = i % g_exportedImageWidthInPixels;
		int imageY = i / g_exportedImageWidthInPixels;
		int tileX = imageX / g_tileWidthInPixels;
		int tileY = imageY / g_tileHeightInPixels;
		int tileIndex = (tileY * g_tileXCount) + tileX;

		int xWithinTile = imageX % g_tileWidthInPixels;
		int yWithinTile = imageY % g_tileHeightInPixels;
		
		tiles.SetPalletizedValue(tileIndex, xWithinTile, yWithinTile, palletizedImage[i]);
	}

	for (int tileY = 0; tileY < g_tileYCount; ++tileY)
	{
		for (int tileX = 0; tileX < g_tileXCount; ++tileX)
		{
			int tileIndex = (tileY * g_tileXCount) + tileX;

			// Turn palletized into RawData
			for (int y = 0; y < 8; ++y)
			{
				for (int x = 0; x < 8; ++x)
				{
					int palletized = tiles.GetPalletizedValue(tileIndex, x, y);

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

					int bitSelect = g_bitSelectionReference[x];
					int firstByteIndex = 0 + y;
					int secondByteIndex = 8 + y;

					if (f0)
					{
						tiles.SetRawDataField(tileIndex, firstByteIndex, bitSelect);
					}

					if (f1)
					{
						tiles.SetRawDataField(tileIndex, secondByteIndex, bitSelect);
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
			DebugEvent();
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
			DebugEvent();
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
		int romOffset = g_imageDataOffset + (tileIndex * 16);

		for (int i = 0; i < 16; ++i)
		{
			romData[romOffset + i] = tiles.GetRawDataField(tileIndex, i);
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
			DebugEvent();
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
		(LPVOID*)&g_wicImagingFactory)))
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

	std::wcout << L"Success.\n";
	return 0;
}

