// bootstages — NRO de diagnóstico visual do boot do emulador (Fase 2.1).
// Cada estágio do boot pinta a tela de uma cor e segura ~2s. A ÚLTIMA COR
// VISÍVEL antes do crash = estágio culpado. Sem rede, sem nxlink, sem log.
//   VERMELHO  = GPU (device+swapchain+present) ok
//   LARANJA   = socket/nxlink + applet/pad ok
//   AMARELO   = new CPS2VM() ok
//   CIANO     = Initialize() (thread da VM) ok
//   MAGENTA   = pad/GS handlers (deko3d init) ok
//   BRANCO    = BootFromFile tentado (arco-íris depois = vivo)
//   CINZA     = exceção C++ capturada (mensagem no nxlink, se ligado)
// Reboot sem cor nova = fatal fora de exceção (svcBreak/abort) no estágio atual.

#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <switch.h>
#include <deko3d.h>

#include "Log.h"
#include "AppConfig.h"
#include "PS2VM.h"
#include "DiskUtils.h"
#include "PH_Generic.h"
#include "GSH_Deko3d.h"

#include "PS2VM_Preferences.h"

#define DEFAULT_FILE "/switch/Play/test.elf"
#define EXIT_COMBO (HidNpadButton_Plus | HidNpadButton_R)

#define FB_NUM 2
#define FB_WIDTH 1280
#define FB_HEIGHT 720
#define CMDMEMSIZE (1 * 1024 * 1024)
#define ALIGN(x, align) (((x) + (align)-1) & ~((align)-1))

static DkDevice g_device;
static DkMemBlock g_framebufferMemBlock;
static DkImage g_framebuffers[FB_NUM];
static DkImage const* g_swapImgs[FB_NUM];
static DkSwapchain g_swapchain;
static DkMemBlock g_bindFbCmdbufMemBlock;
static DkCmdBuf g_bindFbCmdbuf;
static DkCmdList g_cmdsBindFramebuffer[FB_NUM];
static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsRender;
static DkQueue g_renderQueue;
static DkViewport g_viewport = {0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f};
static DkScissor g_scissor = {0, 0, FB_WIDTH, FB_HEIGHT};

static void gpu_init(void)
{
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

	for(unsigned i = 0; i < FB_NUM; i++)
	{
		dkImageInitialize(&g_framebuffers[i], &fbLayout, g_framebufferMemBlock, i * fbSize);
		g_swapImgs[i] = &g_framebuffers[i];
	}

	DkSwapchainMaker scMaker;
	dkSwapchainMakerDefaults(&scMaker, g_device, nwindowGetDefault(), g_swapImgs, FB_NUM);
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
}

static void present_color(float r, float g, float b, unsigned frames)
{
	for(unsigned i = 0; i < frames; i++)
	{
		if(!appletMainLoop()) break;
		dkCmdBufClear(g_cmdbuf);
		dkCmdBufSetViewports(g_cmdbuf, 0, &g_viewport, 1);
		dkCmdBufSetScissors(g_cmdbuf, 0, &g_scissor, 1);
		dkCmdBufClearColorFloat(g_cmdbuf, 0, DkColorMask_RGBA, r, g, b, 1.0f);
		g_cmdsRender = dkCmdBufFinishList(g_cmdbuf);
		int slot = dkQueueAcquireImage(g_renderQueue, g_swapchain);
		dkQueueSubmitCommands(g_renderQueue, g_cmdsBindFramebuffer[slot]);
		dkQueueSubmitCommands(g_renderQueue, g_cmdsRender);
		dkQueuePresentImage(g_renderQueue, g_swapchain, slot);
		svcSleepThread(16000000ULL);
	}
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	CPS2VM* vm = nullptr;

	/* ESTÁGIO GPU (sem VM ainda). */
	gpu_init();
	present_color(1.0f, 0.0f, 0.0f, 120); // VERMELHO

	/* ESTÁGIO rede/applet/pad (igual ao main real). */
	socketInitializeDefault();
	nxlinkStdio();
	{
		PadState pad;
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		padInitializeDefault(&pad);
	}
	present_color(1.0f, 0.5f, 0.0f, 120); // LARANJA

	/* ESTÁGIO VM: construção (FS/config) e init (thread). */
	try
	{
		vm = new CPS2VM();
	}
	catch(...)
	{
		present_color(0.5f, 0.5f, 0.5f, 600); // CINZA = throw no new
		return 1;
	}
	present_color(1.0f, 1.0f, 0.0f, 120); // AMARELO

	try
	{
		vm->Initialize();
	}
	catch(...)
	{
		present_color(0.5f, 0.5f, 0.5f, 600);
		return 1;
	}
	present_color(0.0f, 1.0f, 1.0f, 120); // CIANO

	/* ESTÁGIO handlers (pad + GS/deko3d init real). */
	try
	{
		vm->CreatePadHandler(CPH_Generic::GetFactoryFunction());
		vm->CreateGSHandler(CGSH_Deko3d::GetFactoryFunction());
	}
	catch(...)
	{
		present_color(0.5f, 0.5f, 0.5f, 600);
		return 1;
	}
	present_color(1.0f, 0.0f, 1.0f, 120); // MAGENTA

	/* ESTÁGIO boot do arquivo (test.elf pode não existir = cinza, normal). */
	try
	{
		vm->m_ee->m_os->BootFromFile(DEFAULT_FILE);
	}
	catch(...)
	{
		present_color(0.5f, 0.5f, 0.5f, 600);
		return 1;
	}
	present_color(1.0f, 1.0f, 1.0f, 120); // BRANCO = boot tentado

	/* Vivo: arco-íris + PLUS sai. */
	vm->Resume();
	unsigned frame = 0;
	while(appletMainLoop())
	{
		float t = (float)(frame % 180);
		present_color(t / 180.0f, 1.0f - t / 180.0f, 0.5f, 1);
		frame++;
		if(frame > 3600) break;
	}

	if(vm != nullptr)
	{
		vm->Pause();
		vm->DestroyPadHandler();
		vm->DestroyGSHandler();
		vm->Destroy();
		delete vm;
	}
	dkQueueWaitIdle(g_renderQueue);
	socketExit();
	return 0;
}
