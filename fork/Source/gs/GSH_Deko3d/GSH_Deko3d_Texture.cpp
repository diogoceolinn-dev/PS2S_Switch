// Fase 2 (RoxasBR90): cache de texturas do backend deko3d.
// Conversores adaptados de GSH_OpenGL_Texture.cpp (caminho não-SIMD,
// portátil para ARM64), upload via CopyBufferToImage para DkImage RGBA8.

#include "GSH_Deko3d.h"
#include "../GsPixelFormats.h"

#include <algorithm>
#include <cstring>

#define TEX_ALIGN(x, align) (((x) + (align)-1) & ~((align)-1))

Deko3dTexture::~Deko3dTexture()
{
	// Chamada só após idle da fila (ver TexCache_CollectGarbage) ou no
	// encerramento (ReleaseImpl já dá WaitIdle). Seguro destruir aqui.
	if(stagingBlock != nullptr)
	{
		dkMemBlockDestroy(stagingBlock);
		stagingBlock = nullptr;
	}
	if(imageBlock != nullptr)
	{
		dkMemBlockDestroy(imageBlock);
		imageBlock = nullptr;
	}
	imageReady = false;
}

static bool IsPowerOfTwo(uint32_t v)
{
	return (v != 0) && ((v & (v - 1)) == 0);
}

static bool IsSupportedPsm(unsigned int psm)
{
	switch(psm)
	{
	case CGSHandler::PSMCT32:
	case CGSHandler::PSMCT24:
	case CGSHandler::PSMCT16:
	case CGSHandler::PSMCT16S:
	case CGSHandler::PSMT8:
	case CGSHandler::PSMT4:
		return true;
	default:
		return false;
	}
}

static bool IsPalettedPsm(unsigned int psm)
{
	return (psm == CGSHandler::PSMT8) || (psm == CGSHandler::PSMT4);
}

static uint32_t HashClut(const std::array<uint32_t, 256>& clut, unsigned int entryCount)
{
	// FNV-1a 32-bit
	uint32_t hash = 2166136261u;
	for(unsigned int i = 0; i < entryCount; i++)
	{
		uint32_t v = clut[i];
		hash ^= (v & 0xFF);
		hash *= 16777619u;
		hash ^= ((v >> 8) & 0xFF);
		hash *= 16777619u;
		hash ^= ((v >> 16) & 0xFF);
		hash *= 16777619u;
		hash ^= ((v >> 24) & 0xFF);
		hash *= 16777619u;
	}
	return hash;
}

static void ConvertPsm32(uint8* ram, uint32 bufPtr, uint32 bufWidth,
    uint32* dst, uint32 dstPitchPixels, uint32 w, uint32 h)
{
	CGsPixelFormats::CPixelIndexorPSMCT32 indexor(ram, bufPtr, bufWidth);
	for(uint32 y = 0; y < h; y++)
	{
		for(uint32 x = 0; x < w; x++)
		{
			dst[x] = indexor.GetPixel(x, y);
		}
		dst += dstPitchPixels;
	}
}

static void ConvertPsm16(uint8* ram, uint32 bufPtr, uint32 bufWidth, bool isSigned,
    uint32* dst, uint32 dstPitchPixels, uint32 w, uint32 h)
{
	if(isSigned)
	{
		CGsPixelFormats::CPixelIndexorPSMCT16S indexor(ram, bufPtr, bufWidth);
		for(uint32 y = 0; y < h; y++)
		{
			for(uint32 x = 0; x < w; x++)
			{
				uint16 pixel = indexor.GetPixel(x, y);
				uint32 r = (((pixel >> 0) & 0x1F) * 255 + 15) / 31;
				uint32 g = (((pixel >> 5) & 0x1F) * 255 + 15) / 31;
				uint32 b = (((pixel >> 10) & 0x1F) * 255 + 15) / 31;
				uint32 a = (pixel & 0x8000) ? 0xFF : 0x00;
				dst[x] = r | (g << 8) | (b << 16) | (a << 24);
			}
			dst += dstPitchPixels;
		}
	}
	else
	{
		CGsPixelFormats::CPixelIndexorPSMCT16 indexor(ram, bufPtr, bufWidth);
		for(uint32 y = 0; y < h; y++)
		{
			for(uint32 x = 0; x < w; x++)
			{
				uint16 pixel = indexor.GetPixel(x, y);
				uint32 r = (((pixel >> 0) & 0x1F) * 255 + 15) / 31;
				uint32 g = (((pixel >> 5) & 0x1F) * 255 + 15) / 31;
				uint32 b = (((pixel >> 10) & 0x1F) * 255 + 15) / 31;
				uint32 a = (pixel & 0x8000) ? 0xFF : 0x00;
				dst[x] = r | (g << 8) | (b << 16) | (a << 24);
			}
			dst += dstPitchPixels;
		}
	}
}

