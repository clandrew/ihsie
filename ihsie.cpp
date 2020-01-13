// ihsie.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Util.h"

// Constants
static const int g_tileWidthInPixels = 8;
static const int g_tileHeightInPixels = 8;
static const int g_romDataBytesPerTile = 16;
static const int g_exportedImageTileXCount = 16;
static const int g_bitSelectionReference[] = { 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1 };

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
	TileGrid(int tileXCount, int tileYCount)
		: m_tileXCount(tileXCount)
		, m_tileYCount(tileYCount)
	{
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
	bool Initialize(std::wstring destFilename, int imageWidth, int imageHeight)
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
			imageWidth,
			imageHeight,
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

		if (!CheckCOMResult(m_frameEncode->SetSize(imageWidth, imageHeight)))
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
		WICRect rcLock = { 0, 0, imageWidth, imageHeight };
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

bool ExportPalletizedTilesToImage(
	TileGrid const& tiles, 
	std::wstring destFilename, 
	int exportedImageTileXCount,
	int exportedImageTileYCount)
{
	ImageEncodeHelper e;
	
	int exportedImageWidthInPixels = exportedImageTileXCount * g_tileWidthInPixels;
	int exportedImageHeightInPixels = exportedImageTileYCount * g_tileHeightInPixels;

	if (!e.Initialize(destFilename, exportedImageWidthInPixels, exportedImageHeightInPixels))
	{
		return false;
	}

	BYTE* pv = e.GetDataPointer();
	uint32_t* pixelData = reinterpret_cast<uint32_t*>(pv);

	for (int tileY = 0; tileY < exportedImageTileYCount; ++tileY)
	{
		int destYOrigin = tileY * g_tileHeightInPixels;

		for (int tileX = 0; tileX < exportedImageTileXCount; ++tileX)
		{
			int destXOrigin = tileX * g_tileWidthInPixels;

			int tileIndex = (tileY * exportedImageTileXCount) + tileX;

			for (int y = 0; y < 8; ++y)
			{
				for (int x = 0; x < 8; ++x)
				{
					int palletizedColor = tiles.GetPalletizedValue(tileIndex, x, y);
					uint32_t rgba = PalletizedToRgba(palletizedColor);

					int destX = destXOrigin + x;
					int destY = destYOrigin + y;
					int destPixelIndex = (destY * exportedImageWidthInPixels) + destX;
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

bool Export(std::wstring romFilename, std::wstring imageFilename, int imageDataRomOffset, int exportedByteLength)
{
	// Load ROM file
	FILE* file = {};	
	if (!CheckErrno(_wfopen_s(&file, romFilename.c_str(), L"rb")))
	{
		return false;
	}

	int exportedTileCount = exportedByteLength / g_romDataBytesPerTile;
	int exportedImageTileYCount = exportedTileCount / g_exportedImageTileXCount;

	TileGrid tiles(g_exportedImageTileXCount, exportedImageTileYCount);
	for (int i = 0; i < exportedTileCount; ++i)
	{
		long tileOffset = imageDataRomOffset + (i * 0x10);
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

	for (int tileIndex = 0; tileIndex < exportedTileCount; ++tileIndex)
	{
		for (int y = 0; y < 8; ++y)
		{
			for (int x = 0; x < 8; ++x)
			{
				int decodedValue = Decode(tiles.GetTile(tileIndex)->RawData, 0 + y, 8 + y, g_bitSelectionReference[x]);
				tiles.SetPalletizedValue(tileIndex, x, y, decodedValue);
			}
		}
	}

	ExportPalletizedTilesToImage(
		tiles, 
		imageFilename, 
		g_exportedImageTileXCount,
		exportedImageTileYCount);

	return true;
}

bool Import(std::wstring sourceFilename, std::wstring romFilename, int imageDataRomOffset)
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

	if (refImageWidth % g_tileWidthInPixels != 0)
	{
		std::wcout << L"Image to import must have a width divisible by " << g_tileWidthInPixels << L".\n";
		return false;
	}

	if (refImageHeight % g_tileHeightInPixels != 0)
	{
		std::wcout << L"Image to import must have a height divisible by " << g_tileHeightInPixels << L".\n";
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

	int inputImageTileXCount = refImageWidth / g_tileWidthInPixels;
	int inputImageTileYCount = refImageHeight / g_tileHeightInPixels;
	int inputImageTileCount = inputImageTileXCount * inputImageTileYCount;
	
	TileGrid tiles(inputImageTileXCount, inputImageTileYCount);
	for (size_t i = 0; i < palletizedImage.size(); ++i)
	{
		int imageX = i % refImageWidth;
		int imageY = i / refImageWidth;
		int tileX = imageX / g_tileWidthInPixels;
		int tileY = imageY / g_tileHeightInPixels;
		int tileIndex = (tileY * inputImageTileXCount) + tileX;

		int xWithinTile = imageX % g_tileWidthInPixels;
		int yWithinTile = imageY % g_tileHeightInPixels;
		
		tiles.SetPalletizedValue(tileIndex, xWithinTile, yWithinTile, palletizedImage[i]);
	}

	for (int tileY = 0; tileY < inputImageTileYCount; ++tileY)
	{
		for (int tileX = 0; tileX < inputImageTileXCount; ++tileX)
		{
			int tileIndex = (tileY * inputImageTileXCount) + tileX;

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

	// Save tile data back into rom file
	for (int tileIndex = 0; tileIndex < inputImageTileCount; tileIndex++)
	{
		int romOffset = imageDataRomOffset + (tileIndex * g_romDataBytesPerTile);

		for (int byteIndex = 0; byteIndex < g_romDataBytesPerTile; ++byteIndex)
		{
			romData[romOffset + byteIndex] = tiles.GetRawDataField(tileIndex, byteIndex);
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
		<< L"\tihsie export icehockey.nes image.png 0x8010 4096\n"
		<< L"or\n"
		<< L"\tihsie import image.png icehockey.nes 0x8010\n";
}

int wmain(int argc, void** argv)
{
	if (argc < 5)
	{
		PrintUsage();
		return -1;
	}

	std::wstring op = reinterpret_cast<wchar_t*>(argv[1]);
	std::wstring filename1 = reinterpret_cast<wchar_t*>(argv[2]);
	std::wstring filename2 = reinterpret_cast<wchar_t*>(argv[3]);

	std::wstring imageDataOffsetStr = reinterpret_cast<wchar_t*>(argv[4]);
	std::wistringstream imageDataOffsetStrStrm(imageDataOffsetStr);
	int romImageDataOffset;
	imageDataOffsetStrStrm >> std::hex >> romImageDataOffset;

	if (romImageDataOffset <= 0)
	{
		std::wcout << L"Invalid rom offset of 0x" << std::hex << romImageDataOffset << L" specified.\n";
		DebugEvent();
		return -1;
	}
	
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
		if (argc < 6)
		{
			PrintUsage();
			return -1;
		}

		std::wstring byteLengthStr = reinterpret_cast<wchar_t*>(argv[5]);
		std::wistringstream byteLengthStrStrm(byteLengthStr);
		int byteLength{};
		byteLengthStrStrm >> byteLength;

		if (byteLength <= 0)
		{
			std::wcout << L"Invalid byte length of " << byteLength << L" specified.\n";
			DebugEvent();
			return -1;
		}

		if (!Export(filename1, filename2, romImageDataOffset, byteLength))
		{
			return -1;
		}
	}
	else if (op == L"import")
	{
		if (!Import(filename1, filename2, romImageDataOffset))
		{
			return -1;
		}
	}
	else
	{
		std::wcout << L"Invalid usage mode specified.\n";
		PrintUsage();
		return -1;
	}

	std::wcout << L"Success.\n";
	return 0;
}

