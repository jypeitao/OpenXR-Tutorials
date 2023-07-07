#include "GraphicsAPIs.h"

bool CheckGraphicsAPI_TypeIsValidForPlatform(GraphicsAPI_Type type) {
#if defined(XR_USE_PLATFORM_WIN32)
    return (type == D3D11) || (type == D3D12) || (type == OPENGL) || (type == VULKAN);
#endif
#if defined(XR_USE_PLATFORM_XLIB) || defined(XR_USE_PLATFORM_XCB) || defined(XR_USE_PLATFORM_WAYLAND)
    return (type == OPENGL) || (type == VULKAN);
#endif
#if defined(XR_USE_PLATFORM_ANDROID) || defined(XR_USE_PLATFORM_XCB) || defined(XR_USE_PLATFORM_WAYLAND)
    return (type == OPENGL_ES) || (type == VULKAN);
#endif
}

const char *GetGraphicsAPIInstanceExtensionString(GraphicsAPI_Type type) {
    if (type == D3D11) {
#if defined(XR_USE_GRAPHICS_API_D3D11)
        return XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
#endif
    } else if (type == D3D12) {
#if defined(XR_USE_GRAPHICS_API_D3D12)
        return XR_KHR_D3D12_ENABLE_EXTENSION_NAME;
#endif
    } else if (type == OPENGL) {
#if defined(XR_USE_GRAPHICS_API_OPENGL)
        return XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
#endif
    } else if (type == OPENGL_ES) {
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
        return XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
#endif
    } else if (type == VULKAN) {
#if defined(XR_USE_GRAPHICS_API_VULKAN)
        return XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
#endif
    } else {
        std::cout << "ERROR: Unknown Graphics API." << std::endl;
        DEBUG_BREAK;
        return nullptr;
    }
    return nullptr;
}

// GraphicsAPI
int64_t GraphicsAPI::SelectSwapchainFormat(const std::vector<int64_t> &formats) {
    const std::vector<int64_t> &supportSwapchainFormats = GetSupportedSwapchainFormats();

    const std::vector<int64_t>::const_iterator &swapchainFormatIt = std::find_first_of(formats.begin(), formats.end(),
                                                                                       std::begin(supportSwapchainFormats), std::end(supportSwapchainFormats));
    if (swapchainFormatIt == formats.end()) {
        std::cout << "ERROR: Unable to find supported Swapchain Format" << std::endl;
        DEBUG_BREAK;
        return 0;
    }

    return *swapchainFormatIt;
}

// D3D11
#if defined(XR_USE_GRAPHICS_API_D3D11)

#define D3D11_CHECK(x, y)                                                                         \
    {                                                                                             \
        HRESULT result = (x);                                                                     \
        if (FAILED(result)) {                                                                     \
            std::cout << "ERROR: D3D11: " << std::hex << "0x" << result << std::dec << std::endl; \
            std::cout << "ERROR: D3D11: " << y << std::endl;                                      \
        }                                                                                         \
    }

#define D3D11_SAFE_RELEASE(p) \
    {                         \
        if (p) {              \
            (p)->Release();   \
            (p) = nullptr;    \
        }                     \
    }

GraphicsAPI_D3D11::GraphicsAPI_D3D11(XrInstance xrInstance, XrSystemId systemId) {
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetD3D11GraphicsRequirementsKHR), "Failed to get InstanceProcAddr.");
    XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    OPENXR_CHECK(xrGetD3D11GraphicsRequirementsKHR(xrInstance, systemId, &graphicsRequirements), "Failed to get Graphics Requirements for D3D11.");

    D3D11_CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Failed to create DXGI factory.");

    UINT i = 0;
    IDXGIAdapter *adapter = nullptr;
    DXGI_ADAPTER_DESC adapterDesc = {};
    while (factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        adapter->GetDesc(&adapterDesc);
        if (adapterDesc.AdapterLuid.LowPart == graphicsRequirements.adapterLuid.LowPart && adapterDesc.AdapterLuid.HighPart == graphicsRequirements.adapterLuid.HighPart) {
            break;  // We have the matching adapter that OpenXR wants.
        }
    }

    D3D11_CHECK(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, 0, 0, &graphicsRequirements.minFeatureLevel, 1, D3D11_SDK_VERSION, &device, nullptr, &immediateContext), "Failed to create D3D11 Device.");
};

GraphicsAPI_D3D11::~GraphicsAPI_D3D11() {
    D3D11_SAFE_RELEASE(immediateContext);
    D3D11_SAFE_RELEASE(device);
    D3D11_SAFE_RELEASE(factory);
}

void *GraphicsAPI_D3D11::GetGraphicsBinding() {
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    graphicsBinding.device = device;
    return &graphicsBinding;
}

XrSwapchainImageBaseHeader *GraphicsAPI_D3D11::AllocateSwapchainImageData(uint32_t count) {
    swapchainImages.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    return reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchainImages.data());
}