template <typename IndexorType>
static void ConvertPsmIndexed(IndexorType& indexor, const uint32* clut,
    uint32* dst, uint32 dstPitchPixels, uint32 w, uint32 h)
{
	for(uint32 y = 0; y < h; y++)
	{
		for(uint32 x = 0; x < w; x++)
		{
			dst[x] = clut[indexor.GetPixel(x, y)];
		}
		dst += dstPitchPixels;
	}
}

void CGSH_Deko3d::TexCache_Upload(const TEX0& tex0,
    const std::shared_ptr<Deko3dTexture>& texture, DkCmdBuf cmdbuf)
{
	uint32 w = texture->width;
	uint32 h = texture->height;
	uint32 bufPtr = tex0.GetBufPtr();
	uint32 bufWidth = tex0.nBufWidth;

	auto dst = reinterpret_cast<uint32*>(dkMemBlockGetCpuAddr(texture->stagingBlock));
	uint32 pitch = texture->pitchPixels;

	switch(tex0.nPsm)
	{
	case PSMCT32:
	case PSMCT24:
		ConvertPsm32(m_pRAM, bufPtr, bufWidth, dst, pitch, w, h);
		break;
	case PSMCT16:
		ConvertPsm16(m_pRAM, bufPtr, bufWidth, false, dst, pitch, w, h);
		break;
	case PSMCT16S:
		ConvertPsm16(m_pRAM, bufPtr, bufWidth, true, dst, pitch, w, h);
		break;
	case PSMT8:
	{
		std::array<uint32_t, 256> linearClut;
		MakeLinearCLUT(tex0, linearClut);
		CGsPixelFormats::CPixelIndexorPSMT8 indexor(m_pRAM, bufPtr, bufWidth);
		ConvertPsmIndexed(indexor, linearClut.data(), dst, pitch, w, h);
		break;
	}
	case PSMT4:
	{
		std::array<uint32_t, 256> linearClut;
		MakeLinearCLUT(tex0, linearClut);
		CGsPixelFormats::CPixelIndexorPSMT4 indexor(m_pRAM, bufPtr, bufWidth);
		ConvertPsmIndexed(indexor, linearClut.data(), dst, pitch, w, h);
		break;
	}
	default:
		// Não deve chegar aqui (IsSupportedPsm filtra antes).
		memset(dst, 0, static_cast<size_t>(pitch) * h * sizeof(uint32));
		break;
	}

	DkCopyBuf copy;
	copy.addr = dkMemBlockGetGpuAddr(texture->stagingBlock);
	copy.rowLength = pitch;
	copy.imageHeight = h;

	DkImageView view;
	dkImageViewDefaults(&view, &texture->image);

	DkImageRect rect;
	rect.x = 0;
	rect.y = 0;
	rect.z = 0;
	rect.width = w;
	rect.height = h;
	rect.depth = 1;

	dkCmdBufCopyBufferToImage(cmdbuf, &copy, &view, &rect, 0);
}

