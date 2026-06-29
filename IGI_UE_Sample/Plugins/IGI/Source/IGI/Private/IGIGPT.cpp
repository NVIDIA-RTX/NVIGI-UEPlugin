// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "IGIGPT.h"

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "ID3D12DynamicRHI.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "IGIModule.h"
#include "IGILog.h"

#include "nvigi.h"
#include "nvigi_ai.h"
#include "nvigi_gpt.h"
#include "nvigi_stl_helpers.h"
#include "nvigi_struct.h"

#pragma warning( push )
#pragma warning( disable : 5257 )
#include "nvigi_d3d12.h"
#pragma warning( pop )

#include <condition_variable>
#include <thread>
#include <mutex>

namespace
{
    constexpr const char* const GGUF_MODEL_MINITRON{ "{8E31808B-C182-4016-9ED8-64804FF5B40D}" };

    constexpr std::size_t VRAM_BUDGET_RECOMMENDATION{ 1024 * 12 };
    constexpr std::size_t THREAD_NUM_RECOMMENDATION{ 1 }; // Recommended number of threads for CiG
    constexpr std::size_t CONTEXT_SIZE_RECOMMENDATION{ 4096 };
}

class FIGIGPT::Impl
{
public:
    Impl(FIGIModule* IGIModule)
        : IGIModulePtr(IGIModule)
    {
        nvigi::Result Result = nvigi::kResultOk;

        IGIModulePtr->LoadIGIFeature(nvigi::plugin::gpt::ggml::cuda::kId, &GPTInterface, nullptr);

        nvigi::GPTCreationParameters params{};

        nvigi::CommonCreationParameters common{};
        auto ConvertedString = StringCast<UTF8CHAR>(*IGIModulePtr->GetModelsPath());
        common.utf8PathToModels = reinterpret_cast<const char*>(ConvertedString.Get());
        common.numThreads = THREAD_NUM_RECOMMENDATION;
        common.vramBudgetMB = VRAM_BUDGET_RECOMMENDATION;
        common.modelGUID = GGUF_MODEL_MINITRON;
        Result = params.chain(common);
        if (Result != nvigi::kResultOk)
        {
            UE_LOG(LogIGISDK, Error, TEXT("Unable to chain common parameters; cannot use CiG: %s"), *GetIGIStatusString(Result));
            GPTInstance = nullptr;
        }

        nvigi::D3D12Parameters d3d12Params{};
        if (GDynamicRHI &&
            GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12)
        {
            ID3D12DynamicRHI* RHI = static_cast<ID3D12DynamicRHI*>(GDynamicRHI);
            if (RHI)
            {
                ID3D12CommandQueue* CmdQ = RHI->RHIGetCommandQueue();
                constexpr uint32 RHI_DEVICE_INDEX = 0u;
                ID3D12Device* D3D12Device = RHI->RHIGetDevice(RHI_DEVICE_INDEX);

                if (CmdQ && D3D12Device)
                {
                    d3d12Params.device = D3D12Device;
                    d3d12Params.queue = CmdQ;

                    Result = params.chain(d3d12Params);
                    if (Result != nvigi::kResultOk)
                    {
                        UE_LOG(LogIGISDK, Error, TEXT("Unable to chain D3D12 parameters; cannot use CiG: %s"), *GetIGIStatusString(Result));
                        GPTInstance = nullptr;
                    }
                }
                else
                {
                    UE_LOG(LogIGISDK, Error, TEXT("Unable to retrieve D3D12 device and command queue from UE; cannot use CiG: %s"), *GetIGIStatusString(Result));
                    GPTInstance = nullptr;
                }
            }
            else
            {
                UE_LOG(LogIGISDK, Error, TEXT("Unable to retrieve RHI instance from UE; cannot use CiG: %s"), *GetIGIStatusString(Result));
                GPTInstance = nullptr;
            }
        }
        else
        {
            UE_LOG(LogIGISDK, Log, TEXT("UE not using D3D12; cannot use CiG: %s"), *GetIGIStatusString(Result));
            GPTInstance = nullptr;
        }

        // For the CUDA backend, createInstance brings up CUDA runtime DLLs 
        // (cuBLAS/cuBLASLt/cuDART) by bare name; point the OS at our bundled copies 
        // so resolution doesn't depend on a system CUDA install being on PATH.
        const FString IGIPluginBinPath = FPaths::Combine(
            *IPluginManager::Get().FindPlugin("IGI")->GetBaseDir(),
            TEXT("ThirdParty/nvigi_pack/bin/x64/Release"));
        FPlatformProcess::PushDllDirectory(*IGIPluginBinPath);
        Result = GPTInterface->createInstance(params, &GPTInstance);
        FPlatformProcess::PopDllDirectory(*IGIPluginBinPath);
        if (Result != nvigi::kResultOk)
        {
            UE_LOG(LogIGISDK, Fatal, TEXT("Unable to create gpt.ggml.cuda instance: %s"), *GetIGIStatusString(Result));
            GPTInstance = nullptr;
        }
    }

