#include "graphics_vulkan_defines.h"
#include <vulkan/vulkan.h>

#include <graphics/glfw/glfw.h>
#include <graphics/glfw/glfw_native.h>

#include <dlib/math.h>
#include <dlib/array.h>
#include <dlib/profile.h>
#include <dlib/log.h>
#include <dlib/dstrings.h>

#include <dmsdk/vectormath/cpp/vectormath_aos.h>

#include "../graphics.h"
#include "graphics_vulkan_private.h"

namespace dmGraphics
{
    static const char* VkResultToStr(VkResult res);
    #define CHECK_VK_ERROR(result) \
    { \
        if(g_Context->m_VerifyGraphicsCalls && result != VK_SUCCESS) { \
            dmLogError("Vulkan Error (%s:%d) %s", __FILE__, __LINE__, VkResultToStr(result)); \
            assert(0); \
        } \
    }

    // Validation layers to enable
    static const char*   g_validation_layers[]        = { "VK_LAYER_LUNARG_standard_validation" };
    static const uint8_t g_validation_layer_count     = 1;
    // Validation layer extensions
    static const char*   g_validation_layer_ext[]     = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    static const uint8_t g_validation_layer_ext_count = 1;
    // In flight frames - number of concurrent frames being processed
    static const uint8_t g_max_frames_in_flight       = 2;

    static struct Context
    {
        Context(const ContextParams& params, const VkInstance vk_instance)
        : m_Instance(vk_instance)
        , m_WindowSurface(VK_NULL_HANDLE)
        , m_MainRenderPass(VK_NULL_HANDLE)
        , m_MainRenderTarget(0)
        , m_DefaultTextureMinFilter(params.m_DefaultTextureMinFilter)
        , m_DefaultTextureMagFilter(params.m_DefaultTextureMagFilter)
        , m_VerifyGraphicsCalls(params.m_VerifyGraphicsCalls)
        {}

        ~Context()
        {
            if (m_Instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(m_Instance, 0);
                m_Instance = VK_NULL_HANDLE;
            }
        }

        SwapChain*               m_SwapChain;
        SwapChainCapabilities    m_SwapChainCapabilities;
        PhysicalDevice           m_PhysicalDevice;
        LogicalDevice            m_LogicalDevice;
        FrameResource            m_FrameResources[g_max_frames_in_flight];
        VkInstance               m_Instance;
        VkSurfaceKHR             m_WindowSurface;

        // Main device rendering constructs
        dmArray<VkFramebuffer>   m_MainFrameBuffers;
        dmArray<VkCommandBuffer> m_MainCommandBuffers;
        VkRenderPass             m_MainRenderPass;
        Texture                  m_MainTextureDepthStencil;
        RenderTarget             m_MainRenderTarget;

        // Rendering state
        RenderTarget*            m_CurrentRenderTarget;

        TextureFilter            m_DefaultTextureMinFilter;
        TextureFilter            m_DefaultTextureMagFilter;
        uint32_t                 m_Width;
        uint32_t                 m_Height;
        uint32_t                 m_WindowWidth;
        uint32_t                 m_WindowHeight;
        uint32_t                 m_CurrentFrameInFlight : 1;
        uint32_t                 m_WindowOpened         : 1;
        uint32_t                 m_VerifyGraphicsCalls  : 1;
        uint32_t                 : 29;
    } *g_Context = 0;

