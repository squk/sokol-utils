#define SOKOL_IMPL
/* GPU pass timings hook sokol_gfx through its trace hooks. */
#define SOKOL_TRACE_HOOKS
#include "sokol_gfx.h"
#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"
#include "utest.h"

UTEST(sokol_gfx_utils, flush_commands_without_context) {
    EXPECT_FALSE(sg_flush_commands());
}

#if defined(SOKOL_METAL) || defined(SOKOL_D3D11) || defined(SOKOL_VULKAN)
#include <stdio.h>
#if defined(SOKOL_D3D11)
static ID3D11Device* flush_test_device;
static ID3D11DeviceContext* flush_test_context;
#elif defined(SOKOL_VULKAN)
static struct {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkDebugUtilsMessengerEXT messenger;
} flush_test_vk;
/* Validation errors since setup, when VK_LAYER_KHRONOS_validation is installed. */
static int flush_test_vk_errors;

static VKAPI_ATTR VkBool32 VKAPI_CALL flush_test_vk_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data) {
    (void)types; (void)user_data;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        flush_test_vk_errors++;
        fprintf(stderr, "vulkan validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}

static bool flush_test_vk_has_instance_extension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    VkExtensionProperties exts[128];
    if (count > 128) count = 128;
    vkEnumerateInstanceExtensionProperties(NULL, &count, exts);
    for (uint32_t i = 0; i < count; i++)
        if (strcmp(exts[i].extensionName, name) == 0) return true;
    return false;
}

static bool flush_test_vk_has_layer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    VkLayerProperties layers[64];
    if (count > 64) count = 64;
    vkEnumerateInstanceLayerProperties(&count, layers);
    for (uint32_t i = 0; i < count; i++)
        if (strcmp(layers[i].layerName, name) == 0) return true;
    return false;
}

/* A headless device with what sokol_gfx's Vulkan backend needs, as sokol_app
   creates it (_sapp_vk_create_device) without the swapchain. */
