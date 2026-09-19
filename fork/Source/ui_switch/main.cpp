// Fase 2.1-debug (RoxasBR90): main.cpp do port Switch com boot blindado.
// Base: Source/ui_switch/main.cpp do xerpi/play-switch @ d9c9e42e.
// Mudanças marcadas com "Fase 2.1": log por estágio (nxlink), try/catch total
// (exceção vira mensagem + saída limpa em vez de abort/reboot) e aviso de
// applet mode (Album dá pouca RAM; prefira title override).

#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <switch.h>

#include "Log.h"
#include "AppConfig.h"
#include "PS2VM.h"
#include "DiskUtils.h"
#include "PH_Generic.h"
#include "GSH_Deko3d.h"

#include "PS2VM_Preferences.h"
#include "ElfGuard.h"

#define DEFAULT_FILE "/switch/Play/test.elf"

#define EXIT_COMBO (HidNpadButton_Plus | HidNpadButton_R)

// Fase 2.1: log de estágio (sai no nxlink; última linha = ponto do crash).
#define BOOTLOG(msg)                      \
	do                                  \
	{                                   \
		fprintf(stderr, "[boot] %s\n", \
		        msg);                   \
	} while(0)

// Fase 2.1: foco/suspensão e dock/undock via hooks (padrão dos samples).
static volatile bool g_focused = true;
static volatile bool g_modeChanged = false;
static AppletHookCookie g_hookCookie;

static void applet_hook(AppletHookType hook, void* param)
{
	(void)param;
	if(hook == AppletHookType_OnFocusState)
	{
		g_focused = (appletGetFocusState() == AppletFocusState_InFocus);
	}
	else if(hook == AppletHookType_OnOperationMode)
	{
		g_modeChanged = true;
	}
}

static bool IsBootableExecutablePath(const fs::path& filePath)
{
	auto extension = filePath.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
	return (extension == ".elf");
}

static bool IsBootableDiscImagePath(const fs::path& filePath)
{
	const auto& supportedExtensions = DiskUtils::GetSupportedExtensions();
	auto extension = filePath.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
	auto extensionIterator = supportedExtensions.find(extension);
	return extensionIterator != std::end(supportedExtensions);
}

