/* hwtest — teste mínimo de GPU (deko3d) para isolar o crash 2128-0500.
 *
 * Sem VM, sem rede, sem pad além do botão PLUS para sair.
 * Cada estágio tem uma cor de tela. A ÚLTIMA COR VISÍVEL diz onde morreu:
 *   VERMELHO  = device + swapchain + clear + present OK (GPU funciona)
 *   VERDE     = 60 frames apresentados (loop estável)
 *   AZUL      = 120 frames (estável por ~2s+)
 *   (depois alterna R/G/B a cada segundo; PLUS sai limpo)
 * Se crashar antes de qualquer cor = falha no init (device/swapchain).
 */

#include <stdio.h>
#include <switch.h>
#include <deko3d.h>

#define FB_NUM 2
#define FB_WIDTH 1280
#define FB_HEIGHT 720
#define CMDMEMSIZE (1 * 1024 * 1024)

#define ALIGN(x, align) (((x) + (align)-1) & ~((align)-1))

static DkDevice g_device;
static DkMemBlock g_framebufferMemBlock;
static DkImage g_framebuffers[FB_NUM];
static DkSwapchain g_swapchain;
static DkMemBlock g_bindFbCmdbufMemBlock;
static DkCmdBuf g_bindFbCmdbuf;
static DkCmdList g_cmdsBindFramebuffer[FB_NUM];
static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsRender;
static DkQueue g_renderQueue;

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);

	/* --- ESTÁGIO 0: device (tela ainda preta se morrer aqui) --- */
	DkDeviceMaker deviceMaker;
	dkDeviceMakerDefaults(&deviceMaker);
	g_device = dkDeviceCreate(&deviceMaker);

	DkImageLayoutMaker layoutMaker;
	dkImageLayoutMakerDefaults(&layoutMaker, g_device);
	layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
	layoutMaker.format = DkImageFormat_RGBA8_Unorm;
	layoutMaker.dimensions[0] = FB_WIDTH;
	layoutMaker.dimensions[1] = FB_HEIGHT;
	DkImageLayout fbLayout;
	dkImageLayoutInitialize(&fbLayout, &layoutMaker);
	uint32_t fbSize = ALIGN(dkImageLayoutGetSize(&fbLayout), dkImageLayoutGetAlignment(&fbLayout));

	DkMemBlockMaker memMaker;
	dkMemBlockMakerDefaults(&memMaker, g_device, FB_NUM * fbSize);
	memMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_framebufferMemBlock = dkMemBlockCreate(&memMaker);

	DkImage const* swapImgs[FB_NUM];
	for(unsigned i = 0; i < FB_NUM; i++)
	{
		swapImgs[i] = &g_framebuffers[i];
		dkImageInitialize(&g_framebuffers[i], &fbLayout, g_framebufferMemBlock, i * fbSize);
	}

	DkSwapchainMaker scMaker;
	dkSwapchainMakerDefaults(&scMaker, g_device, nwindowGetDefault(), swapImgs, FB_NUM);
	g_swapchain = dkSwapchainCreate(&scMaker);

	dkMemBlockMakerDefaults(&memMaker, g_device, CMDMEMSIZE);
	memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_cmdbufMemBlock = dkMemBlockCreate(&memMaker);
	DkCmdBufMaker cmdMaker;
	dkCmdBufMakerDefaults(&cmdMaker, g_device);
	g_cmdbuf = dkCmdBufCreate(&cmdMaker);
	dkCmdBufAddMemory(g_cmdbuf, g_cmdbufMemBlock, 0, dkMemBlockGetSize(g_cmdbufMemBlock));

	dkMemBlockMakerDefaults(&memMaker, g_device, 4096);
	memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_bindFbCmdbufMemBlock = dkMemBlockCreate(&memMaker);
	dkCmdBufMakerDefaults(&cmdMaker, g_device);
	g_bindFbCmdbuf = dkCmdBufCreate(&cmdMaker);
	dkCmdBufAddMemory(g_bindFbCmdbuf, g_bindFbCmdbufMemBlock, 0, dkMemBlockGetSize(g_bindFbCmdbufMemBlock));

	for(unsigned i = 0; i < FB_NUM; i++)
	{
		DkImageView view;
		dkImageViewDefaults(&view, &g_framebuffers[i]);
		dkCmdBufBindRenderTarget(g_bindFbCmdbuf, &view, NULL);
		g_cmdsBindFramebuffer[i] = dkCmdBufFinishList(g_bindFbCmdbuf);
	}

	DkQueueMaker qMaker;
	dkQueueMakerDefaults(&qMaker, g_device);
	qMaker.flags = DkQueueFlags_Graphics;
	g_renderQueue = dkQueueCreate(&qMaker);

	/* --- Loop: cor = estágio (60 frames cada) --- */
	DkViewport viewport = {0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f};
	DkScissor scissor = {0, 0, FB_WIDTH, FB_HEIGHT};
	unsigned frame = 0;

	while(appletMainLoop())
	{
		padUpdate(&pad);
		if(padGetButtons(&pad) & HidNpadButton_Plus)
			break;

		float r = 0.125f, g = 0.294f, b = 0.478f; /* padrão = "passou do init" */
		unsigned stage = (frame / 60) % 4;
		if(stage == 0)
		{
			r = 1.0f;
			g = 0.0f;
			b = 0.0f; /* VERMELHO */
		}
		else if(stage == 1)
		{
			r = 0.0f;
			g = 1.0f;
			b = 0.0f; /* VERDE */
		}
		else if(stage == 2)
		{
			r = 0.0f;
			g = 0.0f;
			b = 1.0f; /* AZUL */
		}

		dkCmdBufClear(g_cmdbuf);
		dkCmdBufSetViewports(g_cmdbuf, 0, &viewport, 1);
		dkCmdBufSetScissors(g_cmdbuf, 0, &scissor, 1);
		dkCmdBufClearColorFloat(g_cmdbuf, 0, DkColorMask_RGBA, r, g, b, 1.0f);
		g_cmdsRender = dkCmdBufFinishList(g_cmdbuf);

		int slot = dkQueueAcquireImage(g_renderQueue, g_swapchain);
		dkQueueSubmitCommands(g_renderQueue, g_cmdsBindFramebuffer[slot]);
		dkQueueSubmitCommands(g_renderQueue, g_cmdsRender);
		dkQueuePresentImage(g_renderQueue, g_swapchain, slot);
		frame++;
	}

	dkQueueWaitIdle(g_renderQueue);
	return 0;
}