void *GraphicsAPI_D3D11::CreateImage(const ImageCreateInfo &imageCI) {
    if (imageCI.dimension == 1) {
        ID3D11Texture1D *texture = nullptr;
        D3D11_TEXTURE1D_DESC desc;
        desc.Width = imageCI.width;
        desc.MipLevels = imageCI.mipLevels;
        desc.ArraySize = imageCI.arrayLayers;
        desc.Format = (DXGI_FORMAT)imageCI.format;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = (imageCI.sampled ? D3D11_BIND_SHADER_RESOURCE : 0) | (imageCI.colorAttachment ? D3D11_BIND_RENDER_TARGET : 0) | (imageCI.depthAttachment ? D3D11_BIND_DEPTH_STENCIL : 0);
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = imageCI.mipLevels > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        D3D11_CHECK(device->CreateTexture1D(&desc, nullptr, &texture), "Failed to create Texture1D.");
        return texture;
    } else if (imageCI.dimension == 2) {
        ID3D11Texture2D *texture = nullptr;
        D3D11_TEXTURE2D_DESC desc;
        desc.Width = imageCI.width;
        desc.Height = imageCI.height;
        desc.MipLevels = imageCI.mipLevels;
        desc.ArraySize = imageCI.arrayLayers;
        desc.Format = (DXGI_FORMAT)imageCI.format;
        desc.SampleDesc = {imageCI.sampleCount, 0};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = (imageCI.sampled ? D3D11_BIND_SHADER_RESOURCE : 0) | (imageCI.colorAttachment ? D3D11_BIND_RENDER_TARGET : 0) | (imageCI.depthAttachment ? D3D11_BIND_DEPTH_STENCIL : 0);
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = (imageCI.cubemap ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0) | (imageCI.mipLevels > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);
        D3D11_CHECK(device->CreateTexture2D(&desc, nullptr, &texture), "Failed to create Texture2D.");
        return texture;
    } else if (imageCI.dimension == 3) {
        ID3D11Texture3D *texture = nullptr;
        D3D11_TEXTURE3D_DESC desc;
        desc.Width = imageCI.width;
        desc.Height = imageCI.height;
        desc.Depth = imageCI.depth;
        desc.MipLevels = imageCI.mipLevels;
        desc.Format = (DXGI_FORMAT)imageCI.format;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = (imageCI.sampled ? D3D11_BIND_SHADER_RESOURCE : 0) | (imageCI.colorAttachment ? D3D11_BIND_RENDER_TARGET : 0) | (imageCI.depthAttachment ? D3D11_BIND_DEPTH_STENCIL : 0);
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = imageCI.mipLevels > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        D3D11_CHECK(device->CreateTexture3D(&desc, nullptr, &texture), "Failed to create Texture3D.");
        return texture;
    } else {
        DEBUG_BREAK;
        std::cout << "ERROR: D3D11: Unknown Dimension for CreateImage: " << imageCI.dimension << std::endl;
        return nullptr;
    }
}

void GraphicsAPI_D3D11::DestroyImage(void *&image) {
    ID3D11Resource *d3d11Image = reinterpret_cast<ID3D11Resource *>(image);
    D3D11_SAFE_RELEASE(d3d11Image);
    image = nullptr;
}

const std::vector<int64_t> GraphicsAPI_D3D11::GetSupportedSwapchainFormats() {
    return {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
}
#endif

// D3D12
#if defined(XR_USE_GRAPHICS_API_D3D12)

#define D3D12_CHECK(x, y)                                                                         \
    {                                                                                             \
        HRESULT result = (x);                                                                     \
        if (FAILED(result)) {                                                                     \
            std::cout << "ERROR: D3D12: " << std::hex << "0x" << result << std::dec << std::endl; \
            std::cout << "ERROR: D3D12: " << y << std::endl;                                      \
        }                                                                                         \
    }

#define D3D12_SAFE_RELEASE(p) \
    {                         \
        if (p) {              \
            (p)->Release();   \
            (p) = nullptr;    \
        }                     \
    }

GraphicsAPI_D3D12::GraphicsAPI_D3D12(XrInstance xrInstance, XrSystemId systemId) {
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetD3D12GraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetD3D12GraphicsRequirementsKHR), "Failed to get InstanceProcAddr.");
    XrGraphicsRequirementsD3D12KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    OPENXR_CHECK(xrGetD3D12GraphicsRequirementsKHR(xrInstance, systemId, &graphicsRequirements), "Failed to get Graphics Requirements for D3D12.");

    D3D12_CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Failed to create DXGI factory.");

    UINT i = 0;
    IDXGIAdapter1 *adapter = nullptr;
    DXGI_ADAPTER_DESC adapterDesc = {};
    while (factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        adapter->GetDesc(&adapterDesc);
        if (adapterDesc.AdapterLuid.LowPart == graphicsRequirements.adapterLuid.LowPart && adapterDesc.AdapterLuid.HighPart == graphicsRequirements.adapterLuid.HighPart) {
            break;  // We have the matching adapter that OpenXR wants.
        }
    }

    D3D12_CHECK(D3D12CreateDevice(adapter, graphicsRequirements.minFeatureLevel, IID_PPV_ARGS(&device)), "Failed to create D3D12 Device.");

    D3D12_COMMAND_QUEUE_DESC commandQueueDesc;
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    commandQueueDesc.Priority = 0;
    commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    commandQueueDesc.NodeMask = 0;
    D3D12_CHECK(device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&queue)), "Failed to create D3D12 Command Queue.");
}