    virtual ~Impl()
    {
        if (GPTInstance != nullptr)
        {
            GPTInterface->destroyInstance(GPTInstance);
            GPTInstance = nullptr;
        }

        if (IGIModulePtr)
        {
            IGIModulePtr->UnloadIGIFeature(nvigi::plugin::gpt::ggml::cuda::kId, GPTInterface);
            IGIModulePtr = nullptr;
        }
    }

    FString Evaluate(const FString& SystemPrompt, const FString& UserPrompt, const FString& AssistantPrompt)
    {
        FScopeLock Lock(&CS);

        struct BasicCallbackCtx
        {
            std::mutex callbackMutex;
            std::condition_variable callbackCV;
            std::atomic<nvigi::InferenceExecutionState> callbackState = nvigi::kInferenceExecutionStateDataPending;
            FString gptOutput;
        };
        BasicCallbackCtx cbkCtx;

        auto completionCallback = [](const nvigi::InferenceExecutionContext* ctx, nvigi::InferenceExecutionState state, void* data) -> nvigi::InferenceExecutionState
            {
                if (!data)
                    return nvigi::kInferenceExecutionStateInvalid;

                auto cbkCtx = (BasicCallbackCtx*)data;
                std::scoped_lock lck(cbkCtx->callbackMutex);

                // Outputs from GPT
                auto slots = ctx->outputs;
                const nvigi::InferenceDataText* text{};
                slots->findAndValidateSlot(nvigi::kGPTDataSlotResponse, &text);
                auto response = FString(StringCast<UTF8CHAR>(text->getUTF8Text()));
                if (response.Find("<JSON>") != INDEX_NONE)
                {
                    auto cpuBuffer = castTo<nvigi::CpuData>(text->utf8Text);
                    ((uint8_t*)cpuBuffer->buffer)[0] = 0;
                    cpuBuffer->sizeInBytes = 0;
                }
                else
                {
                    cbkCtx->gptOutput += response;
                }

                cbkCtx->callbackState = state;
                cbkCtx->callbackCV.notify_one();

                return state;
            };

        auto SystemPromptUTF = StringCast<UTF8CHAR>(*SystemPrompt);
        nvigi::InferenceDataTextSTLHelper SystemPromptData(reinterpret_cast<const char*>(SystemPromptUTF.Get()));

        auto UserPromptUTF = StringCast<UTF8CHAR>(*UserPrompt);
        nvigi::InferenceDataTextSTLHelper UserPromptData(reinterpret_cast<const char*>(UserPromptUTF.Get()));

        auto AssistantPromptUTF = StringCast<UTF8CHAR>(*AssistantPrompt);
        nvigi::InferenceDataTextSTLHelper AssistantPromptData(reinterpret_cast<const char*>(AssistantPromptUTF.Get()));

        TArray<nvigi::InferenceDataSlot> inSlots = {
            {nvigi::kGPTDataSlotUser, UserPromptData}
        };
        if (SystemPrompt.Len() > 0u)
        {
            inSlots.Add({ nvigi::kGPTDataSlotSystem, SystemPromptData });
        }
        if (AssistantPrompt.Len() > 0u)
        {
            inSlots.Add({ nvigi::kGPTDataSlotAssistant, AssistantPromptData });
        }

        nvigi::InferenceDataSlotArray inputs = { static_cast<size_t>(inSlots.Num()), inSlots.GetData() };

        // Parameters
        nvigi::GPTRuntimeParameters runtime{};
        runtime.seed = -1;
        runtime.tokensToPredict = 200;
        runtime.interactive = false;

        nvigi::InferenceExecutionContext gptCtx{};
        nvigi::InferenceInstance* instance = GPTInstance;
        gptCtx.instance = instance;
        gptCtx.callback = completionCallback;
        gptCtx.callbackUserData = &cbkCtx;
        gptCtx.inputs = &inputs;
        gptCtx.runtimeParameters = runtime;

        cbkCtx.callbackState = nvigi::kInferenceExecutionStateDataPending;

        instance->evaluateAsync(&gptCtx);

        {
            std::unique_lock lck(cbkCtx.callbackMutex);
            cbkCtx.callbackCV.wait(lck, [&cbkCtx]()
                {
                    return cbkCtx.callbackState != nvigi::kInferenceExecutionStateDataPending;
                });
        }

        FString response(cbkCtx.gptOutput);

        return response;
    }

private:
    FCriticalSection CS;

    // Non-owning ptr
    FIGIModule* IGIModulePtr;

    nvigi::IGeneralPurposeTransformer* GPTInterface{ nullptr };
    nvigi::InferenceInstance* GPTInstance{ nullptr };
};

// ----------------------------------

FIGIGPT::FIGIGPT(FIGIModule* IGIModule)
{
    Pimpl = MakePimpl<FIGIGPT::Impl>(IGIModule);
}

FIGIGPT::~FIGIGPT() {}

FString FIGIGPT::Evaluate(const FString& SystemPrompt, const FString& UserPrompt, const FString& AssistantPrompt)
{
    return Pimpl->Evaluate(SystemPrompt, UserPrompt, AssistantPrompt);
}