    #define DM_VK_RESULT_TO_STR_CASE(x) case x: return #x
    static const char* VkResultToStr(VkResult res)
    {
        switch(res)
        {
            DM_VK_RESULT_TO_STR_CASE(VK_SUCCESS);
            DM_VK_RESULT_TO_STR_CASE(VK_NOT_READY);
            DM_VK_RESULT_TO_STR_CASE(VK_TIMEOUT);
            DM_VK_RESULT_TO_STR_CASE(VK_EVENT_SET);
            DM_VK_RESULT_TO_STR_CASE(VK_EVENT_RESET);
            DM_VK_RESULT_TO_STR_CASE(VK_INCOMPLETE);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INITIALIZATION_FAILED);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_DEVICE_LOST);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_MEMORY_MAP_FAILED);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_LAYER_NOT_PRESENT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_FEATURE_NOT_PRESENT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_TOO_MANY_OBJECTS);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_FRAGMENTED_POOL);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_SURFACE_LOST_KHR);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
            DM_VK_RESULT_TO_STR_CASE(VK_SUBOPTIMAL_KHR);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_OUT_OF_DATE_KHR);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_VALIDATION_FAILED_EXT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INVALID_SHADER_NV);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_FRAGMENTATION_EXT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_NOT_PERMITTED_EXT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_INVALID_DEVICE_ADDRESS_EXT);
            DM_VK_RESULT_TO_STR_CASE(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
            DM_VK_RESULT_TO_STR_CASE(VK_RESULT_MAX_ENUM);
            default: break;
        }

        return "UNKNOWN_ERROR";
    }
    #undef DM_VK_RESULT_TO_STRING_CASE

    static inline void SynchronizeDevice(VkDevice vk_device)
    {
        vkDeviceWaitIdle(vk_device);
    }

    static inline uint32_t GetNextRenderTargetId()
    {
        static uint32_t next_id = 1;

        // Id 0 is taken for the main framebuffer
        if (next_id == 0)
        {
            next_id = 1;
        }

        return next_id++;
    }

    static VkResult CreateMainFrameSyncObjects(VkDevice vk_device, uint8_t numFrameResources, FrameResource* frameResourcesOut)
    {
        VkSemaphoreCreateInfo vk_create_semaphore_info;
        memset(&vk_create_semaphore_info, 0, sizeof(vk_create_semaphore_info));
        vk_create_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo vk_create_fence_info;
        memset(&vk_create_fence_info, 0, sizeof(vk_create_fence_info));
        vk_create_fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vk_create_fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for(uint8_t i=0; i < numFrameResources; i++)
        {
            if (vkCreateSemaphore(vk_device, &vk_create_semaphore_info, 0, &frameResourcesOut[i].m_ImageAvailable) != VK_SUCCESS ||
                vkCreateSemaphore(vk_device, &vk_create_semaphore_info, 0, &frameResourcesOut[i].m_RenderFinished) != VK_SUCCESS ||
                vkCreateFence(vk_device, &vk_create_fence_info, 0, &frameResourcesOut[i].m_SubmitFence) != VK_SUCCESS)
            {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }

        return VK_SUCCESS;
    }

    static bool EndRenderPass(HContext context)
    {
        assert(context->m_CurrentRenderTarget);
        if (!context->m_CurrentRenderTarget->m_IsBound)
        {
            return false;
        }

        vkCmdEndRenderPass(context->m_MainCommandBuffers[context->m_SwapChain->m_ImageIndex]);
        context->m_CurrentRenderTarget->m_IsBound = 0;
        return true;
    }

    static void BeginRenderPass(HContext context, RenderTarget* rt)
    {
        assert(context->m_CurrentRenderTarget);
        if (context->m_CurrentRenderTarget->m_Id == rt->m_Id &&
            context->m_CurrentRenderTarget->m_IsBound)
        {
            return;
        }

        // If we bind a render pass without explicitly unbinding
        // the current render pass, we must first unbind it.
        if (context->m_CurrentRenderTarget->m_IsBound)
        {
            EndRenderPass(context);
        }

        VkClearValue vk_clear_values[2];
        memset(vk_clear_values, 0, sizeof(vk_clear_values));

        // Clear color and depth/stencil separately
        vk_clear_values[0].color.float32[3]     = 1.0f;
        vk_clear_values[1].depthStencil.depth   = 1.0f;
        vk_clear_values[1].depthStencil.stencil = 0;

        VkRenderPassBeginInfo vk_render_pass_begin_info;
        vk_render_pass_begin_info.sType               = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        vk_render_pass_begin_info.renderPass          = rt->m_RenderPass;
        vk_render_pass_begin_info.framebuffer         = rt->m_Framebuffer;
        vk_render_pass_begin_info.pNext               = 0;
        vk_render_pass_begin_info.renderArea.offset.x = 0;
        vk_render_pass_begin_info.renderArea.offset.y = 0;
        vk_render_pass_begin_info.renderArea.extent   = rt->m_Extent;
        vk_render_pass_begin_info.clearValueCount     = 2;
        vk_render_pass_begin_info.pClearValues        = 0;

        vk_render_pass_begin_info.clearValueCount = 2;
        vk_render_pass_begin_info.pClearValues    = vk_clear_values;

        vkCmdBeginRenderPass(context->m_MainCommandBuffers[context->m_SwapChain->m_ImageIndex], &vk_render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        context->m_CurrentRenderTarget = rt;
        context->m_CurrentRenderTarget->m_IsBound = 1;
    }

    static VkResult AllocateDepthStencilTexture(HContext context, uint32_t width, uint32_t height, Texture* depthStencilTextureOut)
    {
        // Depth formats are optional, so we need to query
        // what available formats we have.
        VkFormat vk_format_list_default[] = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM
        };

        const VkPhysicalDevice vk_physical_device = context->m_PhysicalDevice.m_Device;
        const VkDevice vk_device                  = context->m_LogicalDevice.m_Device;
        const size_t vk_format_list_size          = sizeof(vk_format_list_default) / sizeof(vk_format_list_default[0]);

        // Check format support
        VkImageTiling vk_image_tiling = VK_IMAGE_TILING_OPTIMAL;
        VkFormat vk_depth_format      = GetSupportedTilingFormat(vk_physical_device, &vk_format_list_default[0],
            vk_format_list_size, vk_image_tiling, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        if (vk_depth_format == VK_FORMAT_UNDEFINED)
        {
            vk_image_tiling = VK_IMAGE_TILING_LINEAR;
            vk_depth_format = GetSupportedTilingFormat(vk_physical_device, &vk_format_list_default[0],
                vk_format_list_size, vk_image_tiling, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        }

        // The aspect flag indicates what the image should be used for,
        // it is usually color or stencil | depth.
        VkImageAspectFlags vk_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vk_depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            vk_depth_format == VK_FORMAT_D24_UNORM_S8_UINT  ||
            vk_depth_format == VK_FORMAT_D16_UNORM_S8_UINT)
        {
            vk_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        VkResult res = CreateTexture2D(vk_physical_device, vk_device, width, height, 1,
            vk_depth_format, vk_image_tiling, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vk_aspect, depthStencilTextureOut);
        CHECK_VK_ERROR(res);

        if (res == VK_SUCCESS)
        {
            res = TransitionImageLayout(vk_device, context->m_LogicalDevice.m_CommandPool, context->m_LogicalDevice.m_GraphicsQueue, depthStencilTextureOut->m_Image, vk_aspect,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            CHECK_VK_ERROR(res);
        }

        return res;
    }

    static VkResult CreateMainRenderTarget(HContext context)
    {
        assert(context);
        assert(context->m_SwapChain);

        // We need to create a framebuffer per swap chain image
        // so that they can be used in different states in the rendering pipeline
        context->m_MainFrameBuffers.SetCapacity(context->m_SwapChain->m_Images.Size());
        context->m_MainFrameBuffers.SetSize(context->m_SwapChain->m_Images.Size());

        SwapChain* swapChain = context->m_SwapChain;
        VkResult res;

        for (uint8_t i=0; i < context->m_MainFrameBuffers.Size(); i++)
        {
            // All swap chain images can share the same depth buffer,
            // that's why we create a single depth buffer at the start and reuse it.
            VkImageView&   vk_image_view_color     = swapChain->m_ImageViews[i];
            VkImageView&   vk_image_view_depth     = context->m_MainTextureDepthStencil.m_ImageView;
            VkImageView    vk_image_attachments[2] = { vk_image_view_color, vk_image_view_depth };

            res = CreateFramebuffer(context->m_LogicalDevice.m_Device,context->m_MainRenderPass,
                swapChain->m_ImageExtent.width, swapChain->m_ImageExtent.height,
                vk_image_attachments, 2, &context->m_MainFrameBuffers[i]);
            CHECK_VK_ERROR(res);
        }

        // Initialize the dummy rendertarget for the main framebuffer
        // The m_Framebuffer construct will be rotated sequentially
        // with the framebuffer objects created per swap chain.
        RenderTarget& rt = context->m_MainRenderTarget;
        rt.m_RenderPass  = context->m_MainRenderPass;
        rt.m_Framebuffer = context->m_MainFrameBuffers[0];
        rt.m_Extent      = swapChain->m_ImageExtent;

        return res;
    }

    static VkResult CreateMainRenderingResources(HContext context)
    {
        VkDevice vk_device = context->m_LogicalDevice.m_Device;

        // Create depth/stencil buffer
        memset(&context->m_MainTextureDepthStencil, 0, sizeof(context->m_MainTextureDepthStencil));
        VkResult res = AllocateDepthStencilTexture(context,
            context->m_SwapChain->m_ImageExtent.width,
            context->m_SwapChain->m_ImageExtent.height,
            &context->m_MainTextureDepthStencil);
        CHECK_VK_ERROR(res);

        // Create main render pass with two attachments
        RenderPassAttachment attachments[2];
        attachments[0].m_Format      = context->m_SwapChain->m_SurfaceFormat.format;;
        attachments[0].m_ImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].m_Format      = context->m_MainTextureDepthStencil.m_Format;
        attachments[1].m_ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        res = CreateRenderPass(vk_device, attachments, 1, &attachments[1], &context->m_MainRenderPass);
        CHECK_VK_ERROR(res);

        res = CreateMainRenderTarget(context);
        CHECK_VK_ERROR(res);
        context->m_CurrentRenderTarget = &context->m_MainRenderTarget;

        uint32_t num_swap_chain_images = context->m_SwapChain->m_Images.Size();
        context->m_MainCommandBuffers.SetCapacity(num_swap_chain_images);
        context->m_MainCommandBuffers.SetSize(num_swap_chain_images);

        res = CreateCommandBuffers(vk_device,
            context->m_LogicalDevice.m_CommandPool,
            context->m_MainCommandBuffers.Size(),
            context->m_MainCommandBuffers.Begin());
        CHECK_VK_ERROR(res);

        res = CreateMainFrameSyncObjects(vk_device, g_max_frames_in_flight, context->m_FrameResources);
        CHECK_VK_ERROR(res);

        return res;
    }

    static void SwapChainChanged(HContext context, uint32_t* width, uint32_t* height)
    {
        VkDevice vk_device = context->m_LogicalDevice.m_Device;
        // Flush all current commands
        SynchronizeDevice(vk_device);
        VkResult res = UpdateSwapChain(context->m_SwapChain, width, height, true, context->m_SwapChainCapabilities);
        CHECK_VK_ERROR(res);

        // Reset & create main Depth/Stencil buffer
        ResetTexture(vk_device, &context->m_MainTextureDepthStencil);
        res = AllocateDepthStencilTexture(context,
            context->m_SwapChain->m_ImageExtent.width,
            context->m_SwapChain->m_ImageExtent.height,
            &context->m_MainTextureDepthStencil);
        CHECK_VK_ERROR(res);

        context->m_WindowWidth  = context->m_SwapChain->m_ImageExtent.width;
        context->m_WindowHeight = context->m_SwapChain->m_ImageExtent.height;

        // Reset main rendertarget (but not the render pass)
        RenderTarget* mainRenderTarget = &context->m_MainRenderTarget;
        mainRenderTarget->m_RenderPass = VK_NULL_HANDLE;
        for (uint8_t i=0; i < context->m_MainFrameBuffers.Size(); i++)
        {
            mainRenderTarget->m_Framebuffer = context->m_MainFrameBuffers[i];
            ResetRenderTarget(&context->m_LogicalDevice, mainRenderTarget);
        }

        res = CreateMainRenderTarget(context);
        CHECK_VK_ERROR(res);

        // Flush once again to make sure all transitions are complete
        SynchronizeDevice(vk_device);
    }

    static bool InitializeVulkan(HContext context, const WindowParams* params)
    {
        VkResult res = CreateWindowSurface(context->m_Instance, &context->m_WindowSurface, params->m_HighDPI);
        if (res != VK_SUCCESS)
        {
            dmLogError("Could not create window surface for Vulkan, reason: %s.", VkResultToStr(res));
            return false;
        }

        uint32_t device_count = GetPhysicalDeviceCount(context->m_Instance);

        if (device_count == 0)
        {
            dmLogError("Could not get any Vulkan devices.");
            return false;
        }

        PhysicalDevice* device_list     = new PhysicalDevice[device_count];
        PhysicalDevice* selected_device = NULL;
        GetPhysicalDevices(context->m_Instance, &device_list, device_count);

        const uint8_t required_device_extension_count = 2;
        const char* required_device_extensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            // From spec:
            // "Allow negative height to be specified in the VkViewport::height field to
            // perform y-inversion of the clip-space to framebuffer-space transform.
            // This allows apps to avoid having to use gl_Position.y = -gl_Position.y
            // in shaders also targeting other APIs."
            "VK_KHR_maintenance1"
        };

        QueueFamily selected_queue_family;
        SwapChainCapabilities selected_swap_chain_capabilities;
        for (uint32_t i = 0; i < device_count; ++i)
        {
            #define RESET_AND_CONTINUE(d) \
                ResetPhysicalDevice(d); \
                continue;

            PhysicalDevice* device = &device_list[i];

            // Make sure we have a graphics and present queue available
            QueueFamily queue_family = GetQueueFamily(device, context->m_WindowSurface);
            if (!queue_family.IsValid())
            {
                RESET_AND_CONTINUE(device)
            }

            // Make sure all device extensions are supported
            bool all_extensions_found = true;
            for (int32_t ext_i = 0; ext_i < required_device_extension_count; ++ext_i)
            {
                bool found = false;
                for (uint32_t j=0; j < device->m_DeviceExtensionCount; ++j)
                {
                    if (dmStrCaseCmp(device->m_DeviceExtensions[j].extensionName, required_device_extensions[ext_i]) == 0)
                    {
                        found = true;
                        break;
                    }
                }

                all_extensions_found &= found;
            }

            if (!all_extensions_found)
            {
                RESET_AND_CONTINUE(device)
            }

            // Make sure device has swap chain support
            GetSwapChainCapabilities(device, context->m_WindowSurface, selected_swap_chain_capabilities);

            if (selected_swap_chain_capabilities.m_SurfaceFormats.Size() == 0 ||
                selected_swap_chain_capabilities.m_PresentModes.Size() == 0)
            {
                RESET_AND_CONTINUE(device)
            }

            selected_device = device;
            selected_queue_family = queue_family;
            break;

            #undef RESET_AND_CONTINUE
        }

        LogicalDevice logical_device;
        uint32_t created_width  = params->m_Width;
        uint32_t created_height = params->m_Height;
        const bool want_vsync   = true;

        if (selected_device == NULL)
        {
            dmLogError("Could not select a suitable Vulkan device.");
            goto bail;
        }

        res = CreateLogicalDevice(selected_device, context->m_WindowSurface, selected_queue_family,
            required_device_extensions, required_device_extension_count,
            g_validation_layers, g_validation_layer_count, &logical_device);
        if (res != VK_SUCCESS)
        {
            dmLogError("Could not create a logical Vulkan device, reason: %s", VkResultToStr(res));
            goto bail;
        }

        context->m_PhysicalDevice = *selected_device;
        context->m_LogicalDevice  = logical_device;

        // Create swap chain
        context->m_SwapChainCapabilities.Swap(selected_swap_chain_capabilities);
        context->m_SwapChain = new SwapChain(&context->m_LogicalDevice, context->m_WindowSurface, context->m_SwapChainCapabilities, selected_queue_family);

        res = UpdateSwapChain(context->m_SwapChain, &created_width, &created_height, want_vsync, context->m_SwapChainCapabilities);
        if (res != VK_SUCCESS)
        {
            dmLogError("Could not create a swap chain for Vulkan, reason: %s", VkResultToStr(res));
            goto bail;
        }

        delete[] device_list;

        // Create framebuffers, default renderpass etc.
        res = CreateMainRenderingResources(context);
        if (res != VK_SUCCESS)
        {
            dmLogError("Could not create main rendering resources for Vulkan, reason: %s", VkResultToStr(res));
            goto bail;
        }

        return true;
bail:
        if (context->m_SwapChain)
            delete context->m_SwapChain;
        if (device_list)
            delete[] device_list;
        return false;
    }

    HContext NewContext(const ContextParams& params)
    {
        if (g_Context == 0x0)
        {
            if (glfwInit() == 0)
            {
                dmLogError("Could not initialize glfw.");
                return 0x0;
            }

            uint16_t validation_layer_count = 0;

            const char* env_vulkan_validation = getenv("DM_VULKAN_VALIDATION");
            if (env_vulkan_validation != 0x0)
            {
                validation_layer_count = strtol(env_vulkan_validation, 0, 10) ? g_validation_layer_count : 0;
            }

            VkInstance vk_instance;
            if (CreateInstance(&vk_instance, g_validation_layers, validation_layer_count, g_validation_layer_ext, g_validation_layer_ext_count) != VK_SUCCESS)
            {
                dmLogError("Could not create Vulkan instance");
                return 0x0;
            }

            g_Context = new Context(params, vk_instance);

            return g_Context;
        }
        return 0x0;
    }

    void DeleteContext(HContext context)
    {
        if (context != 0x0)
        {
            delete context;
            g_Context = 0x0;
        }
    }

    bool Initialize()
    {
        return glfwInit();
    }

    void Finalize()
    {
        glfwTerminate();
    }

    uint32_t GetWindowRefreshRate(HContext context)
    {
        return 0;
    }

    WindowResult OpenWindow(HContext context, WindowParams* params)
    {
        assert(context);
        assert(context->m_WindowSurface == VK_NULL_HANDLE);

        glfwOpenWindowHint(GLFW_CLIENT_API,   GLFW_NO_API);
        glfwOpenWindowHint(GLFW_FSAA_SAMPLES, params->m_Samples);

        int mode = params->m_Fullscreen ? GLFW_FULLSCREEN : GLFW_WINDOW;

        if (!glfwOpenWindow(params->m_Width, params->m_Height, 8, 8, 8, 8, 32, 8, mode))
        {
            return WINDOW_RESULT_WINDOW_OPEN_ERROR;
        }

        if (!InitializeVulkan(context, params))
        {
            return WINDOW_RESULT_WINDOW_OPEN_ERROR;
        }

    #if !defined(__EMSCRIPTEN__)
        glfwSetWindowTitle(params->m_Title);
    #endif

        context->m_WindowOpened   = 1;
        context->m_Width          = params->m_Width;
        context->m_Height         = params->m_Height;
        context->m_WindowWidth    = context->m_SwapChain->m_ImageExtent.width;
        context->m_WindowHeight   = context->m_SwapChain->m_ImageExtent.height;

        return WINDOW_RESULT_OK;
    }

    void CloseWindow(HContext context)
    {
        assert(context);
        if (context->m_WindowOpened)
        {
            VkDevice vk_device = context->m_LogicalDevice.m_Device;

            SynchronizeDevice(vk_device);

            glfwCloseWindow();

            ResetTexture(vk_device, &context->m_MainTextureDepthStencil);

            vkDestroyRenderPass(vk_device, context->m_MainRenderPass, 0);

            vkFreeCommandBuffers(vk_device, context->m_LogicalDevice.m_CommandPool, context->m_MainCommandBuffers.Size(), context->m_MainCommandBuffers.Begin());

            for (uint8_t i=0; i < context->m_MainFrameBuffers.Size(); i++)
            {
                vkDestroyFramebuffer(vk_device, context->m_MainFrameBuffers[i], 0);
            }

            for (size_t i = 0; i < g_max_frames_in_flight; i++) {
                FrameResource& frame_resource = context->m_FrameResources[i];
                vkDestroySemaphore(vk_device, frame_resource.m_RenderFinished, 0);
                vkDestroySemaphore(vk_device, frame_resource.m_ImageAvailable, 0);
                vkDestroyFence(vk_device, frame_resource.m_SubmitFence, 0);
            }

            ResetSwapChain(context->m_SwapChain);

            ResetLogicalDevice(&context->m_LogicalDevice);
            ResetPhysicalDevice(&context->m_PhysicalDevice);

            vkDestroySurfaceKHR(context->m_Instance, context->m_WindowSurface, 0);

            DestroyInstance(&context->m_Instance);

            context->m_WindowOpened = 0;

            delete context->m_SwapChain;
        }
    }

    void IconifyWindow(HContext context)
    {}

    uint32_t GetWindowState(HContext context, WindowState state)
    {
        assert(context);
        if (context->m_WindowOpened)
        {
            return glfwGetWindowParam(state);
        }
        else
        {
            return 0;
        }
    }

    uint32_t GetDisplayDpi(HContext context)
    {
        return 0;
    }

    uint32_t GetWidth(HContext context)
    {
        assert(context);
        return context->m_Width;
    }

    uint32_t GetHeight(HContext context)
    {
        assert(context);
        return context->m_Height;
    }

    uint32_t GetWindowWidth(HContext context)
    {
        assert(context);
        return context->m_WindowWidth;
    }

    uint32_t GetWindowHeight(HContext context)
    {
        assert(context);
        return context->m_WindowHeight;
    }

    void SetWindowSize(HContext context, uint32_t width, uint32_t height)
    {
        assert(context);
        if (context->m_WindowOpened)
        {
            context->m_Width        = width;
            context->m_Height       = height;
            context->m_WindowWidth  = width;
            context->m_WindowHeight = height;
            glfwSetWindowSize((int)width, (int)height);
            SwapChainChanged(context, &context->m_WindowWidth, &context->m_WindowHeight);
        }
    }

    void ResizeWindow(HContext context, uint32_t width, uint32_t height)
    {
        assert(context);
        if (context->m_WindowOpened)
        {
            context->m_Width        = width;
            context->m_Height       = height;
            context->m_WindowWidth  = width;
            context->m_WindowHeight = height;
            glfwSetWindowSize((int)width, (int)height);
            SwapChainChanged(context, &context->m_WindowWidth, &context->m_WindowHeight);
        }
    }

    void GetDefaultTextureFilters(HContext context, TextureFilter& out_min_filter, TextureFilter& out_mag_filter)
    {
        out_min_filter = context->m_DefaultTextureMinFilter;
        out_mag_filter = context->m_DefaultTextureMagFilter;
    }

    void BeginFrame(HContext context)
    {
        FrameResource& current_frame_resource = context->m_FrameResources[context->m_CurrentFrameInFlight];

        VkDevice vk_device = context->m_LogicalDevice.m_Device;

        vkWaitForFences(vk_device, 1, &current_frame_resource.m_SubmitFence, VK_TRUE, UINT64_MAX);
        vkResetFences(vk_device, 1, &current_frame_resource.m_SubmitFence);

        VkResult res = context->m_SwapChain->Advance(current_frame_resource.m_ImageAvailable);

        if (res != VK_SUCCESS)
        {
            if (res == VK_ERROR_OUT_OF_DATE_KHR)
            {
                context->m_WindowWidth  = context->m_Width;
                context->m_WindowHeight = context->m_Height;
                SwapChainChanged(context, &context->m_WindowWidth, &context->m_WindowHeight);
                res = context->m_SwapChain->Advance(current_frame_resource.m_ImageAvailable);
                assert(res == VK_SUCCESS);
            }
            else if (res == VK_SUBOPTIMAL_KHR)
            {
                // Presenting the swap chain will still work but not optimally, but we should still notify.
                dmLogOnceWarning("Vulkan swapchain is out of date, reason: VK_SUBOPTIMAL_KHR.");
            }
            else
            {
                dmLogOnceError("Vulkan swapchain is out of date, reason: %s.", VkResultToStr(res));
                return;
            }
        }

        uint32_t frame_ix = context->m_SwapChain->m_ImageIndex;
        VkCommandBufferBeginInfo vk_command_buffer_begin_info;

        vk_command_buffer_begin_info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vk_command_buffer_begin_info.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        vk_command_buffer_begin_info.pInheritanceInfo = 0;
        vk_command_buffer_begin_info.pNext            = 0;

        vkBeginCommandBuffer(context->m_MainCommandBuffers[frame_ix], &vk_command_buffer_begin_info);

        context->m_MainRenderTarget.m_Framebuffer = context->m_MainFrameBuffers[frame_ix];

        BeginRenderPass(context, context->m_CurrentRenderTarget);
    }

    void Flip(HContext context)
    {
        uint32_t frame_ix = context->m_SwapChain->m_ImageIndex;
        FrameResource& current_frame_resource = context->m_FrameResources[context->m_CurrentFrameInFlight];

        if (!EndRenderPass(context))
        {
            assert(0);
            return;
        }

        VkResult res = vkEndCommandBuffer(context->m_MainCommandBuffers[frame_ix]);
        CHECK_VK_ERROR(res);

        VkPipelineStageFlags vk_pipeline_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo vk_submit_info;
        vk_submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vk_submit_info.pNext                = 0;
        vk_submit_info.waitSemaphoreCount   = 1;
        vk_submit_info.pWaitSemaphores      = &current_frame_resource.m_ImageAvailable;
        vk_submit_info.pWaitDstStageMask    = &vk_pipeline_stage_flags;
        vk_submit_info.commandBufferCount   = 1;
        vk_submit_info.pCommandBuffers      = &context->m_MainCommandBuffers[frame_ix];
        vk_submit_info.signalSemaphoreCount = 1;
        vk_submit_info.pSignalSemaphores    = &current_frame_resource.m_RenderFinished;

        res = vkQueueSubmit(context->m_LogicalDevice.m_GraphicsQueue, 1, &vk_submit_info, current_frame_resource.m_SubmitFence);
        CHECK_VK_ERROR(res);

        VkPresentInfoKHR vk_present_info;
        vk_present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        vk_present_info.pNext              = 0;
        vk_present_info.waitSemaphoreCount = 1;
        vk_present_info.pWaitSemaphores    = &current_frame_resource.m_RenderFinished;
        vk_present_info.swapchainCount     = 1;
        vk_present_info.pSwapchains        = &context->m_SwapChain->m_SwapChain;
        vk_present_info.pImageIndices      = &frame_ix;
        vk_present_info.pResults           = 0;

        res = vkQueuePresentKHR(context->m_LogicalDevice.m_PresentQueue, &vk_present_info);
        CHECK_VK_ERROR(res);

        // Advance frame index
        context->m_CurrentFrameInFlight = (context->m_CurrentFrameInFlight + 1) % g_max_frames_in_flight;

    #if (defined(__arm__) || defined(__arm64__))
        glfwSwapBuffers();
    #endif
    }

    void SetSwapInterval(HContext context, uint32_t swap_interval)
    {}

    void Clear(HContext context, uint32_t flags, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha, float depth, uint32_t stencil)
    {
        assert(context);
        assert(context->m_CurrentRenderTarget);
        DM_PROFILE(Graphics, "Clear");

        float r = ((float)red)/255.0f;
        float g = ((float)green)/255.0f;
        float b = ((float)blue)/255.0f;
        float a = ((float)alpha)/255.0f;

        VkClearRect vk_clear_rect;
        vk_clear_rect.rect.offset.x      = 0;
        vk_clear_rect.rect.offset.y      = 0;
        vk_clear_rect.rect.extent.width  = context->m_CurrentRenderTarget->m_Extent.width;
        vk_clear_rect.rect.extent.height = context->m_CurrentRenderTarget->m_Extent.height;
        vk_clear_rect.baseArrayLayer     = 0;
        vk_clear_rect.layerCount         = 1;

        VkClearAttachment vk_clear_attachments[2];
        memset(vk_clear_attachments, 0, sizeof(vk_clear_attachments));

        // Clear color
        vk_clear_attachments[0].aspectMask                  = VK_IMAGE_ASPECT_COLOR_BIT;
        vk_clear_attachments[0].clearValue.color.float32[0] = r;
        vk_clear_attachments[0].clearValue.color.float32[1] = g;
        vk_clear_attachments[0].clearValue.color.float32[2] = b;
        vk_clear_attachments[0].clearValue.color.float32[3] = a;

        // Clear depth / stencil
        vk_clear_attachments[1].aspectMask                      = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        vk_clear_attachments[1].clearValue.depthStencil.stencil = stencil;
        vk_clear_attachments[1].clearValue.depthStencil.depth   = depth;

        vkCmdClearAttachments(context->m_MainCommandBuffers[context->m_SwapChain->m_ImageIndex],
            2, vk_clear_attachments, 1, &vk_clear_rect);
    }

    HVertexBuffer NewVertexBuffer(HContext context, uint32_t size, const void* data, BufferUsage buffer_usage)
    {
        return (HVertexBuffer) new uint32_t;
    }

    void DeleteVertexBuffer(HVertexBuffer buffer)
    {
        delete (uint32_t*) buffer;
    }

    void SetVertexBufferData(HVertexBuffer buffer, uint32_t size, const void* data, BufferUsage buffer_usage)
    {}

    void SetVertexBufferSubData(HVertexBuffer buffer, uint32_t offset, uint32_t size, const void* data)
    {}

    void* MapVertexBuffer(HVertexBuffer buffer, BufferAccess access)
    {
        return 0;
    }

    bool UnmapVertexBuffer(HVertexBuffer buffer)
    {
        return true;
    }

    uint32_t GetMaxElementsVertices(HContext context)
    {
        return 65536;
    }

    HIndexBuffer NewIndexBuffer(HContext context, uint32_t size, const void* data, BufferUsage buffer_usage)
    {
        return (HIndexBuffer) new uint32_t;
    }

    void DeleteIndexBuffer(HIndexBuffer buffer)
    {
        delete (uint32_t*) buffer;
    }

    void SetIndexBufferData(HIndexBuffer buffer, uint32_t size, const void* data, BufferUsage buffer_usage)
    {}

    void SetIndexBufferSubData(HIndexBuffer buffer, uint32_t offset, uint32_t size, const void* data)
    {}

    void* MapIndexBuffer(HIndexBuffer buffer, BufferAccess access)
    {
        return 0;
    }

    bool UnmapIndexBuffer(HIndexBuffer buffer)
    {
        return true;
    }

    bool IsIndexBufferFormatSupported(HContext context, IndexBufferFormat format)
    {
        return true;
    }

    uint32_t GetMaxElementsIndices(HContext context)
    {
        return 65536;
    }


    HVertexDeclaration NewVertexDeclaration(HContext context, VertexElement* element, uint32_t count)
    {
        return new VertexDeclaration;
    }

    HVertexDeclaration NewVertexDeclaration(HContext context, VertexElement* element, uint32_t count, uint32_t stride)
    {
        return new VertexDeclaration;
    }

    void DeleteVertexDeclaration(HVertexDeclaration vertex_declaration)
    {}

    void EnableVertexDeclaration(HContext context, HVertexDeclaration vertex_declaration, HVertexBuffer vertex_buffer)
    {}

    void EnableVertexDeclaration(HContext context, HVertexDeclaration vertex_declaration, HVertexBuffer vertex_buffer, HProgram program)
    {}

    void DisableVertexDeclaration(HContext context, HVertexDeclaration vertex_declaration)
    {}


    void DrawElements(HContext context, PrimitiveType prim_type, uint32_t first, uint32_t count, Type type, HIndexBuffer index_buffer)
    {}

    void Draw(HContext context, PrimitiveType prim_type, uint32_t first, uint32_t count)
    {}

    HVertexProgram NewVertexProgram(HContext context, ShaderDesc::Shader* ddf)
    {
        return (HVertexProgram) new uint32_t;
    }

    HFragmentProgram NewFragmentProgram(HContext context, ShaderDesc::Shader* ddf)
    {
        return (HFragmentProgram) new uint32_t;
    }

    HProgram NewProgram(HContext context, HVertexProgram vertex_program, HFragmentProgram fragment_program)
    {
        return (HProgram) new uint32_t;
    }

    void DeleteProgram(HContext context, HProgram program)
    {
        delete (uint32_t*) program;
    }

    bool ReloadVertexProgram(HVertexProgram prog, ShaderDesc::Shader* ddf)
    {
        return true;
    }

    bool ReloadFragmentProgram(HFragmentProgram prog, ShaderDesc::Shader* ddf)
    {
        return true;
    }

    void DeleteVertexProgram(HVertexProgram prog)
    {
        delete (uint32_t*) prog;
    }

    void DeleteFragmentProgram(HFragmentProgram prog)
    {
        delete (uint32_t*) prog;
    }

    ShaderDesc::Language GetShaderProgramLanguage(HContext context)
    {
        return ShaderDesc::LANGUAGE_SPIRV;
    }


    void EnableProgram(HContext context, HProgram program)
    {}

    void DisableProgram(HContext context)
    {}

    bool ReloadProgram(HContext context, HProgram program, HVertexProgram vert_program, HFragmentProgram frag_program)
    {
        return true;
    }


    uint32_t GetUniformCount(HProgram prog)
    {
        return 0;
    }

    void GetUniformName(HProgram prog, uint32_t index, char* buffer, uint32_t buffer_size, Type* type)
    {}

    int32_t GetUniformLocation(HProgram prog, const char* name)
    {
        return 0;
    }

    void SetConstantV4(HContext context, const Vectormath::Aos::Vector4* data, int base_register)
    {}

    void SetConstantM4(HContext context, const Vectormath::Aos::Vector4* data, int base_register)
    {}

    void SetSampler(HContext context, int32_t location, int32_t unit)
    {}


    void SetViewport(HContext context, int32_t x, int32_t y, int32_t width, int32_t height)
    {}

    void EnableState(HContext context, State state)
    {}

    void DisableState(HContext context, State state)
    {}

    void SetBlendFunc(HContext context, BlendFactor source_factor, BlendFactor destinaton_factor)
    {}

    void SetColorMask(HContext context, bool red, bool green, bool blue, bool alpha)
    {}

    void SetDepthMask(HContext context, bool mask)
    {}

    void SetDepthFunc(HContext context, CompareFunc func)
    {}

    void SetScissor(HContext context, int32_t x, int32_t y, int32_t width, int32_t height)
    {}

    void SetStencilMask(HContext context, uint32_t mask)
    {}

    void SetStencilFunc(HContext context, CompareFunc func, uint32_t ref, uint32_t mask)
    {}

    void SetStencilOp(HContext context, StencilOp sfail, StencilOp dpfail, StencilOp dppass)
    {}

    void SetCullFace(HContext context, FaceType face_type)
    {}

    void SetPolygonOffset(HContext context, float factor, float units)
    {}


    HRenderTarget NewRenderTarget(HContext context, uint32_t buffer_type_flags, const TextureCreationParams creation_params[MAX_BUFFER_TYPE_COUNT], const TextureParams params[MAX_BUFFER_TYPE_COUNT])
    {
        return new RenderTarget(GetNextRenderTargetId());
    }

    void DeleteRenderTarget(HRenderTarget render_target)
    {
        delete render_target;
    }

    void SetRenderTarget(HContext context, HRenderTarget render_target, uint32_t transient_buffer_types)
    {}

    HTexture GetRenderTargetTexture(HRenderTarget render_target, BufferType buffer_type)
    {
        return 0;
    }

    void GetRenderTargetSize(HRenderTarget render_target, BufferType buffer_type, uint32_t& width, uint32_t& height)
    {}

    void SetRenderTargetSize(HRenderTarget render_target, uint32_t width, uint32_t height)
    {}

    bool IsTextureFormatSupported(HContext context, TextureFormat format)
    {
        return true;
    }

    HTexture NewTexture(HContext context, const TextureCreationParams& params)
    {
        Texture* tex  = new Texture;
        tex->m_Type   = params.m_Type;
        tex->m_Width  = params.m_Width;
        tex->m_Height = params.m_Height;

        if (params.m_OriginalWidth == 0)
        {
            tex->m_OriginalWidth  = params.m_Width;
            tex->m_OriginalHeight = params.m_Height;
        }
        else
        {
            tex->m_OriginalWidth  = params.m_OriginalWidth;
            tex->m_OriginalHeight = params.m_OriginalHeight;
        }

        return (HTexture) tex;
    }

    void DeleteTexture(HTexture t)
    {
        delete t;
    }

    void SetTexture(HTexture texture, const TextureParams& params)
    {}

    void SetTextureAsync(HTexture texture, const TextureParams& paramsa)
    {}

    uint8_t* GetTextureData(HTexture texture)
    {
        return 0;
    }

    void SetTextureParams(HTexture texture, TextureFilter minfilter, TextureFilter magfilter, TextureWrap uwrap, TextureWrap vwrap)
    {}

    uint32_t GetTextureResourceSize(HTexture texture)
    {
        return 0;
    }

    uint16_t GetTextureWidth(HTexture texture)
    {
        return texture->m_Width;
    }

    uint16_t GetTextureHeight(HTexture texture)
    {
        return texture->m_Height;
    }

    uint16_t GetOriginalTextureWidth(HTexture texture)
    {
        return texture->m_OriginalWidth;
    }

    uint16_t GetOriginalTextureHeight(HTexture texture)
    {
        return texture->m_OriginalHeight;
    }

    void EnableTexture(HContext context, uint32_t unit, HTexture texture)
    {}

    void DisableTexture(HContext context, uint32_t unit, HTexture texture)
    {}

    uint32_t GetMaxTextureSize(HContext context)
    {
        return context->m_PhysicalDevice.m_Properties.limits.maxImageDimension2D;
    }

    uint32_t GetTextureStatusFlags(HTexture texture)
    {
        return 0;
    }

    void ReadPixels(HContext context, void* buffer, uint32_t buffer_size)
    {}

    void RunApplicationLoop(void* user_data, WindowStepMethod step_method, WindowIsRunning is_running)
    {
        while (0 != is_running(user_data))
        {
            step_method(user_data);
        }
    }
}
