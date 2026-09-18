// Fase 2 (RoxasBR90): backend deko3d com texturing.
// Base: Source/gs/GSH_Deko3d/GSH_Deko3d.cpp do xerpi/play-switch @ d9c9e42e.
// Mudanças marcadas com "Fase 2:". Original preservado onde não indicado.

#include "GSH_Deko3d.h"
#include "../../gs/GsPixelFormats.h"

#include <climits>
#include <cstring>

#include <switch.h>
#include <deko3d.h>

#define ALIGN(x, align) (((x) + (align)-1) & ~((align)-1))

#define PS2_FRAMEBUFFER_HEIGHT 1024

extern "C" char _binary_triangle_vsh_dksh_start, _binary_triangle_vsh_dksh_end;
extern "C" char _binary_color_fsh_dksh_start, _binary_color_fsh_dksh_end;
// Fase 2: pipeline texturizado (modulate).
extern "C" char _binary_texture_fsh_dksh_start, _binary_texture_fsh_dksh_end;

struct Vertex
{
	// Fase 2: uvq carrega (u, v, q). q=1 no modo UV; no modo ST o shader
	// divide uv por q (correção perspectiva).
	float x, y, z;
	uint32_t color;
	float u, v, q, pad;
};

// Define the desired number of framebuffers
#define FB_NUM 2

// Define the desired framebuffer resolution (here we set it to 720p).
#define FB_WIDTH 1280
#define FB_HEIGHT 720

// Remove above and uncomment below for 1080p
//#define FB_WIDTH  1920
//#define FB_HEIGHT 1080

// Define the size of the memory block that will hold code
#define CODEMEMSIZE (4 * 1024 * 1024)

// Define the size of the memory block that will hold command lists
#define CMDMEMSIZE (4 * 1024 * 1024)

#define VERTEX_BUFFER_ENTRIES USHRT_MAX

// Fase 2: slots de descritores de imagem (1 slot por draw run texturizado).
#define TEX_IMG_SLOTS 4096
#define TEX_SAMP_REPEAT 0
#define TEX_SAMP_CLAMP 1

static DkDevice g_device;
static DkMemBlock g_framebufferMemBlock;
static DkImage g_framebuffers[FB_NUM];
static DkSwapchain g_swapchain;

static DkMemBlock g_depthMemBlock;
static DkImage g_depthbuffer;

static DkMemBlock g_codeMemBlock;
static uint32_t g_codeMemOffset;
static DkShader g_vertexShader;
static DkShader g_fragmentShader;
// Fase 2: fragment shader texturizado.
static DkShader g_fragmentTexShader;

static DkMemBlock g_bindFbCmdbufMemBlock;
static DkCmdBuf g_bindFbCmdbuf;
static DkCmdList g_cmdsBindFramebuffer[FB_NUM];

static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsRender;

static DkMemBlock g_vtxBufferMemBlock;
static uint32_t g_vtxBufferHead;

// Fase 2: descritores (memória CpuUncached escrita pela CPU, lida pela GPU).
static DkMemBlock g_imgDescBlock;
static DkMemBlock g_sampDescBlock;

static DkMemBlock g_unifMemBlock;

static DkQueue g_renderQueue;

static inline uint32 MakeColor(uint8 r, uint8 g, uint8 b, uint8 a)
{
	return (a << 24) | (b << 16) | (g << 8) | (r);
}

static void dk_debug_callback(void* userData, const char* context, DkResult result, const char* message)
{
	char description[256];

	if(result == DkResult_Success)
	{
		fprintf(stderr, "deko3d debug callback: context: %s, message: %s, result %d",
		        context, message, result);
	}
	else
	{
		snprintf(description, sizeof(description), "context: %s, message: %s, result %d",
		         context, message, result);
		fprintf(stderr, "deko3d fatal error: %s\n", description);
	}
}

static void loadShaderMemory(DkShader* pShader, const void* addr, size_t size)
{
	uint32_t codeOffset = g_codeMemOffset;
	g_codeMemOffset += ALIGN(size, DK_SHADER_CODE_ALIGNMENT);

	memcpy((uint8_t*)dkMemBlockGetCpuAddr(g_codeMemBlock) + codeOffset, addr, size);

	DkShaderMaker shaderMaker;
	dkShaderMakerDefaults(&shaderMaker, g_codeMemBlock, codeOffset);
	dkShaderInitialize(pShader, &shaderMaker);
}

