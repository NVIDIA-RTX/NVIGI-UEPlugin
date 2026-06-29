// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "IGIModule.h"

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#include "IGICore.h"
#include "IGIGPT.h"
#include "IGILog.h"

#include "nvigi.h"
#include "nvigi_ai.h"
#include "nvigi_gpt.h"

#define LOCTEXT_NAMESPACE "FIGIModule"

class FIGIModule::Impl
{
public:
    Impl() {}

    virtual ~Impl() {}

    void StartupModule()
    {
        // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

        FString BaseDir = IPluginManager::Get().FindPlugin("IGI")->GetBaseDir();
        IGICoreLibraryPath = FPaths::Combine(*BaseDir, TEXT("ThirdParty/nvigi_pack/bin/x64/Release/nvigi.core.framework.dll"));
        IGIModelsPath = FPaths::Combine(*BaseDir, TEXT("ThirdParty/nvigi_pack/data/nvigi.models"));
    }

    void ShutdownModule()
    {
        // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
        // we call this function before unloading the module.

        if (Core)
        {
            UnloadIGICore();
        }
    }

    bool LoadIGICore()
    {
        FScopeLock Lock(&CS);

        Core = MakeUnique<FIGICore>(IGICoreLibraryPath);
        return (Core != nullptr) && (Core->IsInitialized());
    }

    bool UnloadIGICore()
    {
        FScopeLock Lock(&CS);

        GPT.Reset();
        Core.Reset();
        return true;
    }

    nvigi::Result LoadIGIFeature(const nvigi::PluginID& Feature, nvigi::InferenceInterface** Interface, const UTF8CHAR* UTF8PathToPlugin = nullptr)
    {
        FScopeLock Lock(&CS);

        return Core->LoadInterface(Feature, nvigi::InferenceInterface::s_type, Interface, UTF8PathToPlugin);
    }

    nvigi::Result UnloadIGIFeature(nvigi::PluginID Feature, nvigi::InferenceInterface* Interface)
    {
        FScopeLock Lock(&CS);

        return Core->UnloadInterface(Feature, Interface);
    }

    const FString GetModelsPath() const { return IGIModelsPath; }

    FIGIGPT* GetGPT(FIGIModule* module)
    {
        FScopeLock Lock(&CS);
        if(!GPT.IsValid())
        {
            GPT = MakeUnique<FIGIGPT>(module);
        }
        return GPT.Get();
    }

private:
    TUniquePtr<FIGICore> Core;
    TUniquePtr<FIGIGPT> GPT;

    FCriticalSection CS;
    FString IGICoreLibraryPath;
    FString IGIModelsPath;
};

// ----------------------------------

void FIGIModule::StartupModule()
{
    Pimpl = MakePimpl<FIGIModule::Impl>();
    Pimpl->StartupModule();
    UE_LOG(LogIGISDK, Log, TEXT("IGI module started"));
}

void FIGIModule::ShutdownModule()
{
    Pimpl->ShutdownModule();
    Pimpl.Reset();
    UE_LOG(LogIGISDK, Log, TEXT("IGI module shut down"));
}

bool FIGIModule::LoadIGICore()
{
    const bool Result{ Pimpl->LoadIGICore() };
    if (Result)
    {
        UE_LOG(LogIGISDK, Log, TEXT("IGI core loaded"));
    }
    else
    {
        UE_LOG(LogIGISDK, Fatal, TEXT("ERROR when loading IGI core"));
    }
    return Result;
}

bool FIGIModule::UnloadIGICore()
{
    const bool Result{ Pimpl->UnloadIGICore() };
    if (Result)
    {
        UE_LOG(LogIGISDK, Log, TEXT("IGI core unloaded"));
    }
    else
    {
        UE_LOG(LogIGISDK, Fatal, TEXT("ERROR when unloading IGI core"));
    }
    return Result;
}

nvigi::Result FIGIModule::LoadIGIFeature(const nvigi::PluginID& Feature, nvigi::InferenceInterface** Interface, const UTF8CHAR* UTF8PathToPlugin)
{
    const nvigi::Result Result{ Pimpl->LoadIGIFeature(Feature, Interface, UTF8PathToPlugin) };
    if (Result == nvigi::kResultOk)
    {
        UE_LOG(LogIGISDK, Log, TEXT("IGI feature loaded"));
    }
    else
    {
        UE_LOG(LogIGISDK, Fatal, TEXT("ERROR when loading IGI feature"));
    }
    return Result;
}

nvigi::Result FIGIModule::UnloadIGIFeature(const nvigi::PluginID& Feature, nvigi::InferenceInterface* Interface)
{
    const nvigi::Result Result{ Pimpl->UnloadIGIFeature(Feature, Interface) };
    if (Result == nvigi::kResultOk)
    {
        UE_LOG(LogIGISDK, Log, TEXT("IGI feature unloaded"));
    }
    else
    {
        UE_LOG(LogIGISDK, Fatal, TEXT("ERROR when unloading IGI feature"));
    }
    return Result;
}

const FString FIGIModule::GetModelsPath() const
{
    return Pimpl->GetModelsPath();
}

FIGIGPT* FIGIModule::GetGPT()
{
    return Pimpl->GetGPT(this);
}

FString GetIGIStatusString(nvigi::Result Result)
{
    switch (Result)
    {
    case nvigi::kResultOk:
        return FString(TEXT("Success"));
    case nvigi::kResultDriverOutOfDate:
        return FString(TEXT("Driver out of date"));
    case nvigi::kResultOSOutOfDate:
        return FString(TEXT("OS out of date"));
    case nvigi::kResultNoPluginsFound:
        return FString(TEXT("No plugins found"));
    case nvigi::kResultInvalidParameter:
        return FString(TEXT("Invalid parameter"));
    case nvigi::kResultNoSupportedHardwareFound:
        return FString(TEXT("No supported hardware found"));
    case nvigi::kResultMissingInterface:
        return FString(TEXT("Missing interface"));
    case nvigi::kResultMissingDynamicLibraryDependency:
        return FString(TEXT("Missing dynamic library dependency"));
    case nvigi::kResultInvalidState:
        return FString(TEXT("Invalid state"));
    case nvigi::kResultException:
        return FString(TEXT("Exception"));
    case nvigi::kResultJSONException:
        return FString(TEXT("JSON exception"));
    case nvigi::kResultRPCError:
        return FString(TEXT("RPC error"));
    case nvigi::kResultInsufficientResources:
        return FString(TEXT("Insufficient resources"));
    case nvigi::kResultNotReady:
        return FString(TEXT("Not ready"));
    case nvigi::kResultPluginOutOfDate:
        return FString(TEXT("Plugin out of date"));
    case nvigi::kResultDuplicatedPluginId:
        return FString(TEXT("Duplicate plugin ID"));
    case nvigi::kResultNoImplementation:
        return FString(TEXT("No implementation"));
    default:
        return FString(TEXT("invalid IGI error code"));
    }
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FIGIModule, IGI)
