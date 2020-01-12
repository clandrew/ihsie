// ihsie.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Util.h"

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

class ImageEncodeHelper
{
	ComPtr<IWICBitmapFrameEncode> m_frameEncode;
	ComPtr<IWICBitmap> m_wicBitmap;
	ComPtr<IWICBitmapEncoder> m_encoder;
	ComPtr<IWICStream> m_stream;
	BYTE* m_dataPointer;

public:
	bool Initialize(std::wstring destFilename)
	{
		if (!CheckCOMResult(g_wicImagingFactory->CreateStream(&m_stream)))
		{
			return false;
		}

		if (!CheckCOMResult(m_stream->InitializeFromFilename(destFilename.c_str(), GENERIC_WRITE)))
		{
			return false;
		}

		if (!CheckCOMResult(g_wicImagingFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &m_encoder)))
		{
			return false;
		}

		if (!CheckCOMResult(m_encoder->Initialize(m_stream.Get(), WICBitmapEncoderNoCache)))
		{
			return false;
		}

		if (!CheckCOMResult(g_wicImagingFactory->CreateBitmap(
			g_exportedImageWidthInPixels,
			g_exportedImageHeightInPixels,
			GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &m_wicBitmap)))
		{
			return false;
		}

		if (!CheckCOMResult(m_encoder->CreateNewFrame(&m_frameEncode, nullptr)))
		{
			return false;
		}

		if (!CheckCOMResult(m_frameEncode->Initialize(nullptr)))
		{
			return false;
		}

		if (!CheckCOMResult(m_frameEncode->SetSize(g_exportedImageWidthInPixels, g_exportedImageHeightInPixels)))
		{
			return false;
		}

		if (!CheckCOMResult(m_frameEncode->SetResolution(96, 96)))
		{
			return false;
		}

		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
		if (!CheckCOMResult(m_frameEncode->SetPixelFormat(&pixelFormat)))
		{
			return false;
		}

		ComPtr<IWICBitmapLock> bitmapLock;
		WICRect rcLock = { 0, 0, g_exportedImageWidthInPixels, g_exportedImageHeightInPixels };
		if (!CheckCOMResult(m_wicBitmap->Lock(&rcLock, WICBitmapLockWrite, &bitmapLock)))
		{
			return false;
		}

		UINT cbBufferSize = 0;

		if (!CheckCOMResult(bitmapLock->GetDataPointer(&cbBufferSize, &m_dataPointer)))
		{
			return false;
		}

		return true;
	}

	bool Finalize()
	{
		if (!CheckCOMResult(m_frameEncode->WriteSource(
			m_wicBitmap.Get(),
			NULL)))
		{
			return false;
		}

		if (!CheckCOMResult(m_frameEncode->Commit()))
		{
			return false;
		}

		if (!CheckCOMResult(m_encoder->Commit()))
		{
			return false;
		}

		if (!CheckCOMResult(m_stream->Commit(STGC_DEFAULT)))
		{
			return false;
		}

		return true;
	}

	BYTE* GetDataPointer()
	{
		return m_dataPointer;
	}
};

bool ExportPalletizedTilesToImage(TileGrid const& tiles, std::wstring destFilename)
{
	ImageEncodeHelper e;

	if (!e.Initialize(destFilename))
	{
		return false;
	}

	BYTE* pv = e.GetDataPointer();
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
					uint32_t rgba = PalletizedToRgba(palletizedColor);

					int destX = destXOrigin + x;
					int destY = destYOrigin + y;
					int destPixelIndex = (destY * g_exportedImageWidthInPixels) + destX;
					pixelData[destPixelIndex] = rgba;
				}
			}
		}
	}

	if (!e.Finalize())
	{
		return false;
	}

	return true;
}

enum BitmapCopyMode
{
	Default,
	HFlip
};