GraphicsAPI_D3D12 ::~GraphicsAPI_D3D12() {
    D3D12_SAFE_RELEASE(device);
    D3D12_SAFE_RELEASE(queue);
}

void *GraphicsAPI_D3D12::GetGraphicsBinding() {
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    graphicsBinding.device = device;
    graphicsBinding.queue = queue;
    return &graphicsBinding;
}

XrSwapchainImageBaseHeader *GraphicsAPI_D3D12::AllocateSwapchainImageData(uint32_t count) {
    swapchainImages.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    return reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchainImages.data());
}

void *GraphicsAPI_D3D12::CreateImage(const ImageCreateInfo &imageCI) {
    ID3D12Resource *texture = nullptr;
    D3D12_RESOURCE_DESC desc;
    desc.Dimension = D3D12_RESOURCE_DIMENSION(imageCI.dimension + 1);
    desc.Alignment = 0;
    desc.Width = imageCI.width;
    desc.Height = imageCI.height;
    desc.DepthOrArraySize = imageCI.dimension == 3 ? imageCI.depth : imageCI.arrayLayers;
    desc.MipLevels = imageCI.mipLevels;
    desc.Format = (DXGI_FORMAT)imageCI.format;
    desc.SampleDesc = {imageCI.sampleCount, 0};
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE | (imageCI.colorAttachment ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAGS(0)) | (imageCI.depthAttachment ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAGS(0));

    D3D12_CLEAR_VALUE clear = {};
    bool useClear = false;
    if (useClear = BitwiseCheck(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) {
        clear.Format = desc.Format;
        clear.Color[0] = 0.0f;
        clear.Color[1] = 0.0f;
        clear.Color[2] = 0.0f;
        clear.Color[3] = 0.0f;
    }
    if (useClear = BitwiseCheck(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
        clear.Format = desc.Format;
        clear.DepthStencil = {0.0f, 0};
    }

    ID3D12Heap *heap = nullptr;
    D3D12_RESOURCE_ALLOCATION_INFO allocInfo = device->GetResourceAllocationInfo(0, 1, &desc);
    D3D12_HEAP_DESC heapDesc;
    heapDesc.SizeInBytes = allocInfo.SizeInBytes;
    heapDesc.Properties = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
    heapDesc.Alignment = allocInfo.Alignment;
    heapDesc.Flags = D3D12_HEAP_FLAG_NONE;
    D3D12_CHECK(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)), "Failed to create Heap.")

    D3D12_CHECK(device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON, useClear ? &clear : nullptr, IID_PPV_ARGS(&texture)), "Failed to create Texture.");
    imageResources[texture] = heap;

    return texture;
}
void GraphicsAPI_D3D12::DestroyImage(void *&image) {
    ID3D12Resource *d3d12Image = reinterpret_cast<ID3D12Resource *>(image);
    ID3D12Heap *heap = imageResources[d3d12Image];
    imageResources.erase(d3d12Image);
    D3D12_SAFE_RELEASE(heap);
    D3D12_SAFE_RELEASE(d3d12Image);
    image = nullptr;
}

const std::vector<int64_t> GraphicsAPI_D3D12::GetSupportedSwapchainFormats() {
    return {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
}
#endif

// OpenGL
#if defined(XR_USE_GRAPHICS_API_OPENGL)
GraphicsAPI_OpenGL::GraphicsAPI_OpenGL(XrInstance xrInstance, XrSystemId systemId) {
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetOpenGLGraphicsRequirementsKHR), "Failed to get InstanceProcAddr.");
    XrGraphicsRequirementsOpenGLKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    OPENXR_CHECK(xrGetOpenGLGraphicsRequirementsKHR(xrInstance, systemId, &graphicsRequirements), "Failed to get Graphics Requirements for OpenGL.");

    // https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/f122f9f1fc729e2dc82e12c3ce73efa875182854/src/tests/hello_xr/graphicsplugin_opengl.cpp#L103-L121
    // Initialize the gl extensions. Note we have to open a window.
    ksDriverInstance driverInstance{};
    ksGpuQueueInfo queueInfo{};
    ksGpuSurfaceColorFormat colorFormat{KS_GPU_SURFACE_COLOR_FORMAT_B8G8R8A8};
    ksGpuSurfaceDepthFormat depthFormat{KS_GPU_SURFACE_DEPTH_FORMAT_D24};
    ksGpuSampleCount sampleCount{KS_GPU_SAMPLE_COUNT_1};
    if (!ksGpuWindow_Create(&window, &driverInstance, &queueInfo, 0, colorFormat, depthFormat, sampleCount, 640, 480, false)) {
        std::cout << "ERROR: OPENGL: Failed to create Context." << std::endl;
    }

    GLint glMajorVersion = 0;
    GLint glMinorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &glMinorVersion);

    const XrVersion glApiVersion = XR_MAKE_VERSION(glMajorVersion, glMinorVersion, 0);
    if (graphicsRequirements.minApiVersionSupported > glApiVersion) {
        std::cout << "ERROR: OPENGL: The created OpenGL version doesn't meet the minimum requried API version for OpenXR." << std::endl;
    }
}
GraphicsAPI_OpenGL::~GraphicsAPI_OpenGL() {
    ksGpuWindow_Destroy(&window);
}

