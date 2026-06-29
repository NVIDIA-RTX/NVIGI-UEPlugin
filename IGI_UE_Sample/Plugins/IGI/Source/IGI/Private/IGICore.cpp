// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "IGICore.h"

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"

#include "IGILog.h"

#include "nvigi.h"
#include "nvigi_ai.h"
#include "nvigi_struct.h"

#if PLATFORM_WINDOWS
// nvigi_types.h requires windows headers for LUID definition, only on Windows.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "nvigi_types.h"
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include "nvigi_types.h"
#endif

FIGICore::FIGICore(FString IGICoreLibraryPath)
{
    IGICoreLibraryHandle = !IGICoreLibraryPath.IsEmpty() ? FPlatformProcess::GetDllHandle(*IGICoreLibraryPath) : nullptr;

    if (IGICoreLibraryHandle)
    {
        Ptr_nvigiInit = (PFun_nvigiInit*)FPlatformProcess::GetDllExport(IGICoreLibraryHandle, TEXT("nvigiInit"));
        Ptr_nvigiShutdown = (PFun_nvigiShutdown*)FPlatformProcess::GetDllExport(IGICoreLibraryHandle, TEXT("nvigiShutdown"));
        Ptr_nvigiLoadInterface = (PFun_nvigiLoadInterface*)FPlatformProcess::GetDllExport(IGICoreLibraryHandle, TEXT("nvigiLoadInterface"));
        Ptr_nvigiUnloadInterface = (PFun_nvigiUnloadInterface*)FPlatformProcess::GetDllExport(IGICoreLibraryHandle, TEXT("nvigiUnloadInterface"));
    }
    else
    {
        UE_LOG(LogIGISDK, Fatal, TEXT("IGI: Failed to load IGI core library... Aborting."));
        bInitialized = false;
        return;
    }

    if (!(Ptr_nvigiInit && Ptr_nvigiShutdown && Ptr_nvigiLoadInterface && Ptr_nvigiUnloadInterface))
    {
        UE_LOG(LogIGISDK, Fatal, TEXT("IGI: Failed to load IGI core library functions... Aborting."));
        bInitialized = false;
        return;
    }

    nvigi::Preferences Pref{};
#if UE_BUILD_SHIPPING
    Pref.showConsole = false;
#else
    Pref.showConsole = true;
#endif
    Pref.logLevel = nvigi::LogLevel::eDefault;

    const FString BaseDir = IPluginManager::Get().FindPlugin("IGI")->GetBaseDir();
    const FString IGIPluginPath = FPaths::Combine(*BaseDir, TEXT("ThirdParty/nvigi_pack/bin/x64/Release"));
    const auto IGIPluginPathUTF8 = StringCast<UTF8CHAR>(*IGIPluginPath);
    const char* IGIPluginPathCStr = reinterpret_cast<const char*>(IGIPluginPathUTF8.Get());
    Pref.utf8PathsToPlugins = &IGIPluginPathCStr;
    Pref.numPathsToPlugins = 1u;

    const auto IGILogsPathUTF8 = StringCast<UTF8CHAR>(*FPaths::ProjectLogDir());
    const char* IGILogsPathCStr = reinterpret_cast<const char*>(IGILogsPathUTF8.Get());
    Pref.utf8PathToLogsAndData = IGILogsPathCStr;

    Pref.logMessageCallback = IGILogCallback;

    nvigi::Result InitResult = (*Ptr_nvigiInit)(Pref, &IGIRequirements, nvigi::kSDKVersion);
    UE_LOG(LogIGISDK, Log, TEXT("IGI: Init result: %u"), InitResult);

    bInitialized = true;
}

FIGICore::~FIGICore()
{
    // Free the dll handle
    FPlatformProcess::FreeDllHandle(IGICoreLibraryHandle);
    IGICoreLibraryHandle = nullptr;
}

nvigi::Result FIGICore::LoadInterface(const nvigi::PluginID& Feature, const nvigi::UID& InterfaceType, nvigi::InferenceInterface** Interface, const UTF8CHAR* UTF8PathToPlugin)
{
    if (Ptr_nvigiLoadInterface == nullptr)
    {
        return nvigi::kResultInvalidState;
    }

    nvigi::InferenceInterface DummyInterface;

    nvigi::Result Result = (*Ptr_nvigiLoadInterface)(Feature, InterfaceType, DummyInterface.getVersion(), reinterpret_cast<void**>(Interface), BitCast<const char*>(UTF8PathToPlugin));
    UE_LOG(LogIGISDK, Log, TEXT("IGI: LoadInterface result: %u"), Result);

    return Result;
}

nvigi::Result FIGICore::UnloadInterface(const nvigi::PluginID& Feature, nvigi::InferenceInterface* Interface)
{
    if (Ptr_nvigiUnloadInterface == nullptr)
    {
        return nvigi::kResultInvalidState;
    }

    nvigi::Result Result = (*Ptr_nvigiUnloadInterface)(Feature, Interface);
    UE_LOG(LogIGISDK, Log, TEXT("IGI: UnloadInterface result: %u"), Result);

    return Result;
}