int main(int argc, char** argv)
{
	CPS2VM* m_virtualMachine = nullptr;
	bool executionOver = false;
	bool vmPaused = false;
	const char* file;
	fs::path filePath;
	PadState pad;
	int i;

	//consoleDebugInit(debugDevice_SVC);
	//inet_aton("127.0.0.1", &__nxlink_host);
	socketInitializeDefault();
	nxlinkStdio();
	BOOTLOG("inicio (Fase 2.1-debug)");

	// Fase 2.1: applet mode (Album) tem pouca RAM; title override tem RAM cheia.
	{
		AppletType appletType = appletGetAppletType();
		fprintf(stderr, "[boot] appletType=%d (0=app,1=sysapp,2=libapplet,3=other)\n",
		        static_cast<int>(appletType));
		if(appletType != AppletType_Application)
		{
			fprintf(stderr, "[boot] AVISO: rodando em applet mode (Album). Se crashar, teste via title override (segure R num jogo).\n");
		}
	}

	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	BOOTLOG("pad ok");

	appletHook(&g_hookCookie, applet_hook, NULL);

	fprintf(stderr, "Play! " PLAY_VERSION "\n");
	fprintf(stderr, "argc: %d\n", argc);
	for(i = 0; i < argc; i++)
	{
		fprintf(stderr, "argv[%d]: %s\n", i, argv[i]);
	}

	// load from argv if present
	if(argc > 1)
	{
		file = argv[1];
	}
	else
	{
		file = DEFAULT_FILE;
	}

	filePath = file;
	fprintf(stderr, "Booting file: '%s'\n", file);

	if(IsBootableDiscImagePath(filePath))
	{
		CAppConfig::GetInstance().SetPreferencePath(PREF_PS2_CDROM0_PATH, filePath);
		CAppConfig::GetInstance().Save();
	}

	try
	{
		BOOTLOG("criando CPS2VM...");
		m_virtualMachine = new CPS2VM();
		BOOTLOG("Initialize()...");
		m_virtualMachine->Initialize();
		BOOTLOG("Initialize ok; criando pad handler...");
		m_virtualMachine->CreatePadHandler(CPH_Generic::GetFactoryFunction());
		BOOTLOG("pad handler ok; criando GS handler (deko3d)...");
		m_virtualMachine->CreateGSHandler(CGSH_Deko3d::GetFactoryFunction());
		BOOTLOG("GS handler ok (device + shaders + swapchain prontos)");
		auto connection = m_virtualMachine->m_ee->m_os->OnRequestExit.Connect(
		    [&executionOver]() {
			    executionOver = true;
		    });

		fprintf(stderr, "Starting execution...\n");
		if(IsBootableExecutablePath(filePath))
		{
			// Fase 2.1: guarda anti-lixo/ISO antes do parser (vira saída
			// limpa em vez de crash).
			char guardErr[256];
			if(!ElfGuard_Check(file, guardErr, sizeof(guardErr)))
			{
				fprintf(stderr, "[boot] ELF rejeitado: %s\n", guardErr);
				goto done;
			}
			fprintf(stderr, "[boot] BootFromFile...\n");
			m_virtualMachine->m_ee->m_os->BootFromFile(filePath);
		}
		else if(IsBootableDiscImagePath(filePath))
		{
			fprintf(stderr, "[boot] BootFromCDROM...\n");
			m_virtualMachine->m_ee->m_os->BootFromCDROM();
		}
		else
		{
			fprintf(stderr, "[boot] arquivo nao bootavel, saindo limpo\n");
			goto done;
		}
	}
	catch(const std::runtime_error& e)
	{
		fprintf(stderr, "[boot] ERRO (runtime_error): %s\n", e.what());
		goto done;
	}
	catch(const std::exception& e)
	{
		fprintf(stderr, "[boot] ERRO (exception): %s\n", e.what());
		goto done;
	}
	catch(...)
	{
		fprintf(stderr, "[boot] ERRO desconhecido no boot\n");
		goto done;
	}

	fprintf(stderr, "Loaded! Starting execution...\n");
	BOOTLOG("Resume()...");

	m_virtualMachine->Resume();
	BOOTLOG("em execucao (loop principal)");

	{
		unsigned int loopCount = 0;
		while(appletMainLoop() && !executionOver)
		{
			// Fase 2.1: dock/undock invalida a swapchain — recria antes de desenhar.
			if(g_modeChanged)
			{
				g_modeChanged = false;
				auto gsHandler = static_cast<CGSH_Deko3d*>(m_virtualMachine->GetGSHandler());
				if(gsHandler != nullptr)
				{
					fprintf(stderr, "[boot] dock/undock detectado\n");
					gsHandler->HandleOperationModeChanged();
				}
			}
			// Fase 2.1: sem foco (HOME/sleep) não submete nada; pausa a VM.
			if(!g_focused)
			{
				if(!vmPaused)
				{
					fprintf(stderr, "[boot] sem foco: pausando VM\n");
					m_virtualMachine->Pause();
					vmPaused = true;
				}
				svcSleepThread(100000000ULL);
				continue;
			}
			if(vmPaused)
			{
				fprintf(stderr, "[boot] foco de volta: retomando VM\n");
				m_virtualMachine->Resume();
				vmPaused = false;
			}
			padUpdate(&pad);

			u64 buttons = padGetButtons(&pad);
			if((buttons & EXIT_COMBO) == EXIT_COMBO)
				executionOver = true;

			auto padHandler = static_cast<CPH_Generic*>(m_virtualMachine->GetPadHandler());

			//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_LEFT_X, (pad.lx / 255.f) * 2.f - 1.f);
			//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_LEFT_Y, (pad.ly / 255.f) * 2.f - 1.f);
			//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_RIGHT_X, (pad.rx / 255.f) * 2.f - 1.f);
			//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_RIGHT_Y, (ry / 255.f) * 2.f - 1.f);
			padHandler->SetButtonState(PS2::CControllerInfo::DPAD_UP, buttons & HidNpadButton_Up);
			padHandler->SetButtonState(PS2::CControllerInfo::DPAD_DOWN, buttons & HidNpadButton_Down);
			padHandler->SetButtonState(PS2::CControllerInfo::DPAD_LEFT, buttons & HidNpadButton_Left);
			padHandler->SetButtonState(PS2::CControllerInfo::DPAD_RIGHT, buttons & HidNpadButton_Right);
			padHandler->SetButtonState(PS2::CControllerInfo::SELECT, buttons & HidNpadButton_Minus);
			padHandler->SetButtonState(PS2::CControllerInfo::START, buttons & HidNpadButton_Plus);
			padHandler->SetButtonState(PS2::CControllerInfo::SQUARE, buttons & HidNpadButton_Y);
			padHandler->SetButtonState(PS2::CControllerInfo::TRIANGLE, buttons & HidNpadButton_X);
			padHandler->SetButtonState(PS2::CControllerInfo::CIRCLE, buttons & HidNpadButton_A);
			padHandler->SetButtonState(PS2::CControllerInfo::CROSS, buttons & HidNpadButton_B);
			padHandler->SetButtonState(PS2::CControllerInfo::L1, buttons & HidNpadButton_L);
			padHandler->SetButtonState(PS2::CControllerInfo::R1, buttons & HidNpadButton_R);

			// Fase 2.1: heartbeat p/ distinguir crash no boot de crash rodando.
			if((++loopCount % 600) == 0)
			{
				fprintf(stderr, "[boot] loop vivo (%u)\n", loopCount);
			}
		}
	}

done:
	fprintf(stderr, "Finish\n");
	appletUnhook(&g_hookCookie);

	if(m_virtualMachine)
	{
		if(vmPaused)
		{
			m_virtualMachine->Resume();
			vmPaused = false;
		}
		m_virtualMachine->Pause();
		m_virtualMachine->DestroyPadHandler();
		m_virtualMachine->DestroyGSHandler();
		//m_virtualMachine->DestroySoundHandler();
		m_virtualMachine->Destroy();
		delete m_virtualMachine;
		m_virtualMachine = nullptr;
	}

	socketExit();

	return 0;
}