void *GraphicsAPI_OpenGL::GetGraphicsBinding() {
    // https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/f122f9f1fc729e2dc82e12c3ce73efa875182854/src/tests/hello_xr/graphicsplugin_opengl.cpp#L123-L144
#if defined(XR_USE_PLATFORM_WIN32)
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    graphicsBinding.hDC = window.context.hDC;
    graphicsBinding.hGLRC = window.context.hGLRC;
#elif defined(XR_USE_PLATFORM_XLIB)
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
    graphicsBinding.xDisplay = window.context.xDisplay;
    graphicsBinding.visualid = window.context.visualid;
    graphicsBinding.glxFBConfig = window.context.glxFBConfig;
    graphicsBinding.glxDrawable = window.context.glxDrawable;
    graphicsBinding.glxContext = window.context.glxContext;
#elif defined(XR_USE_PLATFORM_XCB)
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_XCB_KHR};
    // TODO: Still missing the platform adapter, and some items to make this usable.
    graphicsBinding.connection = window.connection;
    // m_graphicsBinding.screenNumber = window.context.screenNumber;
    // m_graphicsBinding.fbconfigid = window.context.fbconfigid;
    graphicsBinding.visualid = window.context.visualid;
    graphicsBinding.glxDrawable = window.context.glxDrawable;
    // m_graphicsBinding.glxContext = window.context.glxContext;
#elif defined(XR_USE_PLATFORM_WAYLAND)
    // TODO: Just need something other than NULL here for now (for validation).  Eventually need
    //       to correctly put in a valid pointer to an wl_display
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND};
    graphicsBinding.display = reinterpret_cast<wl_display *>(0xFFFFFFFF);
#endif
    return &graphicsBinding;
}

XrSwapchainImageBaseHeader *GraphicsAPI_OpenGL::AllocateSwapchainImageData(uint32_t count) {
    swapchainImages.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    return reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchainImages.data());
}

void *GraphicsAPI_OpenGL::CreateImage(const ImageCreateInfo &imageCI) {
    GLuint texture = 0;
    glGenTextures(1, &texture);

    GLenum target = 0;
    if (imageCI.dimension == 1) {
        if (imageCI.arrayLayers > 1) {
            target = GL_TEXTURE_1D_ARRAY;
        } else {
            target = GL_TEXTURE_1D;
        }
    } else if (imageCI.dimension == 2) {
        if (imageCI.cubemap) {
            if (imageCI.arrayLayers > 6) {
                target = GL_TEXTURE_CUBE_MAP_ARRAY;
            } else {
                target = GL_TEXTURE_CUBE_MAP;
            }
        } else {
            if (imageCI.sampleCount > 1) {
                if (imageCI.arrayLayers > 1) {
                    target = GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
                } else {
                    target = GL_TEXTURE_2D_MULTISAMPLE;
                }
            } else {
                if (imageCI.arrayLayers > 1) {
                    target = GL_TEXTURE_2D_ARRAY;
                } else {
                    target = GL_TEXTURE_2D;
                }
            }
        }
    } else if (imageCI.dimension == 3) {
        target = GL_TEXTURE_3D;
    } else {
        DEBUG_BREAK;
        std::cout << "ERROR: OPENGL: Unknown Dimension for CreateImage: " << imageCI.dimension << std::endl;
        return nullptr;
    }

    glBindTexture(target, texture);

    if (target == GL_TEXTURE_1D) {
        // glTexStorage1D() is not availble - Poor work around.
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, imageCI.mipLevels, imageCI.format, imageCI.width, 1);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else if (target == GL_TEXTURE_2D) {
        glTexStorage2D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height);
    } else if (target == GL_TEXTURE_2D_MULTISAMPLE) {
        glTexStorage2DMultisample(target, imageCI.sampleCount, imageCI.format, imageCI.width, imageCI.height, GL_TRUE);
    } else if (target == GL_TEXTURE_3D) {
        glTexStorage3D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height, imageCI.depth);
    } else if (target == GL_TEXTURE_CUBE_MAP) {
        glTexStorage2D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height);
    } else if (target == GL_TEXTURE_1D_ARRAY) {
        glTexStorage2D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.arrayLayers);
    } else if (target == GL_TEXTURE_2D_ARRAY) {
        glTexStorage3D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height, imageCI.arrayLayers);
    } else if (target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        glTexStorage3DMultisample(target, imageCI.sampleCount, imageCI.format, imageCI.width, imageCI.height, imageCI.arrayLayers, GL_TRUE);
    } else if (target == GL_TEXTURE_CUBE_MAP_ARRAY) {
        glTexStorage3D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height, imageCI.arrayLayers);
    }

    glBindTexture(target, 0);

    return (void *)(uint64_t)texture;
}
void GraphicsAPI_OpenGL::DestroyImage(void *&image) {
    GLuint texture = (GLuint)(uint64_t)image;
    glDeleteTextures(1, &texture);
    image = nullptr;
}

