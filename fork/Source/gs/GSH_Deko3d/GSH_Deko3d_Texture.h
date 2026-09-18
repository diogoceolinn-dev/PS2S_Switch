#pragma once

// Fase 2 (RoxasBR90): cache de texturas do backend deko3d.
//
// O armazenamento usa CGsTextureCache (LRU + chave TEX0, igual ao backend
// OpenGL). A conversão GS->RGBA8 e o upload vivem em métodos de CGSH_Deko3d
// (GSH_Deko3d_Texture.cpp) porque precisam de membros protegidos do
// CGSHandler (m_pRAM, MakeLinearCLUT).
//
// Paletizadas (PSMT8/PSMT4) são resolvidas na CPU com a CLUT linearizada —
// simples e correto; o shader faz só modulate. Ver docs/FASE2.md (limites v1).

#include <cstdint>
#include <memory>

#include <switch.h>
#include <deko3d.h>

struct Deko3dTexture
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t pitchPixels = 0; // pixels por linha no staging (alinhado a 16)
	bool useRepeat = true;    // false = clamp (ver CLAMP / texturas NPOT)
	DkMemBlock stagingBlock = nullptr; // CpuUncached, RGBA8
	DkMemBlock imageBlock = nullptr;   // GpuCached | Image
	DkImage image;
	bool imageReady = false;
	~Deko3dTexture();
};

struct CDeko3dTextureHandle
{
	std::shared_ptr<Deko3dTexture> texture;
	uint64_t fullTex0 = 0; // TEX0 completo (inclui bits de info CLUT)
	uint32_t clutHash = 0; // hash da CLUT linearizada (só paletizadas)
};