void CGSH_Deko3d::InitializeImpl()
{
	// Create the device, which is the root object
	DkDeviceMaker deviceMaker;
	dkDeviceMakerDefaults(&deviceMaker);
	deviceMaker.userData = NULL;
	deviceMaker.cbDebug = dk_debug_callback;
	g_device = dkDeviceCreate(&deviceMaker);

	// Calculate layout for the framebuffers
	DkImageLayoutMaker imageLayoutMaker;
	dkImageLayoutMakerDefaults(&imageLayoutMaker, g_device);
	imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
	imageLayoutMaker.format = DkImageFormat_RGBA8_Unorm;
	imageLayoutMaker.dimensions[0] = FB_WIDTH;
	imageLayoutMaker.dimensions[1] = FB_HEIGHT;

	// Calculate layout for the framebuffers
	DkImageLayout framebufferLayout;
	dkImageLayoutInitialize(&framebufferLayout, &imageLayoutMaker);

	// Retrieve necessary size and alignment for the framebuffers
	uint32_t framebufferSize = dkImageLayoutGetSize(&framebufferLayout);
	uint32_t framebufferAlign = dkImageLayoutGetAlignment(&framebufferLayout);
	framebufferSize = ALIGN(framebufferSize, framebufferAlign);

	// Create a memory block that will host the framebuffers
	DkMemBlockMaker memBlockMaker;
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, FB_NUM * framebufferSize);
	memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_framebufferMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Initialize the framebuffers with the layout and backing memory we've just created
	DkImage const* swapchainImages[FB_NUM];
	for(unsigned i = 0; i < FB_NUM; i++)
	{
		swapchainImages[i] = &g_framebuffers[i];
		dkImageInitialize(&g_framebuffers[i], &framebufferLayout, g_framebufferMemBlock, i * framebufferSize);
	}

	/* Depth buffer */
	dkImageLayoutMakerDefaults(&imageLayoutMaker, g_device);
	imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
	imageLayoutMaker.format = DkImageFormat_Z24S8;
	imageLayoutMaker.dimensions[0] = FB_WIDTH;
	imageLayoutMaker.dimensions[1] = FB_HEIGHT;

	DkImageLayout depthLayout;
	dkImageLayoutInitialize(&depthLayout, &imageLayoutMaker);

	uint32_t depthSize = dkImageLayoutGetSize(&depthLayout);
	uint32_t depthAlign = dkImageLayoutGetAlignment(&depthLayout);
	depthSize = ALIGN(depthSize, depthAlign);

	dkMemBlockMakerDefaults(&memBlockMaker, g_device, depthSize);
	memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_depthMemBlock = dkMemBlockCreate(&memBlockMaker);

	dkImageInitialize(&g_depthbuffer, &depthLayout, g_depthMemBlock, 0);

	// Create a swapchain out of the framebuffers we've just initialized
	DkSwapchainMaker swapchainMaker;
	dkSwapchainMakerDefaults(&swapchainMaker, g_device, nwindowGetDefault(), swapchainImages, FB_NUM);
	g_swapchain = dkSwapchainCreate(&swapchainMaker);

	// Create a memory block onto which we will load shader code
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, CODEMEMSIZE);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
	g_codeMemBlock = dkMemBlockCreate(&memBlockMaker);
	g_codeMemOffset = 0;

	// Load our shaders (both vertex and fragment)
	loadShaderMemory(&g_vertexShader, &_binary_triangle_vsh_dksh_start, (uintptr_t)&_binary_triangle_vsh_dksh_end - (uintptr_t)&_binary_triangle_vsh_dksh_start);
	loadShaderMemory(&g_fragmentShader, &_binary_color_fsh_dksh_start, (uintptr_t)&_binary_color_fsh_dksh_end - (uintptr_t)&_binary_color_fsh_dksh_start);
	// Fase 2: shader de textura (modulate).
	loadShaderMemory(&g_fragmentTexShader, &_binary_texture_fsh_dksh_start, (uintptr_t)&_binary_texture_fsh_dksh_end - (uintptr_t)&_binary_texture_fsh_dksh_start);

	// Fase 2: blocos de descritores (imagem: 1 slot por run; sampler: 2 fixos).
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, TEX_IMG_SLOTS * DK_IMAGE_DESCRIPTOR_ALIGNMENT);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached;
	g_imgDescBlock = dkMemBlockCreate(&memBlockMaker);

	dkMemBlockMakerDefaults(&memBlockMaker, g_device, 2 * DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached;
	g_sampDescBlock = dkMemBlockCreate(&memBlockMaker);

	{
		DkSampler sampler;
		dkSamplerDefaults(&sampler);
		sampler.minFilter = DkFilter_Linear;
		sampler.magFilter = DkFilter_Linear;
		sampler.wrapMode[0] = DkWrapMode_Repeat;
		sampler.wrapMode[1] = DkWrapMode_Repeat;
		sampler.wrapMode[2] = DkWrapMode_Repeat;
		auto desc = reinterpret_cast<DkSamplerDescriptor*>(
		    dkMemBlockGetCpuAddr(g_sampDescBlock));
		dkSamplerDescriptorInitialize(&desc[TEX_SAMP_REPEAT], &sampler);

		sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
		sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
		sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
		dkSamplerDescriptorInitialize(&desc[TEX_SAMP_CLAMP], &sampler);
	}

	// Create a memory block which will be used for recording command lists using a command buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, CMDMEMSIZE);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_cmdbufMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a command buffer object
	DkCmdBufMaker cmdbufMaker;
	dkCmdBufMakerDefaults(&cmdbufMaker, g_device);
	g_cmdbuf = dkCmdBufCreate(&cmdbufMaker);

	// Feed our memory to the command buffer so that we can start recording commands
	dkCmdBufAddMemory(g_cmdbuf, g_cmdbufMemBlock, 0, dkMemBlockGetSize(g_cmdbufMemBlock));

	// Create a memory block which will be used for recording command lists using a command buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, 4096);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_bindFbCmdbufMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a command buffer object
	dkCmdBufMakerDefaults(&cmdbufMaker, g_device);
	g_bindFbCmdbuf = dkCmdBufCreate(&cmdbufMaker);

	// Feed our memory to the command buffer so that we can start recording commands
	dkCmdBufAddMemory(g_bindFbCmdbuf, g_bindFbCmdbufMemBlock, 0, dkMemBlockGetSize(g_bindFbCmdbufMemBlock));

	// Generate a command list for each framebuffer, which will bind each of them as a render target
	DkImageView depthView;
	dkImageViewDefaults(&depthView, &g_depthbuffer);
	for(unsigned i = 0; i < FB_NUM; i++)
	{
		DkImageView imageView;
		dkImageViewDefaults(&imageView, &g_framebuffers[i]);
		dkCmdBufBindRenderTarget(g_bindFbCmdbuf, &imageView, &depthView);
		g_cmdsBindFramebuffer[i] = dkCmdBufFinishList(g_bindFbCmdbuf);
	}

	// Allocate vertex buffer (Fase 2: vértice de 32 bytes agora).
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, ALIGN(sizeof(Vertex) * VERTEX_BUFFER_ENTRIES, 4096));
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_vtxBufferMemBlock = dkMemBlockCreate(&memBlockMaker);
	g_vtxBufferHead = 0;

	// Allocate uniform buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, ALIGN(sizeof(float) * 4 * 4, 4096));
	memBlockMaker.flags = DkMemBlockFlags_GpuCached;
	g_unifMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a queue, to which we will submit our command lists
	DkQueueMaker queueMaker;
	dkQueueMakerDefaults(&queueMaker, g_device);
	queueMaker.flags = DkQueueFlags_Graphics;
	g_renderQueue = dkQueueCreate(&queueMaker);

	BeginFrame();
}