const std::vector<int64_t> GraphicsAPI_OpenGL::GetSupportedSwapchainFormats() {
    // https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/f122f9f1fc729e2dc82e12c3ce73efa875182854/src/tests/hello_xr/graphicsplugin_opengl.cpp#L229-L236
    return {
        GL_RGB10_A2,
        GL_RGBA16F,
        // The two below should only be used as a fallback, as they are linear color formats without enough bits for color
        // depth, thus leading to banding.
        GL_RGBA8,
        GL_RGBA8_SNORM,
    };
}
#endif

// OpenGL ES
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
GraphicsAPI_OpenGL_ES::GraphicsAPI_OpenGL_ES(XrInstance xrInstance, XrSystemId systemId) {
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetOpenGLESGraphicsRequirementsKHR), "Failed to get InstanceProcAddr.");
    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    OPENXR_CHECK(xrGetOpenGLESGraphicsRequirementsKHR(xrInstance, systemId, &graphicsRequirements), "Failed to get Graphics Requirements for OpenGLES.");

    // https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/f122f9f1fc729e2dc82e12c3ce73efa875182854/src/tests/hello_xr/graphicsplugin_opengles.cpp#L101-L119
    // Initialize the gl extensions. Note we have to open a window.
    ksDriverInstance driverInstance{};
    ksGpuQueueInfo queueInfo{};
    ksGpuSurfaceColorFormat colorFormat{KS_GPU_SURFACE_COLOR_FORMAT_B8G8R8A8};
    ksGpuSurfaceDepthFormat depthFormat{KS_GPU_SURFACE_DEPTH_FORMAT_D24};
    ksGpuSampleCount sampleCount{KS_GPU_SAMPLE_COUNT_1};
    if (!ksGpuWindow_Create(&window, &driverInstance, &queueInfo, 0, colorFormat, depthFormat, sampleCount, 640, 480, false)) {
        std::cout << "ERROR: OPENGL ES: Failed to create Context." << std::endl;
    }

    GLint glMajorVersion = 0;
    GLint glMinorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &glMinorVersion);

    const XrVersion glApiVersion = XR_MAKE_VERSION(glMajorVersion, glMinorVersion, 0);
    if (graphicsRequirements.minApiVersionSupported > glApiVersion) {
        std::cout << "ERROR: OPENGL ES: The created OpenGL ES version doesn't meet the minimum requried API version for OpenXR." << std::endl;
    }
}

GraphicsAPI_OpenGL_ES::~GraphicsAPI_OpenGL_ES() {
    ksGpuWindow_Destroy(&window);
}

void *GraphicsAPI_OpenGL_ES::GetGraphicsBinding() {
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = window.display;
    graphicsBinding.config = window.context.config;
    graphicsBinding.context = window.context.context;
    return &graphicsBinding;
}

XrSwapchainImageBaseHeader *GraphicsAPI_OpenGL_ES::AllocateSwapchainImageData(uint32_t count) {
    swapchainImages.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    return reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchainImages.data());
}

