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

void Export()
{
	// Load ROM file
	FILE* file = {};
	fopen_s(&file, "icehockey.nes", "rb");

	Tile tiles[256];

	for (int i = 0; i < 256; ++i)
	{
		long tileOffset = baseOffset + (i * 0x10);
		fseek(file, tileOffset, SEEK_SET);
		fread(tiles[i].Data, 1, 16, file);
	}

	fclose(file);

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
	wicImagingFactory->CreateStream(&stream);

	std::wstring destFilename = L"export.png";
	stream->InitializeFromFilename(destFilename.c_str(), GENERIC_WRITE);

	ComPtr<IWICBitmapEncoder> encoder;
	wicImagingFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder);

	encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

	ComPtr<IWICBitmap> wicBitmap;
	wicImagingFactory->CreateBitmap(
		bitmapWidth,
		bitmapHeight,
		GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wicBitmap);

	ComPtr<IWICBitmapFrameEncode> frameEncode;
	encoder->CreateNewFrame(&frameEncode, nullptr);

	frameEncode->Initialize(nullptr);

	frameEncode->SetSize(bitmapWidth, bitmapHeight);

	frameEncode->SetResolution(96, 96);

	WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
	frameEncode->SetPixelFormat(&pixelFormat);

	{
		ComPtr<IWICBitmapLock> bitmapLock;
		WICRect rcLock = { 0, 0, bitmapWidth, bitmapHeight };
		wicBitmap->Lock(&rcLock, WICBitmapLockWrite, &bitmapLock);

		UINT cbBufferSize = 0;
		BYTE* pv = NULL;

		bitmapLock->GetDataPointer(&cbBufferSize, &pv);

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

	frameEncode->WriteSource(
		wicBitmap.Get(),
		NULL);

	frameEncode->Commit();

	encoder->Commit();

	stream->Commit(STGC_DEFAULT);

}

void Import()
{
	std::wstring sourceFilename = L"export.png";

	ComPtr<IWICBitmapDecoder> spDecoder;
	wicImagingFactory->CreateDecoderFromFilename(sourceFilename.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &spDecoder);

	ComPtr<IWICBitmapFrameDecode> spSource;
	spDecoder->GetFrame(0, &spSource);

	ComPtr<IWICFormatConverter> spConverter;
	wicImagingFactory->CreateFormatConverter(&spConverter);

	spConverter->Initialize(
		spSource.Get(),
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		NULL,
		0.f,
		WICBitmapPaletteTypeMedianCut);

	uint32_t refImageWidth, refImageHeight;
	spConverter->GetSize(&refImageWidth, &refImageHeight);

	std::vector<uint32_t> rgbData;
	rgbData.resize(refImageWidth * refImageHeight);
	const UINT srcImageSizeInBytes = refImageWidth * refImageHeight * 4;
	const UINT srcImageStride = refImageWidth * 4;
	spConverter->CopyPixels(NULL, srcImageStride, srcImageSizeInBytes, reinterpret_cast<BYTE*>(rgbData.data()));

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

	FILE* file = {};
	fopen_s(&file, "icehockey.nes", "rb");
	fseek(file, 0, SEEK_END);
	long fileLength = ftell(file);
	fseek(file, 0, SEEK_SET);

	std::vector<byte> romData;
	romData.resize(fileLength);
	fread_s(romData.data(), romData.size(), 1, romData.size(), file);
	fclose(file);

	std::vector<byte> output;
	output.resize(fileLength);

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

	// Save tile data back into rom file
	for (int tileIndex = 0; tileIndex < 256; tileIndex++)
	{
		for (int i = 0; i < 16; ++i)
		{
			int romOffset = baseOffset + (tileIndex * 16) + i;
			output[romOffset] = tiles[tileIndex].Data[i];
		}
	}

	// Validate output
	for (int i = 0; i < output.size(); i++)
	{
		if (i < baseOffset)
		{
			assert(output[i] == 0);
		}
		else if (i < baseOffset + (256 * 16))
		{
			assert(output[i] == romData[i]);
		}
		else
		{
			assert(output[i] == 0);
		}
	}
}

int main()
{
	CoInitialize(nullptr);

	CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&wicImagingFactory);

	Export();
	Import();
}

