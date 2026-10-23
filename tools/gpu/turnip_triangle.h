#ifndef TRIANGLE_WIDTH
#define TRIANGLE_WIDTH 64
#endif
#ifndef TRIANGLE_HEIGHT
#define TRIANGLE_HEIGHT 64
#endif
_Static_assert(TRIANGLE_WIDTH >= 64 && TRIANGLE_WIDTH <= 8192 && TRIANGLE_HEIGHT >= 64 && TRIANGLE_HEIGHT <= 8192, "Probe dimensions out of range");

static VkShaderModule probe_shader(VkDevice device, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return VK_NULL_HANDLE;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return VK_NULL_HANDLE; }
    long size = ftell(file);
    if (size <= 0 || size > 1024 * 1024 || size % 4) { fclose(file); return VK_NULL_HANDLE; }
    rewind(file);
    uint32_t *code = malloc((size_t)size);
    if (!code) { fclose(file); return VK_NULL_HANDLE; }
    size_t got = fread(code, 1, (size_t)size, file);
    fclose(file);
    VkShaderModule shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)size, .pCode = code};
    if (got == (size_t)size) vkCreateShaderModule(device, &info, NULL, &shader);
    free(code);
    return shader;
}

static int triangle(VkDevice device, VkPhysicalDevice gpu, VkQueue queue, uint32_t family,
                    VkBuffer readback, uint32_t *mapped, const char *vertexPath, const char *fragmentPath) {
    VkImageCreateInfo imageInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {TRIANGLE_WIDTH, TRIANGLE_HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage image;
    CHECK(vkCreateImage(device, &imageInfo, NULL, &image));
    VkMemoryRequirements required;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetImageMemoryRequirements(device, image, &required);
    vkGetPhysicalDeviceMemoryProperties(gpu, &memoryProperties);
    uint32_t type;
    for (type = 0; type < memoryProperties.memoryTypeCount; ++type)
        if ((required.memoryTypeBits & (1u << type)) && (memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
    if (type == memoryProperties.memoryTypeCount) return 1;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = required.size, .memoryTypeIndex = type};
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &memory));
    CHECK(vkBindImageMemory(device, image, memory, 0));
    VkImageSubresourceRange range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};
    VkImageViewCreateInfo viewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageInfo.format, .subresourceRange = range};
    VkImageView view;
    CHECK(vkCreateImageView(device, &viewInfo, NULL, &view));
    VkAttachmentDescription attachment = {.format = imageInfo.format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference colorReference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorReference};
    VkRenderPassCreateInfo passInfo = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass};
    VkRenderPass pass;
    CHECK(vkCreateRenderPass(device, &passInfo, NULL, &pass));
    VkFramebufferCreateInfo framebufferInfo = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass, .attachmentCount = 1, .pAttachments = &view, .width = TRIANGLE_WIDTH, .height = TRIANGLE_HEIGHT, .layers = 1};
    VkFramebuffer framebuffer;
    CHECK(vkCreateFramebuffer(device, &framebufferInfo, NULL, &framebuffer));
    VkShaderModule vertex = probe_shader(device, vertexPath), fragment = probe_shader(device, fragmentPath);
    if (!vertex || !fragment) return 1;
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vertices = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.width = TRIANGLE_WIDTH, .height = TRIANGLE_HEIGHT, .maxDepth = 1};
    VkRect2D scissor = {.extent = {TRIANGLE_WIDTH, TRIANGLE_HEIGHT}};
    VkPipelineViewportStateCreateInfo viewportInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
    VkPipelineLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(device, &layoutInfo, NULL, &layout));
    VkGraphicsPipelineCreateInfo pipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertices, .pInputAssemblyState = &assembly,
        .pViewportState = &viewportInfo, .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &blending, .layout = layout, .renderPass = pass};
    VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline));
    VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &pool));
    VkCommandBufferAllocateInfo commandsInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &commandsInfo, &command));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    VkClearValue clear = {.color = {.float32 = {0, 0, 1, 1}}};
    VkRenderPassBeginInfo passBegin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = pass,
        .framebuffer = framebuffer, .renderArea = scissor, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
    VkImageMemoryBarrier transition = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = range};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &transition);
    VkBufferImageCopy copy = {.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1}, .imageExtent = {TRIANGLE_WIDTH, TRIANGLE_HEIGHT, 1}};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    VkMemoryBarrier visible = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &visible, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fenceInfo, NULL, &fence));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(2000000000));
    printf("triangle fence=%d\n", waited);
    if (waited != VK_SUCCESS) _Exit(124);
    unsigned red = 0, blue = 0, mismatches[2] = {0, 0};
    const unsigned pixels = TRIANGLE_WIDTH * TRIANGLE_HEIGHT;
    const double margin = 3.0 / TRIANGLE_WIDTH + 3.0 / TRIANGLE_HEIGHT;
    for (unsigned i = 0; i < pixels; ++i) {
        if (mapped[i] == UINT32_C(0xff0000ff)) ++red;
        if (mapped[i] == UINT32_C(0xffff0000)) ++blue;
        unsigned x = i % TRIANGLE_WIDTH, y = i / TRIANGLE_WIDTH;
        double nx = (2.0 * x + 1) / TRIANGLE_WIDTH - 1;
        double ny = (2.0 * y + 1) / TRIANGLE_HEIGHT - 1;
        double edge = (0.75 - ny) / 2 - (nx < 0 ? -nx : nx);
        if (ny > -0.75 + margin && ny < 0.75 - margin && edge > margin) {
            if (mapped[i] != UINT32_C(0xff0000ff)) ++mismatches[x >= TRIANGLE_WIDTH / 2];
        } else if (ny < -0.75 - margin || ny > 0.75 + margin || edge < -margin) {
            if (mapped[i] != UINT32_C(0xffff0000)) ++mismatches[x >= TRIANGLE_WIDTH / 2];
        }
    }
    unsigned center = (TRIANGLE_HEIGHT / 2) * TRIANGLE_WIDTH + TRIANGLE_WIDTH / 2;
    int passed = red > pixels / 4 && red < pixels / 3 && red + blue == pixels &&
        mapped[center] == UINT32_C(0xff0000ff) && mapped[0] == UINT32_C(0xffff0000) &&
        !mismatches[0] && !mismatches[1];
    printf("TRIANGLE_READBACK size=%ux%u red=%u blue=%u center=0x%x corner=0x%x leftErrors=%u rightErrors=%u passed=%d\n",
        TRIANGLE_WIDTH, TRIANGLE_HEIGHT, red, blue, mapped[center], mapped[0], mismatches[0], mismatches[1], passed);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, pass, NULL);
    vkDestroyImageView(device, view, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL);
    return passed ? 0 : 1;
}