void *GraphicsAPI_OpenGL_ES::CreateImage(const ImageCreateInfo &imageCI) {
    GLuint texture = 0;
    glGenTextures(1, &texture);

    GLenum target = 0;
    if (imageCI.dimension == 1) {
        if (imageCI.arrayLayers > 1) {
            target = GL_TEXTURE_1D_ARRAY;
        } else {
            target = GL_TEXTURE_1D;
        }
    } else if (imageCI.dimension == 2) {
        if (imageCI.cubemap) {
            if (imageCI.arrayLayers > 6) {
                target = GL_TEXTURE_CUBE_MAP_ARRAY;
            } else {
                target = GL_TEXTURE_CUBE_MAP;
            }
        } else {
            if (imageCI.sampleCount > 1) {
                if (imageCI.arrayLayers > 1) {
                    target = GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
                } else {
                    target = GL_TEXTURE_2D_MULTISAMPLE;
                }
            } else {
                if (imageCI.arrayLayers > 1) {
                    target = GL_TEXTURE_2D_ARRAY;
                } else {
                    target = GL_TEXTURE_2D;
                }
            }
        }
    } else if (imageCI.dimension == 3) {
        target = GL_TEXTURE_3D;
    } else {
        DEBUG_BREAK;
        std::cout << "ERROR: OPENGL: Unknown Dimension for CreateImage: " << imageCI.dimension << std::endl;
        return nullptr;
    }

    glBindTexture(target, texture);

    if (target == GL_TEXTURE_1D) {
        // glTexStorage1D() is not availble - Poor work around.
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, imageCI.mipLevels, imageCI.format, imageCI.width, 1);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else if (target == GL_TEXTURE_2D) {
        glTexStorage2D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height);
    } else if (target == GL_TEXTURE_2D_MULTISAMPLE) {
        glTexStorage2DMultisample(target, imageCI.sampleCount, imageCI.format, imageCI.width, imageCI.height, GL_TRUE);
    } else if (target == GL_TEXTURE_3D) {
        glTexStorage3D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height, imageCI.depth);
    } else if (target == GL_TEXTURE_CUBE_MAP) {
        glTexStorage2D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height);
    } else if (target == GL_TEXTURE_1D_ARRAY) {
        glTexStorage2D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.arrayLayers);
    } else if (target == GL_TEXTURE_2D_ARRAY) {
        glTexStorage3D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height, imageCI.arrayLayers);
    } else if (target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        glTexStorage3DMultisample(target, imageCI.sampleCount, imageCI.format, imageCI.width, imageCI.height, imageCI.arrayLayers, GL_TRUE);
    } else if (target == GL_TEXTURE_CUBE_MAP_ARRAY) {
        glTexStorage3D(target, imageCI.mipLevels, imageCI.format, imageCI.width, imageCI.height, imageCI.arrayLayers);
    }

    glBindTexture(target, 0);

    return (void *)(uint64_t)texture;
}
void GraphicsAPI_OpenGL_ES::DestroyImage(void *&image) {
    GLuint texture = (GLuint)(uint64_t)image;
    glDeleteTextures(1, &texture);
    image = nullptr;
}

const std::vector<int64_t> GraphicsAPI_OpenGL_ES::GetSupportedSwapchainFormats() {
    // https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/f122f9f1fc729e2dc82e12c3ce73efa875182854/src/tests/hello_xr/graphicsplugin_opengles.cpp#L208-L216
    GLint glMajorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajorVersion);
    if (glMajorVersion >= 3) {
        return {GL_RGBA8, GL_RGBA8_SNORM, GL_SRGB8_ALPHA8};
    } else {
        return {GL_RGBA8, GL_RGBA8_SNORM};
    }
}
#endif

// Vulkan
#if defined(XR_USE_GRAPHICS_API_VULKAN)

#define VULKAN_CHECK(x, y)                                                                         \
    {                                                                                              \
        VkResult result = (x);                                                                     \
        if (result != VK_SUCCESS) {                                                                \
            std::cout << "ERROR: VULKAN: " << std::hex << "0x" << result << std::dec << std::endl; \
            std::cout << "ERROR: VULKAN: " << y << std::endl;                                      \
        }                                                                                          \
    }