static bool flush_test_vk_create(void) {
    memset(&flush_test_vk, 0, sizeof(flush_test_vk));
    flush_test_vk_errors = 0;
    const bool validation = flush_test_vk_has_layer("VK_LAYER_KHRONOS_validation");
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* instance_ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info;
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    /* sokol_gfx names its objects through VK_EXT_debug_utils and fails setup
       without it, as sokol_app enables it; the layer is optional. */
    if (!flush_test_vk_has_instance_extension(instance_ext)) return false;
    instance_info.enabledExtensionCount = 1;
    instance_info.ppEnabledExtensionNames = &instance_ext;
    if (validation) {
        instance_info.enabledLayerCount = 1;
        instance_info.ppEnabledLayerNames = &layer;
    }
    if (vkCreateInstance(&instance_info, NULL, &flush_test_vk.instance) != VK_SUCCESS) return false;
    if (validation) {
        PFN_vkCreateDebugUtilsMessengerEXT create_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(flush_test_vk.instance, "vkCreateDebugUtilsMessengerEXT");
        VkDebugUtilsMessengerCreateInfoEXT messenger_info;
        memset(&messenger_info, 0, sizeof(messenger_info));
        messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        messenger_info.pfnUserCallback = flush_test_vk_message;
        if (create_messenger)
            create_messenger(flush_test_vk.instance, &messenger_info, NULL, &flush_test_vk.messenger);
    }
    uint32_t device_count = 8;
    VkPhysicalDevice devices[8];
    if (vkEnumeratePhysicalDevices(flush_test_vk.instance, &device_count, devices) < 0 || device_count == 0)
        return false;
    for (uint32_t d = 0; d < device_count && !flush_test_vk.physical_device; d++) {
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &ext_count, NULL);
        VkExtensionProperties* exts = (VkExtensionProperties*)calloc(ext_count, sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &ext_count, exts);
        bool descriptor_buffer = false;
        for (uint32_t e = 0; e < ext_count; e++)
            descriptor_buffer |= strcmp(exts[e].extensionName, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0;
        free(exts);
        if (!descriptor_buffer) continue;
        uint32_t family_count = 16;
        VkQueueFamilyProperties families[16];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);
        for (uint32_t f = 0; f < family_count; f++) {
            if ((families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                flush_test_vk.physical_device = devices[d];
                flush_test_vk.queue_family = f;
                break;
            }
        }
    }
    if (!flush_test_vk.physical_device) return false;
    const float priority = 0.0f;
    VkDeviceQueueCreateInfo queue_info;
    memset(&queue_info, 0, sizeof(queue_info));
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = flush_test_vk.queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer;
    memset(&descriptor_buffer, 0, sizeof(descriptor_buffer));
    descriptor_buffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptor_buffer.descriptorBuffer = VK_TRUE;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state;
    memset(&dynamic_state, 0, sizeof(dynamic_state));
    dynamic_state.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    dynamic_state.pNext = &descriptor_buffer;
    dynamic_state.extendedDynamicState = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vk12;
    memset(&vk12, 0, sizeof(vk12));
    vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12.pNext = &dynamic_state;
    vk12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceVulkan13Features vk13;
    memset(&vk13, 0, sizeof(vk13));
    vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13.pNext = &vk12;
    vk13.dynamicRendering = VK_TRUE;
    vk13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features;
    memset(&features, 0, sizeof(features));
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vk13;
    features.features.samplerAnisotropy = VK_TRUE;
    features.features.fragmentStoresAndAtomics = VK_TRUE;
    const char* device_ext = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME;
    VkDeviceCreateInfo device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = &device_ext;
    if (vkCreateDevice(flush_test_vk.physical_device, &device_info, NULL, &flush_test_vk.device) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(flush_test_vk.device, flush_test_vk.queue_family, 0, &flush_test_vk.queue);
    return true;
}

static void flush_test_vk_destroy(void) {
    if (flush_test_vk.device) vkDestroyDevice(flush_test_vk.device, NULL);
    if (flush_test_vk.messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(flush_test_vk.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger) destroy_messenger(flush_test_vk.instance, flush_test_vk.messenger, NULL);
    }
    if (flush_test_vk.instance) vkDestroyInstance(flush_test_vk.instance, NULL);
    memset(&flush_test_vk, 0, sizeof(flush_test_vk));
}
#endif

/* A real device for sg_flush_commands; false, with a note, when the machine has none. */
static bool flush_test_setup_device(void);
static bool flush_test_setup(void) {
    if (flush_test_setup_device()) return true;
    fprintf(stderr, "flush_commands test skipped: no GPU device\n");
    return false;
}

/* sokol_gfx reports validation and backend errors here; without a logger they are silent. */
static void flush_test_log(const char* tag, uint32_t level, uint32_t id, const char* message, uint32_t line,
                           const char* file, void* user_data) {
    (void)file; (void)user_data;
    fprintf(stderr, "[%s] level %u id %u line %u: %s\n", tag ? tag : "", level, id, line, message ? message : "");
}

static bool flush_test_setup_device(void) {
    sg_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.logger.func = flush_test_log;
#if defined(SOKOL_METAL)
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return false;
    desc.environment.metal.device = (__bridge const void*)device;
#elif defined(SOKOL_D3D11)
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                   &flush_test_device, &level, &flush_test_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                               &flush_test_device, &level, &flush_test_context);
    }
    if (FAILED(hr)) return false;
    desc.environment.d3d11.device = flush_test_device;
    desc.environment.d3d11.device_context = flush_test_context;
#else
    if (!flush_test_vk_create()) {
        flush_test_vk_destroy();
        return false;
    }
    desc.environment.vulkan.instance = flush_test_vk.instance;
    desc.environment.vulkan.physical_device = flush_test_vk.physical_device;
    desc.environment.vulkan.device = flush_test_vk.device;
    desc.environment.vulkan.queue = flush_test_vk.queue;
    desc.environment.vulkan.queue_family_index = flush_test_vk.queue_family;
#endif
    sg_setup(&desc);
    return sg_isvalid();
}

static void flush_test_teardown(void) {
    sg_shutdown();
#if defined(SOKOL_VULKAN)
    flush_test_vk_destroy();
#endif
#if defined(SOKOL_D3D11)
    flush_test_context->lpVtbl->Release(flush_test_context);
    flush_test_device->lpVtbl->Release(flush_test_device);
    flush_test_context = NULL;
    flush_test_device = NULL;
#endif
}

/* An offscreen target for one clear pass, so a frame has work to split. */
static sg_image flush_test_target(sg_view* view) {
    sg_image_desc image_desc;
    memset(&image_desc, 0, sizeof(image_desc));
    image_desc.usage.color_attachment = true;
    image_desc.width = 64;
    image_desc.height = 64;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    sg_image image = sg_make_image(&image_desc);
    sg_view_desc view_desc;
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.color_attachment.image = image;
    *view = sg_make_view(&view_desc);
    return image;
}

