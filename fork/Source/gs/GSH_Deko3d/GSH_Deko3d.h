#pragma once

#include <memory>
#include <vector>

#include "../GSHandler.h"
#include "../../gs/GsDebuggerInterface.h"
#include "../../gs/GsTextureCache.h"
#include "GSH_Deko3d_Texture.h"

class CGSH_Deko3d : public CGSHandler
{
public:
	CGSH_Deko3d() = default;
	virtual ~CGSH_Deko3d() = default;

	void ProcessHostToLocalTransfer() override;
	void ProcessLocalToHostTransfer() override;
	void ProcessLocalToLocalTransfer() override;
	void ProcessClutTransfer(uint32, uint32) override;

	static FactoryFunction GetFactoryFunction();

	// Fase 2 (RoxasBR90): cache de texturas. Implementados em
	// GSH_Deko3d_Texture.cpp (precisam de membros protegidos do CGSHandler).
	std::shared_ptr<Deko3dTexture> TexCache_Prepare(
	    const TEX0& tex0, const CLAMP& clamp, DkCmdBuf cmdbuf, DkDevice device);
	void TexCache_Upload(const TEX0& tex0,
	    const std::shared_ptr<Deko3dTexture>& texture, DkCmdBuf cmdbuf);
	void TexCache_Flush();
	void TexCache_CollectGarbage(DkQueue queue, bool force = false);

private:
	//These need to match the layout of the shader's uniform block
	struct VERTEXPARAMS
	{
		float projMatrix[16];
	};

	// Fase 2: um draw run = sequência de vértices com mesma textura/pipeline.
	// Texturizados e não-texturizados não se misturam no mesmo Draw.
	struct DrawRun
	{
		uint32_t firstVertex = 0;
		uint32_t vertexCount = 0;
		uint32_t primType = 0; // DkPrimitive_*
		bool textured = false;
		std::shared_ptr<Deko3dTexture> texture;
		bool repeatWrap = true;
	};

	void InitializeImpl() override;
	void ReleaseImpl() override;
	void MarkNewFrame() override;
	void FlipImpl(const DISPLAY_INFO&) override;
	void WriteRegisterImpl(uint8, uint64) override;

	void BeginFrame();
	void EndFrame();

	void VertexKick(uint8, uint64);
	void SetRenderingContext(uint64);
	void SetupDepthBuffer(uint64, uint64);
	void SetupTestFunctions(uint64);

	void Prim_Point();
	void Prim_Line();
	void Prim_Triangle();
	void Prim_Sprite();

	// Fase 2: registra vértice no run atual (abre novo run se a textura
	// mudou). Retorna false se o buffer de vértices estourou (primitiva
	// descartada com segurança).
	bool EmitVertex(float x, float y, float z, uint32 color,
	    float u, float v, float q, uint32 primType,
	    const std::shared_ptr<Deko3dTexture>& texture, bool repeatWrap);

	// Fase 2: resolve a textura do prim atual (nullptr = sem textura).
	std::shared_ptr<Deko3dTexture> ResolvePrimTexture();

	float CalcZ(float);

	static void MakeLinearZOrtho(float*, float, float, float, float);

	/* Draw context */
	VERTEX m_vtxBuffer[3];
	uint32 m_vtxCount = 0;
	uint32 m_primitiveType = 0;
	PRMODE m_primitiveMode;
	uint32 m_fbBasePtr = 0;
	float m_primOfsX = 0;
	float m_primOfsY = 0;
	uint32 m_texWidth = 0;
	uint32 m_texHeight = 0;
	float m_nMaxZ;

	// Fase 2: contexto de textura do prim atual (vindo de SetRenderingContext).
	TEX0 m_curTex0;
	CLAMP m_curClamp;
	bool m_haveTexContext = false;

	// Fase 2: runs do frame em gravação + cache de texturas.
	std::vector<DrawRun> m_runs;
	CGsTextureCache<CDeko3dTextureHandle> m_texCache;
	std::vector<std::shared_ptr<Deko3dTexture>> m_texRegistry;
	uint32 m_texSlotCursor = 0;
	uint32 m_unsupportedTexWarns = 0;

	VERTEXPARAMS m_vertexParams;
};