void DrawTileToLockedBitmap(uint32_t* pixelData, TileGrid const& tiles, int tileIndex, int destXOrigin, int destYOrigin, BitmapCopyMode mode=Default)
{
	if (mode == Default)
	{
		for (int y = 0; y < 8; ++y)
		{
			for (int x = 0; x < 8; ++x)
			{
				int palletized = tiles.GetPalletizedValue(tileIndex, x, y);
				uint32_t rgb = PalletizedToRgba(palletized);

				int destX = destXOrigin + x;
				int destY = destYOrigin + y;
				int destPixelIndex = (destY * g_exportedImageWidthInPixels) + destX;
				pixelData[destPixelIndex] = rgb;
			}
		}
	}
	else
	{
		assert(mode == HFlip);

		for (int y = 0; y < 8; ++y)
		{
			for (int x = 0; x < 8; ++x)
			{
				int palletized = tiles.GetPalletizedValue(tileIndex, 8-x-1, y);
				uint32_t rgb = PalletizedToRgba(palletized);

				int destX = destXOrigin + x;
				int destY = destYOrigin + y;
				int destPixelIndex = (destY * g_exportedImageWidthInPixels) + destX;
				pixelData[destPixelIndex] = rgb;
			}
		}
	}
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

	{
		ImageEncodeHelper e;
		if (!e.Initialize(L"reformatted.png"))
		{
			return false;
		}

		BYTE* pv = e.GetDataPointer();
		uint32_t* pixelData = reinterpret_cast<uint32_t*>(pv);

		// Do a clear
		memset(pixelData, 0, g_exportedImageWidthInPixels * g_exportedImageHeightInPixels * sizeof(uint32_t));

		// Medium /////////////////////////////////////////////////
		int x = 0;
		int y = 0;
		DrawTileToLockedBitmap(pixelData, tiles, 0x4, x, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x5, x+8, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x6, x, y+8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x7, x+8, y+8);

		x = 0;
		y = 17;
		DrawTileToLockedBitmap(pixelData, tiles, 0xE5, x, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x01, x + 8, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x02, x, y + 8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x03, x + 8, y + 8);

		x = 0;
		y = 34;
		DrawTileToLockedBitmap(pixelData, tiles, 0x22, x, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x23, x + 8, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x24, x, y + 8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x25, x + 8, y + 8);

		// Fat /////////////////////////////////////////////////
		x = 17;
		y = 0;
		DrawTileToLockedBitmap(pixelData, tiles, 0x44, x, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x45, x+8, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x46, x, y+8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x47, x + 8, y+8);

		x = 17;
		y = 34;
		DrawTileToLockedBitmap(pixelData, tiles, 0x62, x, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x63, x + 8, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x64, x, y + 8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x65, x + 8, y + 8);

		// Thin /////////////////////////////////////////////////
		x = 34;
		y = 0;
		DrawTileToLockedBitmap(pixelData, tiles, 0x80, x+5, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0x85, x, y+8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x86, x+8, y + 8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x87, x, y+16);
		DrawTileToLockedBitmap(pixelData, tiles, 0x88, x + 8, y + 16);

		x = 34;
		y = 34;
		DrawTileToLockedBitmap(pixelData, tiles, 0x80, x+1, y); // redundant
		DrawTileToLockedBitmap(pixelData, tiles, 0xA6, x, y + 8);
		DrawTileToLockedBitmap(pixelData, tiles, 0x8C, x + 8, y + 8);
		DrawTileToLockedBitmap(pixelData, tiles, 0xA7, x, y + 16);
		DrawTileToLockedBitmap(pixelData, tiles, 0x8E, x + 8, y + 16);

		// Referee /////////////////////////////////////////////////
		x = 0;
		y = 100;
		DrawTileToLockedBitmap(pixelData, tiles, 0xE7, x, y, HFlip);
		DrawTileToLockedBitmap(pixelData, tiles, 0xE7, x+8, y);
		DrawTileToLockedBitmap(pixelData, tiles, 0xE9, x, y+8, HFlip);
		DrawTileToLockedBitmap(pixelData, tiles, 0xE9, x + 8, y + 8);

		// Puck
		x = 0;
		y = 119;
		DrawTileToLockedBitmap(pixelData, tiles, 0xFE, x, y);

		e.Finalize();
	}

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
		uint32_t palletizedColor = RgbToPalletized(rgb);

		if (palletizedColor == -1)
		{
			std::wcout << L"Encountered an unexpected color, 0x" << std::hex << rgb << L", in the image to be imported.";
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