static void flush_test_pass(sg_view view) {
    sg_pass pass;
    memset(&pass, 0, sizeof(pass));
    pass.attachments.colors[0] = view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    sg_begin_pass(&pass);
    sg_end_pass();
}

/* Splits are refused inside a pass, and a frame keeps recording after them. */
UTEST(sokol_gfx_utils, flush_commands_continues_frame) {
    if (!flush_test_setup()) return;
    sg_view view;
    sg_image image = flush_test_target(&view);
    for (int frame = 0; frame < 3; frame++) {
        for (int split = 0; split < 8; split++) {
            sg_pass pass;
            memset(&pass, 0, sizeof(pass));
            pass.attachments.colors[0] = view;
            pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            sg_begin_pass(&pass);
            EXPECT_FALSE(sg_flush_commands());
            sg_end_pass();
            EXPECT_TRUE(sg_flush_commands());
        }
        flush_test_pass(view);
        sg_commit();
    }
#if defined(SOKOL_D3D11)
    EXPECT_TRUE(flush_test_device->lpVtbl->GetDeviceRemovedReason(flush_test_device) == S_OK);
#endif
    sg_destroy_view(view);
    sg_destroy_image(image);
    flush_test_teardown();
}
#endif

#if defined(SOKOL_D3D11)
/* Copies a 2D R32UI image into a staging texture and reads texel (x, y). */
static uint32_t fstore_test_read_texel(sg_image image, int x, int y) {
    _sg_image_t* img = _sg_lookup_image(image.id);
    if (!img) return 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    img->d3d11.tex2d->lpVtbl->GetDesc(img->d3d11.tex2d, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = 0;
    if (FAILED(flush_test_device->lpVtbl->CreateTexture2D(flush_test_device, &desc, 0, &staging))) return 0xFFFFFFFFu;
    flush_test_context->lpVtbl->CopyResource(flush_test_context, (ID3D11Resource*)staging, img->d3d11.res);
    D3D11_MAPPED_SUBRESOURCE mapped;
    uint32_t value = 0xFFFFFFFFu;
    if (SUCCEEDED(flush_test_context->lpVtbl->Map(flush_test_context, (ID3D11Resource*)staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        value = ((const uint32_t*)((const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch))[x];
        flush_test_context->lpVtbl->Unmap(flush_test_context, (ID3D11Resource*)staging, 0);
    }
    staging->lpVtbl->Release(staging);
    return value;
}

/* A pixel shader writes a storage image at register u4 next to its render
   target; every texel holds its own coordinates afterwards, and the image can
   be read once sg_fragment_storage_end unbinds the UAV. A register past the
   UAV slots is refused when the shader is made. */
UTEST(sokol_gfx_utils, fragment_storage_d3d11_writes_image) {
    if (!flush_test_setup()) return;
    EXPECT_TRUE(sg_fragment_storage_supported());
    EXPECT_FALSE(sg_fragment_storage_raster_order_supported());
    sg_view color_view;
    sg_image color = flush_test_target(&color_view);
    sg_image_desc storage_desc;
    memset(&storage_desc, 0, sizeof(storage_desc));
    storage_desc.usage.storage_image = true;
    storage_desc.width = 64;
    storage_desc.height = 64;
    storage_desc.pixel_format = SG_PIXELFORMAT_R32UI;
    sg_image storage = sg_make_image(&storage_desc);
    sg_view_desc storage_view_desc;
    memset(&storage_view_desc, 0, sizeof(storage_view_desc));
    storage_view_desc.storage_image.image = storage;
    sg_view storage_view = sg_make_view(&storage_view_desc);

    sg_shader_desc shader_desc;
    memset(&shader_desc, 0, sizeof(shader_desc));
    shader_desc.vertex_func.source =
        "float4 main(uint id : SV_VertexID) : SV_Position {\n"
        "    return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0.0, 1.0);\n"
        "}\n";
    shader_desc.vertex_func.d3d11_target = "vs_5_0";
    shader_desc.fragment_func.source =
        "RWTexture2D<uint> marks : register(u4);\n"
        "float4 main(float4 position : SV_Position) : SV_Target0 {\n"
        "    uint2 pixel = uint2(position.xy);\n"
        "    marks[pixel] = pixel.y * 1000u + pixel.x + 1u;\n"
        "    return float4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n";
    shader_desc.fragment_func.d3d11_target = "ps_5_0";
    sg_fragment_storage_layout_desc layout;
    memset(&layout, 0, sizeof(layout));
    layout.num_slots = 1;
    layout.slots[0].slot = 0;
    layout.slots[0].msl_texture_n = -1;
    layout.slots[0].vk_binding_n = -1;
    layout.slots[0].hlsl_register_u_n = 4;
    sg_shader shader = sg_make_shader_fragment_storage(&shader_desc, &layout);
    EXPECT_NE(shader.id, (uint32_t)SG_INVALID_ID);
    sg_pipeline_desc pipeline_desc;
    memset(&pipeline_desc, 0, sizeof(pipeline_desc));
    pipeline_desc.shader = shader;
    pipeline_desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    pipeline_desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    sg_pipeline pipeline = sg_make_pipeline(&pipeline_desc);

    /* A register past the output merger's UAV slots is refused. */
    sg_fragment_storage_layout_desc low = layout;
    low.slots[0].hlsl_register_u_n = SG_MAX_VIEW_BINDSLOTS;
    sg_shader rejected = sg_make_shader_fragment_storage(&shader_desc, &low);
    EXPECT_EQ(rejected.id, (uint32_t)SG_INVALID_ID);

    EXPECT_TRUE(sg_fragment_storage_begin(storage_view));
    sg_pass pass;
    memset(&pass, 0, sizeof(pass));
    pass.attachments.colors[0] = color_view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    sg_begin_pass(&pass);
    sg_apply_pipeline(pipeline);
    /* The shader has no sokol_gfx resources, so there is no sg_apply_bindings. */
    sg_fragment_storage_bindings fstore;
    memset(&fstore, 0, sizeof(fstore));
    fstore.images[0] = storage_view;
    EXPECT_TRUE(sg_fragment_storage_bind(&fstore));
    sg_draw(0, 3, 1);
    sg_end_pass();
    EXPECT_TRUE(sg_fragment_storage_end(storage_view));
    sg_commit();

    EXPECT_EQ(fstore_test_read_texel(storage, 0, 0), 1u);
    EXPECT_EQ(fstore_test_read_texel(storage, 5, 3), 3006u);
    EXPECT_EQ(fstore_test_read_texel(storage, 63, 63), 63064u);
    EXPECT_TRUE(flush_test_device->lpVtbl->GetDeviceRemovedReason(flush_test_device) == S_OK);

    sg_destroy_pipeline(pipeline);
    sg_destroy_shader(shader);
    sg_destroy_view(storage_view);
    sg_destroy_image(storage);
    sg_destroy_view(color_view);
    sg_destroy_image(color);
    flush_test_teardown();
    sg_fragment_storage_shutdown();
}
#endif

#if defined(SOKOL_VULKAN)
/* Splits stay in the frame's slot: the slot moves only at sg_commit, spare
   command buffers grow past their first allocation and are reused by later
   frames of the slot, and the validation layer reports no error. */
UTEST(sokol_gfx_utils, flush_commands_vulkan_takes_no_frame_slot) {
    if (!flush_test_setup()) return;
    sg_view view;
    sg_image image = flush_test_target(&view);
    const int splits = 9;
    for (int frame = 0; frame < 3 * SG_NUM_INFLIGHT_FRAMES; frame++) {
        flush_test_pass(view);
        const uint32_t slot = _sg.vk.frame_slot;
        for (int split = 0; split < splits; split++) {
            EXPECT_TRUE(sg_flush_commands());
            flush_test_pass(view);
            EXPECT_EQ(_sg.vk.frame_slot, slot);
        }
        EXPECT_EQ(_sgu_vk_split.used[slot], splits);
        EXPECT_GE(_sgu_vk_split.capacity[slot], splits);
        sg_commit();
    }
    vkDeviceWaitIdle(flush_test_vk.device);
    EXPECT_EQ(flush_test_vk_errors, 0);
    sg_destroy_view(view);
    sg_destroy_image(image);
    flush_test_teardown();
}
#endif

#if defined(SOKOL_METAL)
#include <time.h>
#include <unistd.h>

static double flush_test_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

/* Waits until every submitted buffer finished and its completed handlers ran,
   then checks that the frame semaphore is back at SG_NUM_INFLIGHT_FRAMES. */
static bool flush_test_semaphore_balanced(void) {
    id<MTLCommandBuffer> drain = [_sg.mtl.cmd_queue commandBuffer];
    [drain commit];
    [drain waitUntilCompleted];
    int taken = 0;
    while (taken < SG_NUM_INFLIGHT_FRAMES &&
           dispatch_semaphore_wait(_sg.mtl.sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0) {
        taken++;
    }
    const bool extra = taken == SG_NUM_INFLIGHT_FRAMES &&
        dispatch_semaphore_wait(_sg.mtl.sem, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) == 0;
    if (extra) taken++;
    for (int i = 0; i < taken; i++) dispatch_semaphore_signal(_sg.mtl.sem);
    return taken == SG_NUM_INFLIGHT_FRAMES && !extra;
}

/* A split continues the frame in a new command buffer; with no pass since the
   last split or commit there is nothing to submit. */
UTEST(sokol_gfx_utils, flush_commands_metal_splits_frame) {
    if (!flush_test_setup()) return;
    sg_view view;
    sg_image image = flush_test_target(&view);
    EXPECT_TRUE(sg_flush_commands());
    EXPECT_TRUE(_sg.mtl.cmd_buffer == nil);
    flush_test_pass(view);
    id<MTLCommandBuffer> first = _sg.mtl.cmd_buffer;
    EXPECT_TRUE(first != nil);
    EXPECT_TRUE(sg_flush_commands());
    EXPECT_TRUE(_sg.mtl.cmd_buffer != nil);
    EXPECT_TRUE(_sg.mtl.cmd_buffer != first);
    EXPECT_TRUE(_sgu_mtl_split.tail != NULL);
    flush_test_pass(view);
    sg_commit();
    EXPECT_TRUE(_sgu_mtl_split.tail == NULL);
    EXPECT_TRUE(flush_test_semaphore_balanced());
    sg_destroy_view(view);
    sg_destroy_image(image);
    flush_test_teardown();
}

/* Many splits per frame over many frames: an extra semaphore wait would block
   the frame (deadlocking it once SG_NUM_INFLIGHT_FRAMES buffers are pending),
   and an extra signal would let a frame start before its uniform ring slot is
   free. After each commit the frame that last used the next slot has finished. */
/* Timings of a split frame wait for its last command buffer and keep the first
   for the frame's start, and the "Frame" total still arrives. */
UTEST(sokol_gfx_utils, flush_commands_metal_timings_cover_split_frame) {
    if (!flush_test_setup()) return;
    if (!sg_gpu_pass_timings_setup()) {
        fprintf(stderr, "flush_commands timing test skipped: no GPU timestamps\n");
        flush_test_teardown();
        return;
    }
    sg_gpu_pass_timings_set_enabled(true);
    sg_view view;
    sg_image image = flush_test_target(&view);
    flush_test_pass(view);
    id<MTLCommandBuffer> first = _sg.mtl.cmd_buffer;
    EXPECT_TRUE(_sgu_mtl_gpu_timing_state.current.command_buffer == first);
    EXPECT_TRUE(sg_flush_commands());
    flush_test_pass(view);
    EXPECT_TRUE(_sgu_mtl_gpu_timing_state.current.first_command_buffer == first);
    EXPECT_TRUE(_sgu_mtl_gpu_timing_state.current.command_buffer == _sg.mtl.cmd_buffer);
    EXPECT_TRUE(_sg.mtl.cmd_buffer != first);
    sg_commit();
    EXPECT_TRUE(flush_test_semaphore_balanced());
    sg_gpu_pass_timing timings[SG_GPU_PASS_TIMING_MAX_PASSES + 1];
    int count = 0;
    for (int attempt = 0; attempt < 100 && count == 0; attempt++) {
        count = sg_gpu_pass_timings_poll(timings, SG_GPU_PASS_TIMING_MAX_PASSES + 1);
        if (count == 0) usleep(10000);
    }
    EXPECT_GE(count, 1);
    if (count >= 1) {
        EXPECT_STREQ(timings[0].label, "Frame");
        EXPECT_GT(timings[0].milliseconds, 0.0);
    }
    sg_gpu_pass_timings_set_enabled(false);
    sg_destroy_view(view);
    sg_destroy_image(image);
    flush_test_teardown();
    sg_gpu_pass_timings_shutdown();
}

UTEST(sokol_gfx_utils, flush_commands_metal_takes_no_frame_slot) {
    if (!flush_test_setup()) return;
    sg_view view;
    sg_image image = flush_test_target(&view);
    const double start = flush_test_seconds();
    for (int frame = 0; frame < 3 * SG_NUM_INFLIGHT_FRAMES + 1; frame++) {
        for (int split = 0; split < 4 * SG_NUM_INFLIGHT_FRAMES; split++) {
            flush_test_pass(view);
            EXPECT_TRUE(sg_flush_commands());
        }
        flush_test_pass(view);
        sg_commit();
        EXPECT_TRUE(_sgu_mtl_split.slot_tail[_sg.mtl.cur_frame_rotate_index] == NULL);
    }
    EXPECT_LT(flush_test_seconds() - start, 10.0);
    EXPECT_TRUE(flush_test_semaphore_balanced());
    sg_destroy_view(view);
    sg_destroy_image(image);
    flush_test_teardown();
}
#endif

UTEST(sokol_gfx_utils, readback_request_null) {
    sg_image img = { .id = 0 };
    void* req = sg_readback_request(img);
    EXPECT_TRUE(req == NULL);
}

UTEST(sokol_gfx_utils, ray_query_unsupported_without_context) {
    EXPECT_FALSE(sg_ray_query_supported());
}

UTEST(sokol_gfx_utils, accel_invalid_arguments) {
    sg_blas_desc blas_desc = { 0 };
    sg_tlas_desc tlas_desc = { 0 };
    EXPECT_TRUE(sg_make_blas(NULL) == NULL);
    EXPECT_TRUE(sg_make_blas(&blas_desc) == NULL);
    EXPECT_TRUE(sg_make_tlas(&tlas_desc) == NULL);
    EXPECT_FALSE(sg_build_blas(NULL));
    EXPECT_FALSE(sg_accel_bind(NULL, 0));
    EXPECT_FALSE(sg_accel_built(NULL));
    sg_destroy_accel(NULL);
}

UTEST(sokol_gfx_utils, multi_geometry_blas_invalid_arguments) {
    sg_blas_geometry geos[2];
    memset(geos, 0, sizeof(geos));
    sg_blas_desc desc = { 0 };
    desc.geometries = geos;
    desc.geometry_count = 2;
    EXPECT_TRUE(sg_make_blas(&desc) == NULL);
    desc.geometry_count = 0;
    EXPECT_TRUE(sg_make_blas(&desc) == NULL);
    EXPECT_FALSE(sg_blas_set_geometry_vertex_buffer(NULL, 0, (sg_buffer){ 0 }, 0));
    EXPECT_FALSE(sg_blas_set_vertex_buffer(NULL, (sg_buffer){ 0 }, 0));
}

UTEST(sokol_gfx_utils, gpu_tlas_build_invalid_arguments) {
    EXPECT_EQ(sizeof(sg_gpu_accel_instance), (size_t)64);
    EXPECT_FALSE(sg_build_tlas_from_buffer(NULL, (sg_buffer){ 0 }, 0, 1));
    EXPECT_FALSE(sg_refit_tlas_from_buffer(NULL, (sg_buffer){ 0 }, 0, 1));
}

UTEST(sokol_gfx_utils, bindless_unsupported_without_context) {
    EXPECT_FALSE(sg_bindless_supported());
    EXPECT_EQ(sg_bindless_max_capacity(SG_BINDLESS_KIND_TEXTURES), 0);
    sg_bindless_table_desc desc = { 0 };
    desc.kind = SG_BINDLESS_KIND_TEXTURES;
    desc.capacity = 4;
    EXPECT_TRUE(sg_make_bindless_table(&desc) == NULL);
    EXPECT_TRUE(sg_make_bindless_table(NULL) == NULL);
    EXPECT_FALSE(sg_bindless_set_texture(NULL, 0, (sg_view){ 0 }));
    EXPECT_FALSE(sg_bindless_set_sampler(NULL, 0, (sg_sampler){ 0 }));
    EXPECT_FALSE(sg_bindless_clear_entry(NULL, 0));
    EXPECT_FALSE(sg_bindless_prepare_table(NULL));
    EXPECT_FALSE(sg_bindless_bind(NULL, 0));
    EXPECT_EQ(sg_make_shader_bindless(NULL, NULL).id, (uint32_t)SG_INVALID_ID);
    EXPECT_EQ(sg_make_shader_ext(NULL, NULL).id, (uint32_t)SG_INVALID_ID);
    sg_destroy_bindless_table(NULL);
}
