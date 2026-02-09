# RmlUI Vulkan Backend on macOS (MoltenVK)

This document describes how to build and run RmlUI with the SDL_VK (Vulkan) backend on macOS using MoltenVK.

## Prerequisites

Install via Homebrew:

```bash
brew install sdl2 molten-vk vulkan-headers vulkan-loader freetype
```

## Build

```bash
cd /path/to/rmlui

cmake -B build -S . \
  -DRMLUI_SAMPLES=ON \
  -DRMLUI_BACKEND=SDL_VK \
  -DRMLUI_FONT_ENGINE=freetype \
  -DBUILD_SHARED_LIBS=ON

cmake --build build
```

## Run

RmlUI samples must be run from the `Samples/` directory for asset paths to resolve correctly:

```bash
cd Samples
DYLD_LIBRARY_PATH=/opt/homebrew/lib \
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
../build/rmlui_sample_demo
```

### Environment Variables

| Variable | Purpose |
|----------|---------|
| `DYLD_LIBRARY_PATH` | Helps locate libvulkan.dylib at runtime |
| `VK_ICD_FILENAMES` | Points Vulkan loader to MoltenVK ICD |

## macOS/MoltenVK Compatibility Fixes

The following changes were made to support MoltenVK on macOS:

### 1. Vulkan Platform Header (`Backends/RmlUi_Include_Vulkan.h`)

Use Metal surface extension instead of XCB:

```cpp
#if defined RMLUI_PLATFORM_MACOSX
    #define VK_USE_PLATFORM_METAL_EXT 1
#elif defined RMLUI_PLATFORM_LINUX
    #define VK_USE_PLATFORM_XCB_KHR 1
#endif
```

### 2. SDL Include Path (`Backends/RmlUi_Backend_SDL_VK.cpp`)

Fix SDL2 header include for Homebrew:

```cpp
#if SDL_MAJOR_VERSION >= 3
    #include <SDL3/SDL_vulkan.h>
#else
    #include <SDL_vulkan.h>  // Not <SDL2/SDL_vulkan.h>
#endif
```

### 3. Vulkan Renderer (`Backends/RmlUi_Renderer_VK.cpp`)

#### Instance Creation

Add portability enumeration extension and flag:

```cpp
// In CreatePropertiesFor_Instance():
#ifdef RMLUI_PLATFORM_MACOSX
    AddExtensionToInstance(instance_extension_names, instance_extension_properties,
                           VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

// In Initialize_Instance():
#ifdef RMLUI_PLATFORM_MACOSX
    info_instance.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#else
    info_instance.flags = 0;
#endif
```

#### Device Creation

Add portability subset extension:

```cpp
// In Initialize_Device():
#ifdef RMLUI_PLATFORM_MACOSX
    AddExtensionToDevice(device_extension_names, device_extension_properties,
                         "VK_KHR_portability_subset");
#endif
```

#### Unsupported Features

Disable features not available on Apple Silicon via MoltenVK:

```cpp
features_physical_device.fillModeNonSolid = true;
features_physical_device.fragmentStoresAndAtomics = true;
features_physical_device.vertexPipelineStoresAndAtomics = true;
features_physical_device.shaderImageGatherExtended = true;
#ifndef RMLUI_PLATFORM_MACOSX
    // MoltenVK on Apple Silicon doesn't support these features
    features_physical_device.pipelineStatisticsQuery = true;
    features_physical_device.wideLines = true;
#endif
```

## Troubleshooting

| Error | Solution |
|-------|----------|
| `Failed to load Vulkan Portability library` | Install `vulkan-loader`: `brew install vulkan-loader` |
| `failed to vkCreateInstance` | Ensure portability enumeration extension and flag are set |
| `failed to vkCreateDevice` | Add `VK_KHR_portability_subset` device extension |
| `VK_ERROR_FEATURE_NOT_PRESENT` | Disable unsupported features (wideLines, pipelineStatisticsQuery) |
| `Failed to pick the discrete gpu` | Normal on Apple Silicon - falls back to integrated GPU |
| Asset files not found | Run from `Samples/` directory |

## Verification

Run `vulkaninfo` to verify MoltenVK is working:

```bash
brew install vulkan-tools
vulkaninfo
```

Should show `VK_KHR_portability_enumeration` in instance extensions and your Apple GPU.
