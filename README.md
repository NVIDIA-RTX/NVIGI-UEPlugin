# NVIGI Unreal Engine 5.5 Sample

This project combines NVIGI and Epic Games' Unreal Engine 5.5 (https://www.unrealengine.com/) to create a sample app demonstrating an NVIGI AI integration.

> **IMPORTANT:**
> For important changes and bug fixes included in the current release, please see the release notes at the end of this document BEFORE use.

## What is the Sample?

The NVIDIA NVIGI (In-Game Inference) Unreal Engine Sample is a minimalistic code sample that is designed to show how one might integrate such AI features as chatbots (GPT/LLM) into an Unreal Engine application. The sample is exclusively provided in source form (C++ and Unreal Blueprint) with no binary executables included.

## Requirements
- Hardware:
  - Windows development system with an NVIDIA RTX 30x0/A6000 series or (preferably) 4080/4090 or RTX 5080/5090 with a minimum of 8GB and recommendation of 12GB VRAM.  Note that some plugins only support 40x0 and above (e.g. the TensorRT-LLM plugin), and will not be available on 30x0. Currently SDK supports only x64 CPUs.
- Software:
  - NVIDIA RTX driver
    - r551.78 or newer required for functionality 
    - For maximum performance, an r555.85 or newer driver is recommended
  - MS Visual Studio 2022 (2019 may be compatible, untested)
  - cmake (3.27.1 tested) installed in the command prompt path
  - Windows SDK including DirectX SDK.  Ensure that FXC.exe (https://learn.microsoft.com/en-us/windows/win32/direct3dtools/fxc) is in your PATH.

## Setup

In addition to the hardware and software requirements listed above, this code sample has two additional dependencies:
- A local installation of the Unreal Engine 5.5.x.
    - The most convenient method of procuring this is through the Epic Games Launcher; please visit their download page (https://www.unrealengine.com/en-US/download) and follow their instructions.
- A ZIP file with the NVIGI SDK pack.
    - You can obtain this ZIP file at https://developer.nvidia.com/rtx/in-game-inferencing.
    - As of this current release, the supported version of IGI is 1.1.0 (`nvigi_pack_1_1_0.zip`).

Please follow these steps:
1. Clone this repository to a local directory (which will be referred to as `<SAMPLE_ROOT>`)
2. In Windows Explorer, open `<SAMPLE_ROOT>\IGI_UE_Sample\Plugins\IGI\ThirdParty` and unzip the contents of the NVIGI SDK pack ZIP file there.
    - This should create a new folder named `nvigi_pack` inside that directory.
        - Please notice the file named `copy_nvigi_pack_here.txt` also in that folder as a cue.
    - Run the `setup_links.bat` script in the top-level of `nvigi_pack`, which will create required cross-directory links that are needed by the various components.
    - Download the `Nemotron Mini 4B` model using the `download.bat` file located in `\nvigi_pack\nvigi.models\nvigi.plugin.gpt.ggml\{8E31808B-C182-4016-9ED8-64804FF5B40D}`
    - KNOWN ISSUE: the `nvigi.plugin.gpt.onnxgenai.dml` NVIGI plugin currently crashes the application (Unreal Editor or packaged game). This will be fixed in future updates to this sample. A work-around is to simply rename the DLL file for that plugin (`<SAMPLE_ROOT>\IGI_UE_Sample\Plugins\IGI\ThirdParty\nvigi_pack\plugins\sdk\bin\x64\nvigi.plugin.gpt.onnxgenai.dml.dll`).
3. In Windows Explorer, go into `<SAMPLE_ROOT>\IGI_UE_Sample\` and right-click the file named `IGI_UE_Sample.uproject`. Select the "Generate Visual Studio project files" option in the right-click menu.
    - After the process is complete, a new Visual Studio solution file named `IGI_UE_Sample.sln` should appear.
4. Double-click the `IGI_UE_Sample.sln` file in order to open the code sample in Visual Studio.
5. Make sure the `Development Editor` build configuration is selected.
6. Make sure the `IGI_UE_Sample` project is selected as the startup project.
7. Press `F7` to compile the entire solution.

## Running the Sample

In Visual Studio, simply press `F5` to start the Unreal Editor. In addition to opening the project, the Unreal map named `GPTmap` will open.

From there, you can use the Editor to either:
1. Immediately start gameplay (within the viewport or a PIE window), or
2. Package the game, and then start it through the executable.

Once the game is open, a HUD user interface will appear over an empty map. Then:
1. Use the editable user prompt text box (focused by default) on the center left of the UI to type or paste the prompt for the chatbot.
    - Example: "_What are the three countries that consume the most rice?_"
2. (Optional) Use the editable system prompt text box on the top left of the UI to type or paste a description of the model's role.
    - Example 1: "_You are a helpful AI assistant. Answer the user's questions as best as you can_"
    - Example 2: "_You are a poet. All replies should be in poetic prose_"
3. Click the `Send to Language Model`
4. The chatbot's reply will appear in the read-only output text box at the bottom of the HUD

Feel free to repeat the last 3 steps as many times as you would like.

If running a packaged executable, please press `Alt`+`F4` to exit the sample.

## Inspecting the code

This sample's Visual Studio C++ project is organized into two parts:
1. An application-level Unreal Engine plugin with a single module, which contains the entirety of the NVIGI SDK integration.
    - The module includes a C++ Blueprint class that provides a single asynchronous entry point for taking a prompt string, running inference and returning an output string.
    - The build C# script for this module also details how to add all the DLLs and dependencies from the NVIGI SDK pack so that they are available both during gameplay and project packaging.
2. A minimalistic executable application that drives the sample.

This sample's Unreal Engine Blueprint code is organized into the following:
1. A widget Blueprint, which:
    - Specifies the UI elements and layout
    - Contains the logic for calling into the previously mentioned C++ Blueprint class with the contents of the input text boxes (and also update the output text box with the reply).
2. Blueprint class for the HUD, which creates and displays the widget described above.
3. Blueprint class for a Game Mode, which loads the HUD upon starting the application.

As explained at the beginning of this README, this sample is meant to be minimalistic, monolithic and brief. Further, it only shows usage of a single NVIGI feature (GPT chatbot). A more proper, modular and extensible integration should consider the following suggestions (or combinations thereof):
- Organizing the individual IGI plugins and their respective integrations into their own separate Unreal modules (or even into their own separate Unreal plugins).
- Organizing the IGI binary DLL files into their own specific directories (please refer to the `nvigi::Preferences::utf8PathToDependencies` field in `nvigi.h`).
- Checking whether IGI plugins have been loaded and are available through `nvigi::PluginAndSystemInformation` in `nvigi_types.h`.

For a more comprehensive code sample that not only shows NVIGI SDK integration (including usage of multiple NVIGI features) but that is closer to a shippable product, please refer to the ACE Unreal Engine plugin that is part of the NVIDIA ACE suite (https://developer.nvidia.com/ace).

## Release Notes:
- Initial release
    - Tested against Unreal Engine 5.5.2 and NVIGI SDK pack `nvigi_pack_1_1_0.zip`
    - KNOWN ISSUE: the `nvigi.plugin.gpt.onnxgenai.dml` NVIGI plugin currently crashes the application (Unreal Editor or packaged game). This will be fixed in future updates to this sample. A work-around is to simply rename the DLL file for that plugin (`<SAMPLE_ROOT>\IGI_UE_Sample\Plugins\IGI\ThirdParty\nvigi_pack\plugins\sdk\bin\x64\nvigi.plugin.gpt.onnxgenai.dml.dll`).