std::shared_ptr<Deko3dTexture> CGSH_Deko3d::TexCache_Prepare(
    const TEX0& tex0, const CLAMP& clamp, DkCmdBuf cmdbuf, DkDevice device)
{
	uint32 texWidth = std::min<uint32>(tex0.GetWidth(), TEX0_MAX_TEXTURE_SIZE);
	uint32 texHeight = std::min<uint32>(tex0.GetHeight(), TEX0_MAX_TEXTURE_SIZE);
	if((texWidth == 0) || (texHeight == 0)) return nullptr;
	if(!IsSupportedPsm(tex0.nPsm)) return nullptr;

	bool paletted = IsPalettedPsm(tex0.nPsm);
	uint32 clutHash = 0;
	if(paletted)
	{
		std::array<uint32_t, 256> linearClut;
		MakeLinearCLUT(tex0, linearClut);
		unsigned int entryCount = CGsPixelFormats::IsPsmIDTEX4(tex0.nPsm) ? 16 : 256;
		clutHash = HashClut(linearClut, entryCount);
	}

	auto cached = m_texCache.Search(tex0);
	if(cached != nullptr)
	{
		const auto& handle = cached->m_textureHandle;
		if((handle.fullTex0 == static_cast<uint64>(tex0)) &&
		   (!paletted || (handle.clutHash == clutHash)) &&
		   (handle.texture != nullptr) && handle.texture->imageReady)
		{
			return handle.texture;
		}
		// Entrada obsoleta (CLUT trocou): re-sobe por cima do LRU.
	}

	auto texture = std::make_shared<Deko3dTexture>();
	texture->width = texWidth;
	texture->height = texHeight;
	texture->pitchPixels = TEX_ALIGN(texWidth, 16);
	texture->useRepeat = (clamp.nWMS == 0) && (clamp.nWMT == 0) &&
	    IsPowerOfTwo(texWidth) && IsPowerOfTwo(texHeight);

	// Staging RGBA8 (CPU escreve, GPU lê no copy).
	uint32 stagingSize = TEX_ALIGN(texture->pitchPixels * texHeight * sizeof(uint32), 4096);
	{
		DkMemBlockMaker maker;
		dkMemBlockMakerDefaults(&maker, device, stagingSize);
		maker.flags = DkMemBlockFlags_CpuUncached;
		texture->stagingBlock = dkMemBlockCreate(&maker);
	}

	// Imagem RGBA8 (sample-only; sem flags extras).
	{
		DkImageLayoutMaker layoutMaker;
		dkImageLayoutMakerDefaults(&layoutMaker, device);
		layoutMaker.format = DkImageFormat_RGBA8_Unorm;
		layoutMaker.dimensions[0] = texWidth;
		layoutMaker.dimensions[1] = texHeight;
		DkImageLayout layout;
		dkImageLayoutInitialize(&layout, &layoutMaker);
		uint32 imageSize = TEX_ALIGN(dkImageLayoutGetSize(&layout),
		    dkImageLayoutGetAlignment(&layout));
		DkMemBlockMaker maker;
		dkMemBlockMakerDefaults(&maker, device, imageSize);
		maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
		texture->imageBlock = dkMemBlockCreate(&maker);
		dkImageInitialize(&texture->image, &layout, texture->imageBlock, 0);
		texture->imageReady = true;
	}

	TexCache_Upload(tex0, texture, cmdbuf);

	CDeko3dTextureHandle handle;
	handle.texture = texture;
	handle.fullTex0 = static_cast<uint64>(tex0);
	handle.clutHash = clutHash;
	m_texCache.Insert(tex0, std::move(handle));
	m_texRegistry.push_back(texture);

	return texture;
}

void CGSH_Deko3d::TexCache_Flush()
{
	m_texCache.Flush();
}

void CGSH_Deko3d::TexCache_CollectGarbage(DkQueue queue, bool force)
{
	if(!force && (m_texRegistry.size() <= 256)) return;
	// Espera a GPU esvaziar: só então entradas evictadas podem morrer.
	dkQueueWaitIdle(queue);
	m_texRegistry.erase(
	    std::remove_if(m_texRegistry.begin(), m_texRegistry.end(),
	        [](const std::shared_ptr<Deko3dTexture>& t) { return t.use_count() == 1; }),
	    m_texRegistry.end());
}