GraphicsAPI_Vulkan::GraphicsAPI_Vulkan(XrInstance xrInstance, XrSystemId systemId) {
    // Instance
    LoadPFN_XrFunctions(xrInstance);

    XrGraphicsRequirementsVulkanKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    OPENXR_CHECK(xrGetVulkanGraphicsRequirementsKHR(xrInstance, systemId, &graphicsRequirements), "Failed to get Graphics Requirements for Vulkan.");

    VkApplicationInfo ai;
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pNext = nullptr;
    ai.pApplicationName = "OpenXR Tutorial - Vulkan";
    ai.applicationVersion = 1;
    ai.pEngineName = "OpenXR Tutorial - Vulkan Engine";
    ai.engineVersion = 1;
    ai.apiVersion = graphicsRequirements.minApiVersionSupported;

    uint32_t instanceExtensionCount = 0;
    VULKAN_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr), "Failed to enumerate InstanceExtensionProperties.");

    std::vector<VkExtensionProperties> instanceExtensionProperties;
    instanceExtensionProperties.resize(instanceExtensionCount);
    VULKAN_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensionProperties.data()), "Failed to enumerate InstanceExtensionProperties.");
    const std::vector<std::string> &openXrInstanceExtensionNames = GetInstanceExtensionsForOpenXR(xrInstance, systemId);
    for (const std::string &requestExtension : openXrInstanceExtensionNames) {
        for (const VkExtensionProperties &extensionProperty : instanceExtensionProperties) {
            if (strcmp(requestExtension.c_str(), extensionProperty.extensionName))
                continue;
            else
                activeInstanceExtensions.push_back(requestExtension.c_str());
            break;
        }
    }

    VkInstanceCreateInfo instanceCI;
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pNext = nullptr;
    instanceCI.flags = 0;
    instanceCI.pApplicationInfo = &ai;
    instanceCI.enabledLayerCount = 0;
    instanceCI.ppEnabledLayerNames = nullptr;
    instanceCI.enabledExtensionCount = static_cast<uint32_t>(activeInstanceExtensions.size());
    instanceCI.ppEnabledExtensionNames = activeInstanceExtensions.data();
    VULKAN_CHECK(vkCreateInstance(&instanceCI, nullptr, &instance), "Failed to create Vulkan Instance.");

    // Physical Device
    uint32_t physicalDeviceCount = 0;
    std::vector<VkPhysicalDevice> physicalDevices;
    VULKAN_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr), "Failed to enumerate PhysicalDevices.");
    physicalDevices.resize(physicalDeviceCount);
    VULKAN_CHECK(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()), "Failed to enumerate PhysicalDevices.");

    VkPhysicalDevice physicalDeviceFromXR;
    OPENXR_CHECK(xrGetVulkanGraphicsDeviceKHR(xrInstance, systemId, instance, &physicalDeviceFromXR), "Failed to get Graphics Device for Vulkan.");
    auto physicalDeviceFromXR_it = std::find(physicalDevices.begin(), physicalDevices.end(), physicalDeviceFromXR);
    if (physicalDeviceFromXR_it != physicalDevices.end()) {
        physicalDevice = *physicalDeviceFromXR_it;
    } else {
        std::cout << "ERROR: Vulkan: Failed to find PhysicalDevice for OpenXR." << std::endl;
        // Select the first available device.
        physicalDevice = physicalDevices[0];
    }

    // Device
    std::vector<VkQueueFamilyProperties> queueFamilyProperties;
    uint32_t queueFamilyPropertiesCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropertiesCount, nullptr);
    queueFamilyProperties.resize(queueFamilyPropertiesCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyPropertiesCount, queueFamilyProperties.data());

    std::vector<VkDeviceQueueCreateInfo> deviceQueueCIs;
    std::vector<std::vector<float>> queuePriorities;
    queuePriorities.resize(queueFamilyProperties.size());
    deviceQueueCIs.resize(queueFamilyProperties.size());
    for (size_t i = 0; i < deviceQueueCIs.size(); i++) {
        for (size_t j = 0; j < queueFamilyProperties[i].queueCount; j++)
            queuePriorities[i].push_back(1.0f);

        deviceQueueCIs[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        deviceQueueCIs[i].pNext = nullptr;
        deviceQueueCIs[i].flags = 0;
        deviceQueueCIs[i].queueFamilyIndex = static_cast<uint32_t>(i);
        deviceQueueCIs[i].queueCount = queueFamilyProperties[i].queueCount;
        deviceQueueCIs[i].pQueuePriorities = queuePriorities[i].data();
    }

    uint32_t deviceExtensionCount = 0;
    VULKAN_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &deviceExtensionCount, 0), "Failed to enumerate DeviceExtensionProperties.");
    std::vector<VkExtensionProperties> deviceExtensionProperties;
    deviceExtensionProperties.resize(deviceExtensionCount);

    VULKAN_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &deviceExtensionCount, deviceExtensionProperties.data()), "Failed to enumerate DeviceExtensionProperties.");
    const std::vector<std::string> &openXrDeviceExtensionNames = GetDeviceExtensionsForOpenXR(xrInstance, systemId);
    for (const std::string &requestExtension : openXrDeviceExtensionNames) {
        for (const VkExtensionProperties &extensionProperty : deviceExtensionProperties) {
            if (strcmp(requestExtension.c_str(), extensionProperty.extensionName))
                continue;
            else
                activeDeviceExtensions.push_back(requestExtension.c_str());
            break;
        }
    }

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);

    VkDeviceCreateInfo deviceCI;
    deviceCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCI.pNext = nullptr;
    deviceCI.flags = 0;
    deviceCI.queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCIs.size());
    deviceCI.pQueueCreateInfos = deviceQueueCIs.data();
    deviceCI.enabledLayerCount = 0;
    deviceCI.ppEnabledLayerNames = nullptr;
    deviceCI.enabledExtensionCount = static_cast<uint32_t>(activeDeviceExtensions.size());
    deviceCI.ppEnabledExtensionNames = activeDeviceExtensions.data();
    deviceCI.pEnabledFeatures = &features;
    VULKAN_CHECK(vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device), "Failed to create Device.");

    queueFamilyIndex = 0;
    queueIndex = 0;
}

GraphicsAPI_Vulkan::~GraphicsAPI_Vulkan() {
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}

void *GraphicsAPI_Vulkan::GetGraphicsBinding() {
    graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    graphicsBinding.instance = instance;
    graphicsBinding.physicalDevice = physicalDevice;
    graphicsBinding.device = device;
    graphicsBinding.queueFamilyIndex = queueFamilyIndex;
    graphicsBinding.queueIndex = queueIndex;
    return &graphicsBinding;
}

XrSwapchainImageBaseHeader *GraphicsAPI_Vulkan::AllocateSwapchainImageData(uint32_t count) {
    swapchainImages.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    return reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchainImages.data());
}