void CGSH_Deko3d::ReleaseImpl()
{
	EndFrame();

	// Make sure the rendering queue is idle before destroying anything
	dkQueueWaitIdle(g_renderQueue);

	// Fase 2: libera texturas com a GPU parada (seguro).
	TexCache_CollectGarbage(g_renderQueue, true);
	m_texRegistry.clear();

	// Destroy all the resources we've created
	dkQueueDestroy(g_renderQueue);
	dkCmdBufDestroy(g_cmdbuf);
	dkCmdBufDestroy(g_bindFbCmdbuf);
	dkMemBlockDestroy(g_cmdbufMemBlock);
	dkMemBlockDestroy(g_bindFbCmdbufMemBlock);
	dkMemBlockDestroy(g_codeMemBlock);
	dkMemBlockDestroy(g_imgDescBlock);
	dkMemBlockDestroy(g_sampDescBlock);
	dkSwapchainDestroy(g_swapchain);
	dkMemBlockDestroy(g_framebufferMemBlock);
	dkMemBlockDestroy(g_depthMemBlock);
	dkMemBlockDestroy(g_vtxBufferMemBlock);
	dkMemBlockDestroy(g_unifMemBlock);
	dkDeviceDestroy(g_device);
}

void CGSH_Deko3d::BeginFrame()
{
	// Declare structs that will be used for binding state
	DkViewport viewport = {0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f};
	DkScissor scissor = {0, 0, FB_WIDTH, FB_HEIGHT};
	DkShader const* shaders[] = {&g_vertexShader, &g_fragmentShader};
	DkRasterizerState rasterizerState;
	DkColorState colorState;
	DkColorWriteState colorWriteState;
	DkDepthStencilState depthStencilState;

	// Fase 2: posição + cor + (u, v, q).
	constexpr DkVtxAttribState VertexAttribState[3] =
	    {
	        DkVtxAttribState{0, 0, 0, DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
	        DkVtxAttribState{0, 0, offsetof(Vertex, color), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
	        DkVtxAttribState{0, 0, offsetof(Vertex, u), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
	    };

	constexpr DkVtxBufferState VertexBufferState[1] =
	    {
	        DkVtxBufferState{sizeof(Vertex), 0},
	    };

	// Initialize state structs
	dkRasterizerStateDefaults(&rasterizerState);
	rasterizerState.cullMode = DkFace_None;
	rasterizerState.frontFace = DkFrontFace_CW;
	dkColorStateDefaults(&colorState);
	dkColorWriteStateDefaults(&colorWriteState);
	dkDepthStencilStateDefaults(&depthStencilState);

	dkCmdBufClear(g_cmdbuf);

	/* Start recording the command list */
	dkCmdBufSetViewports(g_cmdbuf, 0, &viewport, 1);
	dkCmdBufSetScissors(g_cmdbuf, 0, &scissor, 1);
	dkCmdBufClearColorFloat(g_cmdbuf, 0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	dkCmdBufClearDepthStencil(g_cmdbuf, true, 0.0f, 0xFF, 0);
	dkCmdBufBindShaders(g_cmdbuf, DkStageFlag_GraphicsMask, shaders, sizeof(shaders) / sizeof(shaders[0]));
	dkCmdBufBindUniformBuffer(g_cmdbuf, DkStage_Vertex, 0, dkMemBlockGetGpuAddr(g_unifMemBlock), dkMemBlockGetSize(g_unifMemBlock));
	dkCmdBufBindRasterizerState(g_cmdbuf, &rasterizerState);
	dkCmdBufBindColorState(g_cmdbuf, &colorState);
	dkCmdBufBindColorWriteState(g_cmdbuf, &colorWriteState);
	dkCmdBufBindDepthStencilState(g_cmdbuf, &depthStencilState);
	dkCmdBufBindVtxAttribState(g_cmdbuf, VertexAttribState, 3);
	dkCmdBufBindVtxBufferState(g_cmdbuf, VertexBufferState, 1);
	dkCmdBufPushConstants(g_cmdbuf, dkMemBlockGetGpuAddr(g_unifMemBlock), dkMemBlockGetSize(g_unifMemBlock),
	                      0, sizeof(m_vertexParams.projMatrix), m_vertexParams.projMatrix);

	// Fase 2: sets de descritores (imagem + sampler) válidos o frame todo;
	// o conteúdo dos slots de imagem é preenchido por draw run.
	dkCmdBufBindImageDescriptorSet(g_cmdbuf, dkMemBlockGetGpuAddr(g_imgDescBlock), TEX_IMG_SLOTS);
	dkCmdBufBindSamplerDescriptorSet(g_cmdbuf, dkMemBlockGetGpuAddr(g_sampDescBlock), 2);

	// Fase 2: coleta de texturas evictadas (só trava a GPU se passar do teto).
	TexCache_CollectGarbage(g_renderQueue, false);

	g_vtxBufferHead = 0;
	m_runs.clear();
	m_texSlotCursor = 0;
}

void CGSH_Deko3d::EndFrame()
{
	fprintf(stderr, "g_vtxBufferHead: %d\n", g_vtxBufferHead);

	dkCmdBufBindVtxBuffer(g_cmdbuf, 0, dkMemBlockGetGpuAddr(g_vtxBufferMemBlock), dkMemBlockGetSize(g_vtxBufferMemBlock));

	DkShader const* colorShaders[] = {&g_vertexShader, &g_fragmentShader};
	DkShader const* texShaders[] = {&g_vertexShader, &g_fragmentTexShader};

	// Fase 2: um Draw por run (textura/pipeline homogêneos por run).
	for(const auto& run : m_runs)
	{
		if(run.vertexCount == 0) continue;
		if(run.textured && (run.texture != nullptr) && run.texture->imageReady)
		{
			if(m_texSlotCursor < TEX_IMG_SLOTS)
			{
				uint32_t slot = m_texSlotCursor++;
				auto descBase = reinterpret_cast<DkImageDescriptor*>(
				    dkMemBlockGetCpuAddr(g_imgDescBlock));
				DkImageView view;
				dkImageViewDefaults(&view, &run.texture->image);
				dkImageDescriptorInitialize(&descBase[slot], &view, false, false);
				uint32_t sampSlot = run.repeatWrap ? TEX_SAMP_REPEAT : TEX_SAMP_CLAMP;
				dkCmdBufBindShaders(g_cmdbuf, DkStageFlag_GraphicsMask,
				    texShaders, sizeof(texShaders) / sizeof(texShaders[0]));
				dkCmdBufBindImage(g_cmdbuf, DkStage_Fragment, 0,
				    dkMakeTextureHandle(slot, sampSlot));
			}
			else
			{
				// Teto de slots: degradação segura (sem textura).
				dkCmdBufBindShaders(g_cmdbuf, DkStageFlag_GraphicsMask,
				    colorShaders, sizeof(colorShaders) / sizeof(colorShaders[0]));
			}
		}
		else
		{
			dkCmdBufBindShaders(g_cmdbuf, DkStageFlag_GraphicsMask,
			    colorShaders, sizeof(colorShaders) / sizeof(colorShaders[0]));
		}
		dkCmdBufDraw(g_cmdbuf, static_cast<DkPrimitive>(run.primType),
		    run.vertexCount, 1, run.firstVertex, 0);
	}

	g_cmdsRender = dkCmdBufFinishList(g_cmdbuf);

	// Acquire a framebuffer from the swapchain (and wait for it to be available)
	int slot = dkQueueAcquireImage(g_renderQueue, g_swapchain);

	// Run the command list that binds said framebuffer as a render target
	dkQueueSubmitCommands(g_renderQueue, g_cmdsBindFramebuffer[slot]);

	// Run the main rendering command list
	dkQueueSubmitCommands(g_renderQueue, g_cmdsRender);

	// Now that we are done rendering, present it to the screen
	dkQueuePresentImage(g_renderQueue, g_swapchain, slot);
}

void CGSH_Deko3d::MarkNewFrame()
{
	fprintf(stderr, "CGSH_Deko3d::MarkNewFrame\n");

	EndFrame();
	BeginFrame();

	CGSHandler::MarkNewFrame();
}

void CGSH_Deko3d::FlipImpl(const DISPLAY_INFO& dispInfo)
{
	fprintf(stderr, "CGSH_Deko3d::FlipImpl\n");

	CGSHandler::FlipImpl(dispInfo);
}

void CGSH_Deko3d::SetupDepthBuffer(uint64 zbufReg, uint64 frameReg)
{
	auto frame = make_convertible<FRAME>(frameReg);
	auto zbuf = make_convertible<ZBUF>(zbufReg);

	switch(CGsPixelFormats::GetPsmPixelSize(zbuf.nPsm))
	{
	case 16:
		m_nMaxZ = 32768.0f;
		break;
	case 24:
		m_nMaxZ = 8388608.0f;
		break;
	default:
	case 32:
		m_nMaxZ = 2147483647.0f;
		break;
	}
}

void CGSH_Deko3d::SetupTestFunctions(uint64 testReg)
{
	DkDepthStencilState depthStencilState;

	dkDepthStencilStateDefaults(&depthStencilState);

	auto tst = make_convertible<TEST>(testReg);

	if(tst.nDepthEnabled)
	{
		DkCompareOp depthFunc = DkCompareOp_Never;

		switch(tst.nDepthMethod)
		{
		case DEPTH_TEST_NEVER:
			depthFunc = DkCompareOp_Never;
			break;
		case DEPTH_TEST_ALWAYS:
			depthFunc = DkCompareOp_Always;
			break;
		case DEPTH_TEST_GEQUAL:
			depthFunc = DkCompareOp_Gequal;
			break;
		case DEPTH_TEST_GREATER:
			depthFunc = DkCompareOp_Greater;
			break;
		}
		depthStencilState.depthCompareOp = depthFunc;
		depthStencilState.depthTestEnable = true;
	}
	else
	{
		depthStencilState.depthTestEnable = false;
	}

	dkCmdBufBindDepthStencilState(g_cmdbuf, &depthStencilState);
}

void CGSH_Deko3d::SetRenderingContext(uint64 primReg)
{
	auto prim = make_convertible<PRMODE>(primReg);

	unsigned int context = prim.nContext;

	auto offset = make_convertible<XYOFFSET>(m_nReg[GS_REG_XYOFFSET_1 + context]);
	auto frame = make_convertible<FRAME>(m_nReg[GS_REG_FRAME_1 + context]);
	auto zbuf = make_convertible<ZBUF>(m_nReg[GS_REG_ZBUF_1 + context]);
	auto tex0 = make_convertible<TEX0>(m_nReg[GS_REG_TEX0_1 + context]);
	auto tex1 = make_convertible<TEX1>(m_nReg[GS_REG_TEX1_1 + context]);
	auto clamp = make_convertible<CLAMP>(m_nReg[GS_REG_CLAMP_1 + context]);
	auto alpha = make_convertible<ALPHA>(m_nReg[GS_REG_ALPHA_1 + context]);
	auto scissor = make_convertible<SCISSOR>(m_nReg[GS_REG_SCISSOR_1 + context]);
	auto test = make_convertible<TEST>(m_nReg[GS_REG_TEST_1 + context]);
	auto texA = make_convertible<TEXA>(m_nReg[GS_REG_TEXA]);
	auto fogCol = make_convertible<FOGCOL>(m_nReg[GS_REG_FOGCOL]);
	auto scanMask = m_nReg[GS_REG_SCANMSK] & 3;
	auto colClamp = m_nReg[GS_REG_COLCLAMP] & 1;
	auto fba = m_nReg[GS_REG_FBA_1 + context] & 1;

	SetupDepthBuffer(zbuf, frame);
	SetupTestFunctions(test);

	MakeLinearZOrtho(m_vertexParams.projMatrix, 0, frame.GetWidth(), 0, 480);

	m_fbBasePtr = frame.GetBasePtr();

	m_primOfsX = offset.GetX();
	m_primOfsY = offset.GetY();

	m_texWidth = tex0.GetWidth();
	m_texHeight = tex0.GetHeight();

	// Fase 2: guarda o contexto de textura do prim atual.
	m_curTex0 = tex0;
	m_curClamp = clamp;
	m_haveTexContext = true;
}

// Fase 2: resolve a textura do prim atual (ou nullptr = desenhar sem textura).
std::shared_ptr<Deko3dTexture> CGSH_Deko3d::ResolvePrimTexture()
{
	if(!m_primitiveMode.nTexture || !m_haveTexContext) return nullptr;
	if((m_texWidth == 0) || (m_texHeight == 0)) return nullptr;
	auto texture = TexCache_Prepare(m_curTex0, m_curClamp, g_cmdbuf, g_device);
	if((texture == nullptr) && (m_unsupportedTexWarns < 8))
	{
		m_unsupportedTexWarns++;
		fprintf(stderr, "Fase2: PSM sem suporte (%u), desenhando sem textura\n",
		    static_cast<unsigned int>(m_curTex0.nPsm));
	}
	return texture;
}

bool CGSH_Deko3d::EmitVertex(float x, float y, float z, uint32 color,
    float u, float v, float q, uint32 primType,
    const std::shared_ptr<Deko3dTexture>& texture, bool repeatWrap)
{
	bool textured = (texture != nullptr);
	if(g_vtxBufferHead >= VERTEX_BUFFER_ENTRIES) return false;

	if(m_runs.empty() ||
	   (m_runs.back().primType != primType) ||
	   (m_runs.back().textured != textured) ||
	   (m_runs.back().texture != texture) ||
	   (m_runs.back().repeatWrap != repeatWrap))
	{
		DrawRun run;
		run.firstVertex = g_vtxBufferHead;
		run.vertexCount = 0;
		run.primType = primType;
		run.textured = textured;
		run.texture = texture;
		run.repeatWrap = repeatWrap;
		m_runs.push_back(std::move(run));
	}

	auto vertices = reinterpret_cast<Vertex*>(dkMemBlockGetCpuAddr(g_vtxBufferMemBlock));
	vertices[g_vtxBufferHead] = {x, y, z, color, u, v, q, 0.0f};
	g_vtxBufferHead++;
	m_runs.back().vertexCount++;
	return true;
}

void CGSH_Deko3d::VertexKick(uint8 registerId, uint64 data)
{
	//fprintf(stderr, "CGSH_Deko3d::VertexKick\n");

	if(m_vtxCount == 0)
		return;

	bool drawingKick = (registerId == GS_REG_XYZ2) || (registerId == GS_REG_XYZF2);
	bool fog = (registerId == GS_REG_XYZF2) || (registerId == GS_REG_XYZF3);

	if(!m_drawEnabled)
		drawingKick = false;

	if(fog)
	{
		m_vtxBuffer[m_vtxCount - 1].position = data & 0x00FFFFFFFFFFFFFFULL;
		m_vtxBuffer[m_vtxCount - 1].rgbaq = m_nReg[GS_REG_RGBAQ];
		m_vtxBuffer[m_vtxCount - 1].uv = m_nReg[GS_REG_UV];
		m_vtxBuffer[m_vtxCount - 1].st = m_nReg[GS_REG_ST];
		m_vtxBuffer[m_vtxCount - 1].fog = static_cast<uint8>(data >> 56);
	}
	else
	{
		m_vtxBuffer[m_vtxCount - 1].position = data;
		m_vtxBuffer[m_vtxCount - 1].rgbaq = m_nReg[GS_REG_RGBAQ];
		m_vtxBuffer[m_vtxCount - 1].uv = m_nReg[GS_REG_UV];
		m_vtxBuffer[m_vtxCount - 1].st = m_nReg[GS_REG_ST];
		m_vtxBuffer[m_vtxCount - 1].fog = static_cast<uint8>(m_nReg[GS_REG_FOG] >> 56);
	}

	m_vtxCount--;

	if(m_vtxCount == 0)
	{
		if((m_nReg[GS_REG_PRMODECONT] & 1) != 0)
			m_primitiveMode <<= m_nReg[GS_REG_PRIM];
		else
			m_primitiveMode <<= m_nReg[GS_REG_PRMODE];

		if(drawingKick)
			SetRenderingContext(m_primitiveMode);

		switch(m_primitiveType)
		{
		case PRIM_POINT:
			if(drawingKick) Prim_Point();
			m_vtxCount = 1;
			break;
		case PRIM_LINE:
			if(drawingKick) Prim_Line();
			m_vtxCount = 2;
			break;
		case PRIM_LINESTRIP:
			if(drawingKick) Prim_Line();
			memcpy(&m_vtxBuffer[1], &m_vtxBuffer[0], sizeof(VERTEX));
			m_vtxCount = 1;
			break;
		case PRIM_TRIANGLE:
			if(drawingKick) Prim_Triangle();
			m_vtxCount = 3;
			break;
		case PRIM_TRIANGLESTRIP:
			if(drawingKick) Prim_Triangle();
			memcpy(&m_vtxBuffer[2], &m_vtxBuffer[1], sizeof(VERTEX));
			memcpy(&m_vtxBuffer[1], &m_vtxBuffer[0], sizeof(VERTEX));
			m_vtxCount = 1;
			break;
		case PRIM_TRIANGLEFAN:
			if(drawingKick) Prim_Triangle();
			memcpy(&m_vtxBuffer[1], &m_vtxBuffer[0], sizeof(VERTEX));
			m_vtxCount = 1;
			break;
		case PRIM_SPRITE:
			if(drawingKick) Prim_Sprite();
			m_vtxCount = 2;
			break;
		}
	}
}

float CGSH_Deko3d::CalcZ(float nZ)
{
	if(nZ < 256.0f)
		return nZ / 32768.0f;
	else if(nZ > m_nMaxZ)
		return 1.0f;
	else
		return nZ / m_nMaxZ;
}

void CGSH_Deko3d::Prim_Point()
{
	auto xyz = make_convertible<XYZ>(m_vtxBuffer[0].position);
	auto rgbaq = make_convertible<RGBAQ>(m_vtxBuffer[0].rgbaq);

	float x = xyz.GetX() - m_primOfsX;
	float y = xyz.GetY() - m_primOfsY;
	float z = CalcZ(static_cast<float>(xyz.nZ));

	auto color = MakeColor(rgbaq.nR, rgbaq.nG, rgbaq.nB, rgbaq.nA);

	// Fase 2: pontos sem textura na v1 (raro texturizar ponto).
	EmitVertex(x, y, z, color, 0.0f, 0.0f, 1.0f,
	    DkPrimitive_Points, nullptr, true);
}

void CGSH_Deko3d::Prim_Line()
{
	XYZ pos[2];
	pos[0] <<= m_vtxBuffer[1].position;
	pos[1] <<= m_vtxBuffer[0].position;

	float x1 = pos[0].GetX() - m_primOfsX, x2 = pos[1].GetX() - m_primOfsX;
	float y1 = pos[0].GetY() - m_primOfsY, y2 = pos[1].GetY() - m_primOfsY;
	float z1 = CalcZ(static_cast<float>(pos[0].nZ));
	float z2 = CalcZ(static_cast<float>(pos[1].nZ));

	RGBAQ rgbaq[2];
	rgbaq[0] <<= m_vtxBuffer[1].rgbaq;
	rgbaq[1] <<= m_vtxBuffer[0].rgbaq;

	auto color1 = MakeColor(rgbaq[0].nR, rgbaq[0].nG, rgbaq[0].nB, rgbaq[0].nA);
	auto color2 = MakeColor(rgbaq[1].nR, rgbaq[1].nG, rgbaq[1].nB, rgbaq[1].nA);

	// Fase 2: linhas sem textura na v1.
	EmitVertex(x1, y1, z1, color1, 0.0f, 0.0f, 1.0f,
	    DkPrimitive_Lines, nullptr, true);
	EmitVertex(x2, y2, z2, color2, 0.0f, 0.0f, 1.0f,
	    DkPrimitive_Lines, nullptr, true);
}

void CGSH_Deko3d::Prim_Triangle()
{
	XYZ pos[3];
	pos[0] <<= m_vtxBuffer[2].position;
	pos[1] <<= m_vtxBuffer[1].position;
	pos[2] <<= m_vtxBuffer[0].position;

	float x1 = pos[0].GetX(), x2 = pos[1].GetX(), x3 = pos[2].GetX();
	float y1 = pos[0].GetY(), y2 = pos[1].GetY(), y3 = pos[2].GetY();
	uint32 z1 = pos[0].nZ, z2 = pos[1].nZ, z3 = pos[2].nZ;

	RGBAQ rgbaq[3];
	rgbaq[0] <<= m_vtxBuffer[2].rgbaq;
	rgbaq[1] <<= m_vtxBuffer[1].rgbaq;
	rgbaq[2] <<= m_vtxBuffer[0].rgbaq;

	x1 -= m_primOfsX;
	x2 -= m_primOfsX;
	x3 -= m_primOfsX;

	y1 -= m_primOfsY;
	y2 -= m_primOfsY;
	y3 -= m_primOfsY;

	float s[3] = {0, 0, 0};
	float t[3] = {0, 0, 0};
	float q[3] = {1, 1, 1};

	float f[3] = {0, 0, 0};

	if(m_primitiveMode.nFog)
	{
		f[0] = static_cast<float>(0xFF - m_vtxBuffer[2].fog) / 255.0f;
		f[1] = static_cast<float>(0xFF - m_vtxBuffer[1].fog) / 255.0f;
		f[2] = static_cast<float>(0xFF - m_vtxBuffer[0].fog) / 255.0f;
	}

	if(m_primitiveMode.nTexture)
	{
		if(m_primitiveMode.nUseUV)
		{
			UV uv[3];
			uv[0] <<= m_vtxBuffer[2].uv;
			uv[1] <<= m_vtxBuffer[1].uv;
			uv[2] <<= m_vtxBuffer[0].uv;

			s[0] = uv[0].GetU() / static_cast<float>(m_texWidth);
			s[1] = uv[1].GetU() / static_cast<float>(m_texWidth);
			s[2] = uv[2].GetU() / static_cast<float>(m_texWidth);

			t[0] = uv[0].GetV() / static_cast<float>(m_texHeight);
			t[1] = uv[1].GetV() / static_cast<float>(m_texHeight);
			t[2] = uv[2].GetV() / static_cast<float>(m_texHeight);
		}
		else
		{
			ST st[3];
			st[0] <<= m_vtxBuffer[2].st;
			st[1] <<= m_vtxBuffer[1].st;
			st[2] <<= m_vtxBuffer[0].st;

			s[0] = st[0].nS;
			s[1] = st[1].nS;
			s[2] = st[2].nS;
			t[0] = st[0].nT;
			t[1] = st[1].nT;
			t[2] = st[2].nT;

			q[0] = rgbaq[0].nQ;
			q[1] = rgbaq[1].nQ;
			q[2] = rgbaq[2].nQ;

			// Fase 2: normaliza ST para 0..1 (shader divide por q).
			if((m_texWidth != 0) && (m_texHeight != 0))
			{
				for(int i = 0; i < 3; i++)
				{
					s[i] /= static_cast<float>(m_texWidth);
					t[i] /= static_cast<float>(m_texHeight);
				}
			}
		}
	}

	auto color1 = MakeColor(
	    rgbaq[0].nR, rgbaq[0].nG,
	    rgbaq[0].nB, rgbaq[0].nA);

	auto color2 = MakeColor(
	    rgbaq[1].nR, rgbaq[1].nG,
	    rgbaq[1].nB, rgbaq[1].nA);

	auto color3 = MakeColor(
	    rgbaq[2].nR, rgbaq[2].nG,
	    rgbaq[2].nB, rgbaq[2].nA);

	if(m_primitiveMode.nShading == 0)
	{
		//Flat shaded triangles use the last color set
		color1 = color2 = color3;
	}

#if 0
	fprintf(stderr, "Triangle {%f %f %f}, {%f %f %f}, {%f %f %f}\n", x1, y1, CalcZ(z1), x2, y2, CalcZ(z2), x3, y3, CalcZ(z3));
#endif

	// Fase 2: emite com UV; resolve a textura uma vez por primitiva.
	auto texture = ResolvePrimTexture();
	bool repeatWrap = (texture != nullptr) ? texture->useRepeat : true;
	EmitVertex(x1, y1, CalcZ(static_cast<float>(z1)), color1, s[0], t[0], q[0],
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x2, y2, CalcZ(static_cast<float>(z2)), color2, s[1], t[1], q[1],
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x3, y3, CalcZ(static_cast<float>(z3)), color3, s[2], t[2], q[2],
	    DkPrimitive_Triangles, texture, repeatWrap);
}

void CGSH_Deko3d::Prim_Sprite()
{
	XYZ pos[2];
	pos[0] <<= m_vtxBuffer[1].position;
	pos[1] <<= m_vtxBuffer[0].position;

	float x1 = pos[0].GetX(), y1 = pos[0].GetY();
	float x2 = pos[1].GetX(), y2 = pos[1].GetY();
	float z = CalcZ(static_cast<float>(pos[1].nZ));

	RGBAQ rgbaq[2];
	rgbaq[0] <<= m_vtxBuffer[1].rgbaq;
	rgbaq[1] <<= m_vtxBuffer[0].rgbaq;

	x1 -= m_primOfsX;
	x2 -= m_primOfsX;

	y1 -= m_primOfsY;
	y2 -= m_primOfsY;

	float s[2] = {0, 0};
	float t[2] = {0, 0};

	if(m_primitiveMode.nTexture)
	{
		if(m_primitiveMode.nUseUV)
		{
			UV uv[2];
			uv[0] <<= m_vtxBuffer[1].uv;
			uv[1] <<= m_vtxBuffer[0].uv;

			s[0] = uv[0].GetU() / static_cast<float>(m_texWidth);
			s[1] = uv[1].GetU() / static_cast<float>(m_texWidth);

			t[0] = uv[0].GetV() / static_cast<float>(m_texHeight);
			t[1] = uv[1].GetV() / static_cast<float>(m_texHeight);
		}
		else
		{
			ST st[2];

			st[0] <<= m_vtxBuffer[1].st;
			st[1] <<= m_vtxBuffer[0].st;

			float q1 = rgbaq[1].nQ;
			float q2 = rgbaq[0].nQ;
			if(q1 == 0) q1 = 1;
			if(q2 == 0) q2 = 1;

			s[0] = st[0].nS / q1;
			s[1] = st[1].nS / q2;

			t[0] = st[0].nT / q1;
			t[1] = st[1].nT / q2;

			// Fase 2: normaliza para 0..1 (shader espera UV normalizado).
			if((m_texWidth != 0) && (m_texHeight != 0))
			{
				s[0] /= static_cast<float>(m_texWidth);
				s[1] /= static_cast<float>(m_texWidth);
				t[0] /= static_cast<float>(m_texHeight);
				t[1] /= static_cast<float>(m_texHeight);
			}
		}
	}

	auto color = MakeColor(
	    rgbaq[1].nR, rgbaq[1].nG,
	    rgbaq[1].nB, rgbaq[1].nA);

	// Fase 2: sprite como 2 triângulos (q=1, divisão já feita acima).
	auto texture = ResolvePrimTexture();
	bool repeatWrap = (texture != nullptr) ? texture->useRepeat : true;
	EmitVertex(x1, y1, z, color, s[0], t[0], 1.0f,
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x2, y1, z, color, s[1], t[0], 1.0f,
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x1, y2, z, color, s[0], t[1], 1.0f,
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x1, y2, z, color, s[0], t[1], 1.0f,
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x2, y1, z, color, s[1], t[0], 1.0f,
	    DkPrimitive_Triangles, texture, repeatWrap);
	EmitVertex(x2, y2, z, color, s[1], t[1], 1.0f,
	    DkPrimitive_Triangles, texture, repeatWrap);
}

void CGSH_Deko3d::WriteRegisterImpl(uint8 registerId, uint64 data)
{
	//fprintf(stderr, "CGSH_Deko3d::WriteRegisterImpl\n");

	CGSHandler::WriteRegisterImpl(registerId, data);

	switch(registerId)
	{
	case GS_REG_PRIM:
		m_primitiveType = static_cast<unsigned int>(data & 0x07);
		switch(m_primitiveType)
		{
		case PRIM_POINT:
			m_vtxCount = 1;
			break;
		case PRIM_LINE:
		case PRIM_LINESTRIP:
			m_vtxCount = 2;
			break;
		case PRIM_TRIANGLE:
		case PRIM_TRIANGLESTRIP:
		case PRIM_TRIANGLEFAN:
			m_vtxCount = 3;
			break;
		case PRIM_SPRITE:
			m_vtxCount = 2;
			break;
		}
		break;
	case GS_REG_XYZ2:
	case GS_REG_XYZ3:
	case GS_REG_XYZF2:
	case GS_REG_XYZF3:
		VertexKick(registerId, data);
		break;
	}
}

void CGSH_Deko3d::ProcessHostToLocalTransfer()
{
	//fprintf(stderr, "CGSH_Deko3d::ProcessHostToLocalTransfer()\n");
	// Fase 2: transferência EE->GS pode ter alterado texels: invalida o cache.
	TexCache_Flush();
}

void CGSH_Deko3d::ProcessLocalToHostTransfer()
{
	//fprintf(stderr, "CGSH_Deko3d::ProcessLocalToHostTransfer()\n");
}

void CGSH_Deko3d::ProcessLocalToLocalTransfer()
{
	//fprintf(stderr, "CGSH_Deko3d::ProcessLocalToLocalTransfer()\n");
}

void CGSH_Deko3d::ProcessClutTransfer(uint32, uint32)
{
	//fprintf(stderr, "CGSH_Deko3d::ProcessClutTransfer()\n");
	// Fase 2: CLUT nova invalida texturas paletizadas (flush geral na v1).
	TexCache_Flush();
}

CGSHandler::FactoryFunction CGSH_Deko3d::GetFactoryFunction()
{
	return []() { return new CGSH_Deko3d(); };
}

void CGSH_Deko3d::MakeLinearZOrtho(float* matrix, float left, float right, float bottom, float top)
{
	matrix[0] = 2.0f / (right - left);
	matrix[1] = 0;
	matrix[2] = 0;
	matrix[3] = 0;

	matrix[4] = 0;
	matrix[5] = -2.0f / (top - bottom);
	matrix[6] = 0;
	matrix[7] = 0;

	matrix[8] = 0;
	matrix[9] = 0;
	matrix[10] = 1;
	matrix[11] = 0;

	matrix[12] = -(right + left) / (right - left);
	matrix[13] = (top + bottom) / (top - bottom);
	matrix[14] = 0;
	matrix[15] = 1;
}
