#include <windows.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <D3DX11tex.h>
#pragma comment(lib, "D3DX11.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")
#include <thread>
#include <iostream>
#include <atomic> 
#include <string>
#include <vector>
#include <include/MinHook.h>
#include <TlHelp32.h>
#include <src/adb/adb.hpp>
#include <src/Overlay/Overlay.hpp>
#include <src/Overlay/Render.hpp>
#include <src/Globals.hpp>
#include <EspLines/Data/Data.hpp>
#include <EspLines/Offsets.hpp>
#include <EspLines/Features/Visuals/Visual.hpp>
#include <src/adb/adb_utils.hpp>
#include <EspLines\Aimbot\Aimbot.hpp>
#include "src/ui/ui.hpp"
#include "EspLines/Exploits/Weapons/WeaponAttributes.hpp"
#include "EspLines/Exploits/SpinBot.hpp"
#include "EspLines/Exploits/TeleportMark.hpp"
#include "EspLines/Exploits/Speed/SpeedTimer.hpp"
#include "EspLines/Exploits/Others/TelaParada.hpp"
#include "EspLines/Exploits/Others/BackJump.hpp"
#include "EspLines/Exploits/Others/PullEnemy.hpp"
#include "EspLines/Exploits/Others/TeleKill.hpp"

void initcmd() {
	AllocConsole();
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	SetConsoleTitleA("Debug Console");
}

void closecmd() {
	FreeConsole();
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);
}

HMODULE g_hModule;
FWork::Interface* g_pInterface = nullptr;
DWORD WINAPI Unload() {
	SpeedTimer::Reset();

	if (g_pInterface) {
		g_pInterface->ShutDown();
		delete g_pInterface;
		g_pInterface = nullptr;
	}

	if (MemoryUtils::ogPhysRead) {
		if (MH_DisableHook((LPVOID)MemoryUtils::ogPhysRead) != MH_OK) {
			std::cout << "Falha ao desativar o hook de PGMPhysRead!" << std::endl;
		}

		if (MH_RemoveHook((LPVOID)MemoryUtils::ogPhysRead) != MH_OK) {
			std::cout << "Falha ao remover o hook de PGMPhysRead!" << std::endl;
		}
	}

	MH_Uninitialize();

	if (g_hModule) {
		FreeLibraryAndExitThread(g_hModule, 0);
	}
	return 0;
}

bool memoryinit = false;
void Memoryy() {
	auto vmm = GetModuleHandleA("BstkVMM.dll");
	if (vmm == nullptr) {
		return;
	}

	auto readFunc = (MemoryUtils::PGMPhysReadFunc)GetProcAddress(vmm, "PGMPhysRead");
	if (readFunc == nullptr) {
		return;
	}

	MH_Initialize();
	if (MH_CreateHook((LPVOID)readFunc, MemoryUtils::HookedPGMPhysRead, (LPVOID*)&MemoryUtils::ogPhysRead) != MH_OK) {
		return;
	}

	if (MH_EnableHook((LPVOID)readFunc) != MH_OK) {
		return;
	}

	int timeout = 5000;
	int elapsed = 0;

	while (MemoryUtils::vmPtr == nullptr && elapsed < timeout) {
		Sleep(1);
		elapsed++;
	}

	MemoryUtils::ogCPU = (MemoryUtils::VMMGetCpuByIdFunc)GetProcAddress(vmm, "VMMGetCpuById");
	if (MemoryUtils::ogCPU == nullptr) {
		return;
	}

	MemoryUtils::ogCast = (MemoryUtils::PGMPhysGCPtr2GCPhysFunc)GetProcAddress(vmm, "PGMPhysGCPtr2GCPhys");
	if (MemoryUtils::ogCast == nullptr) {
		return;
	}

	MemoryUtils::ogWrite = (MemoryUtils::PGMPhysSimpleWriteGCPhysFunc)GetProcAddress(vmm, "PGMPhysSimpleWriteGCPhys");
	if (MemoryUtils::ogWrite == nullptr) {
		return;
	}

	MemoryUtils::Initialize(MemoryUtils::vmPtr);
	std::cout << "Virt Memory: " << MemoryUtils::pVMAddr << std::endl;

	memoryinit = true;
}