void *GraphicsAPI_Vulkan::CreateImage(const ImageCreateInfo &imageCI) {
    VkImage image{};
    VkImageCreateInfo vkImageCI;
    vkImageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    vkImageCI.pNext = nullptr;
    vkImageCI.flags = (imageCI.cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) | (imageCI.dimension == 3 ? VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT : 0);
    vkImageCI.imageType = VkImageType(imageCI.dimension - 1);
    vkImageCI.format = (VkFormat)imageCI.format;
    vkImageCI.extent = {imageCI.width, imageCI.height, imageCI.depth};
    vkImageCI.mipLevels = imageCI.mipLevels;
    vkImageCI.arrayLayers = imageCI.arrayLayers;
    vkImageCI.samples = VkSampleCountFlagBits(imageCI.sampleCount);
    vkImageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    vkImageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (imageCI.colorAttachment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0) | (imageCI.depthAttachment ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0);
    vkImageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkImageCI.queueFamilyIndexCount = 0;
    vkImageCI.pQueueFamilyIndices = nullptr;
    vkImageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VULKAN_CHECK(vkCreateImage(device, &vkImageCI, nullptr, &image), "Failed to create Image");

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(device, image, &memoryRequirements);

    VkDeviceMemory memory{};
    VkMemoryAllocateInfo allocateInfo;
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memoryRequirements.size;

    auto MemoryTypeFromProperties = [](VkPhysicalDeviceMemoryProperties memory_properties, uint32_t typeBits, VkMemoryPropertyFlags requirements_mask, uint32_t *typeIndex) -> bool {
        // Search memtypes to find first index with those properties
        for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
            if ((typeBits & 1) == 1) {
                // Type is available, does it match user properties?
                if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
                    *typeIndex = i;
                    return true;
                }
            }
            typeBits >>= 1;
        }
        // No memory types matched, return failure
        return false;
    };

    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemoryProperties);
    MemoryTypeFromProperties(physicalDeviceMemoryProperties, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &allocateInfo.memoryTypeIndex);
   
    vkAllocateMemory(device, &allocateInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);

    imageResources[image] = memory;

    return (void*)image;
}
void GraphicsAPI_Vulkan::DestroyImage(void *&image) {
    VkImage vkImage = (VkImage)image;
    VkDeviceMemory memory = imageResources[vkImage];
    imageResources.erase(vkImage);

    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    image = nullptr;
}

void GraphicsAPI_Vulkan::LoadPFN_XrFunctions(XrInstance xrInstance) {
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsRequirementsKHR), "Failed to get InstanceProcAddr.");
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanInstanceExtensionsKHR), "Failed to get InstanceProcAddr.");
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanDeviceExtensionsKHR), "Failed to get InstanceProcAddr.");
    OPENXR_CHECK(xrGetInstanceProcAddr(xrInstance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsDeviceKHR), "Failed to get InstanceProcAddr.");
}

std::vector<std::string> GraphicsAPI_Vulkan::GetInstanceExtensionsForOpenXR(XrInstance xrInstance, XrSystemId systemId) {
    uint32_t extensionNamesSize = 0;
    OPENXR_CHECK(xrGetVulkanInstanceExtensionsKHR(xrInstance, systemId, 0, &extensionNamesSize, nullptr), "Failed to get Vulkan Instance Extensions.");

    std::vector<char> extensionNames(extensionNamesSize);
    OPENXR_CHECK(xrGetVulkanInstanceExtensionsKHR(xrInstance, systemId, extensionNamesSize, &extensionNamesSize, extensionNames.data()), "Failed to get Vulkan Instance Extensions.");

    std::stringstream streamData(extensionNames.data());
    std::vector<std::string> extensions;
    std::string extension;
    while (std::getline(streamData, extension, ' ')) {
        extensions.push_back(extension);
    }
    return extensions;
}

std::vector<std::string> GraphicsAPI_Vulkan::GetDeviceExtensionsForOpenXR(XrInstance xrInstance, XrSystemId systemId) {
    uint32_t extensionNamesSize = 0;
    OPENXR_CHECK(xrGetVulkanDeviceExtensionsKHR(xrInstance, systemId, 0, &extensionNamesSize, nullptr), "Failed to get Vulkan Device Extensions.");

    std::vector<char> extensionNames(extensionNamesSize);
    OPENXR_CHECK(xrGetVulkanDeviceExtensionsKHR(xrInstance, systemId, extensionNamesSize, &extensionNamesSize, extensionNames.data()), "Failed to get Vulkan Device Extensions.");

    std::stringstream streamData(extensionNames.data());
    std::vector<std::string> extensions;
    std::string extension;
    while (std::getline(streamData, extension, ' ')) {
        extensions.push_back(extension);
    }
    return extensions;
}

const std::vector<int64_t> GraphicsAPI_Vulkan::GetSupportedSwapchainFormats() {
    return {
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM};
}
#endif