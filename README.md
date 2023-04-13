# Description

This application is a real-time "simulation" of an hourglass, using Vulkan and a
block cellular automaton to represent and update the sand particles. Cell
transitions are performed via compute shaders and rendering happens in a
"bufferless" manner (buffers holding cell states are directly used as input
texture for fullscreen quad rendering). The block size of the cells is 2x2 and
[Margolus neighborhood](https://en.wikipedia.org/wiki/Block_cellular_automaton) is used as the partitioning scheme.

# Features

-   Cell transitions solely via compute shaders
-   Different grid generation methods (hourglass, random patterns, etc.)
-   Cell grids are directly used as input textures for fullscreen quad rendering,
    so rendering itself is "bufferless"
-   Several configurable settings (see [ApplicationDefines.hpp](src/ApplicationDefines.hpp))

![Demo of cell transitions](https://gitlab.com/MaxMutant/readme-assets/-/raw/main/vulkan-hourglass/demo.gif)

# Getting Started

## Prerequisites

-   Make
-   clang++ (with C++17 support)
-   Vulkan SDK installed and properly configured
-   [shaderc](https://github.com/google/shaderc) (glslc for shader compilation)
-   [glfw](https://github.com/glfw/glfw)
-   GPU with graphics/compute and present capabilities (+ support for
    'VK<sub>KHR</sub><sub>surface</sub>' and your platforms 'VK<sub>KHR</sub><sub>\*</sub><sub>surface</sub>' extension)

Example installation with OpenSUSE:

    sudo zypper in -t pattern devel_vulkan # vulkan + shaderc
    sudo zypper clang libglfw-devel


## Build and Run

    # Default release build and run:
    make
    ./bin/release/vulkan_hourglass

    # For debug build:
    make mode=debug
    ./bin/debug/vulkan_hourglass

    # To enable validation layers use:
    make CPPFLAGS="-DVALIDATION_LAYERS"


## Making changes

All settings are defined within [ApplicationDefines.hpp](src/ApplicationDefines.hpp) and
static asserts are in place to prevent misconfiguration.

Furthermore, the generation method for the initial state can be adapted in
[main.cpp](src/main.cpp).

# Noteworthy

## Use of Graphics Pipeline instead of Blit to Framebuffer

In the current implementation the compute shader works solely on `uint` storage
buffers, which are used as "texture" input for rendering (via `VkBufferView`).

An alternative approach would be to directly work with RGB textures in the
computer shader an simply [blit](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCmdBlitImage.html) the result to framebuffer images. The benefit of
this approach would be that no graphics pipeline is needed. However, I decided
against it for the sake of learning to work with dependencies between compute
and graphics pipelines, as well as having a clear separation between compute
(cell updates) and rendering (cell state to color mapping).

## Use of Hard-Coded Transition Table in Shader

I was concerned that having the hard-coded transition table would decrease
performance due to filling up the device's registers. On my setup (integrated
GPU only) no measurable performance differences between this approach and using
a uniform buffer for the transition table were detected.

## Device Limits

No device limits should be exceeded with the default settings, as the
requirements are well bellow the declared minimum limits in the specification.

In case the limits are exceeded due to reconfiguration, the application prints
warnings and will abort execution.


# Possible Improvements

The following things are improvements I would like to look further into:

-   Async compute
-   User interactivity (change update speed at runtime, rotate grid, add/remove
    sand via mouse, etc.)
-   Multiple frames in flight (wasn't a priority so far, as presented output is
    anyhow dependent on compute update)

# Acknowledgment

-   [Vulkan](https://www.vulkan.org/) - Graphics and Compute API
-   [Vulkan tutorial on rendering a fullscreen quad without buffers - Sascha Willems](https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/)
-   [Shader Hash Function](https://www.shadertoy.com/view/llGSzw) for handling cell transition with probability