namespace Cheat {
	void Initialize() {
		FWork::Overlay::Setup(Render::FindRenderWindow());
		FWork::Overlay::Initialize();
		Memoryy();
		if (!memoryinit) {
			MessageBox(nullptr, L"Memory initialization failed", L"Error", MB_OK | MB_ICONERROR);
		}

		std::thread([&]() { FWork::ADB::InitializeADB(); }).detach();

		if (FWork::Overlay::IsInitialized())
		{
			FWork::Interface Interface(FWork::Overlay::GetOverlayWindow(), FWork::Overlay::GetTargetWindow(), FWork::Overlay::dxGetDevice(), FWork::Overlay::dxGetDeviceContext());
			Interface.UpdateStyle();
			FWork::Overlay::SetupWindowProcHook(std::bind(&FWork::Interface::WindowProc, &Interface, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

			MSG Message;
			ZeroMemory(&Message, sizeof(Message));
			while (Message.message != WM_QUIT) {

				if (PeekMessage(&Message, FWork::Overlay::GetOverlayWindow(), NULL, NULL, PM_REMOVE))
				{
					TranslateMessage(&Message);
					DispatchMessage(&Message);
				}

				ImGui::GetIO().MouseDrawCursor = false;

				if (Interface.ResizeHeight != 0 || Interface.ResizeWidht != 0) 
				{
					FWork::Overlay::dxCleanupRenderTarget();
					FWork::Overlay::dxGetSwapChain()->ResizeBuffers(0, Interface.ResizeWidht, Interface.ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
					Interface.ResizeHeight = Interface.ResizeWidht = 0;
					FWork::Overlay::dxCreateRenderTarget();
				}

				Interface.HandleMenuKey();
				FWork::Overlay::UpdateWindowPos();

				static bool CaptureBypassOn = false;
				if (g_Globals.General.Capture != CaptureBypassOn) 
				{
					CaptureBypassOn = g_Globals.General.Capture;
					SetWindowDisplayAffinity(hWindow, CaptureBypassOn ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
				}

				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();
				{
				FWork::Data::Work();

					SpinBot::Run();

						TeleportMark::Run();

						SpeedTimer::Run();

						TelaParada::Run();

						BackJump::Run();

						PullEnemy::Run();

						TeleKill::Run();

						WeaponAttributes::Apply(
						g_Globals.EspConfig.LocalPlayer,
						g_Globals.Exploits.WeaponAttributesLevel - 1,
						g_Globals.Exploits.WeaponAttributes,
						false,
						1.0f
					);


						Interface.RenderGui();

						ESP::Players();

						if (CurrentTab != 0) {
							ESP::PlayerCounter();
							ESP::CounterTime();
							ESP::KeybindsPanel();
						}


					if (g_Globals.Misc.ShowAimbotFov) {
						ImVec2 center(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2);
						float radius = g_Globals.AimBot.Fov * 8.0f;
						float screenDiag = sqrtf(ImGui::GetIO().DisplaySize.x * ImGui::GetIO().DisplaySize.x + ImGui::GetIO().DisplaySize.y * ImGui::GetIO().DisplaySize.y) / 2.0f;
						if (radius <= screenDiag) {
							ImGui::GetBackgroundDrawList()->AddCircle(
								center, radius, ImColor(1.0f, 1.0f, 1.0f, 0.8f), 360
							);
						}
					}
				}
				ImGui::EndFrame();
				ImGui::Render();
				FWork::Overlay::dxRefresh();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				FWork::Overlay::dxGetSwapChain()->Present(0, 0);

				if (g_Globals.General.ShutDown) {
					Unload();
					return;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(g_Globals.General.Delay));
			}
		}
	}
}

DWORD WINAPI DllWorkerThread(LPVOID) {
#ifdef _DEBUG
	initcmd();
#endif

	Cheat::Initialize();

	while (!g_Globals.General.ShutDown) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

#ifdef _DEBUG
	closecmd();
#endif
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	UNREFERENCED_PARAMETER(lpReserved);

	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		g_hModule = hModule;
		DisableThreadLibraryCalls(hModule);
		{
			HANDLE worker = CreateThread(nullptr, 0, DllWorkerThread, nullptr, 0, nullptr);
			if (worker) {
				CloseHandle(worker);
			}
		}
		break;
	case DLL_PROCESS_DETACH:
		g_Globals.General.ShutDown = true;
		break;
	}
	return TRUE;
}
