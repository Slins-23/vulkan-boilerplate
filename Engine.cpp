#include "Engine.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include<tiny_obj_loader.h>


Engine::Engine() {

}

Engine::~Engine() {

}

void Engine::VKCheck(const char* message, VkResult result) {
	 if (result != VK_SUCCESS) {
		 std::cout << message << " " << result << std::endl;
		throw std::runtime_error(message + *" Error code: " + result);
	}
}

void Engine::Load() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	CreateInstance();
	this->window = glfwCreateWindow(static_cast<uint32_t>(this->WIN_W), static_cast<uint32_t>(this->WIN_H), this->TITLE, nullptr, nullptr);
	CreateWindowSurface();
	CreatePhysicalDevice();
	GetQueueFamilies();
	CreateLogicalDevice();
	GetSwapchainDetails(this->physicalDevice, this->swapchainDetails);
	CreateSwapchain(this->swapchain, this->WIN_W, this->WIN_H);
	GetSwapImages(this->swapchain, this->swapImages);
	CreateImageViews();
	CreateRenderPass();
	CreateDescriptorSetLayout();
	CreateGraphicsPipeline();
	CreateCommandPool(this->commandPool, this->queueFamilies.graphicsQF.value());
	CreateCommandPool(this->copyPool, this->queueFamilies.graphicsQF.value());
	CreateColorResources();
	CreateDepthResources();
	CreateFramebuffers();
	CreateTextureImage(this->TEXTURE_PATH);
	//CreateTextureImage("textures/pokeball.png");
	//CreateTextureImage("textures/naruto.jpg");
	CreateTextureImageViews(this->mipLevels);
	CreateTextureSampler();
	CreateModel(this->MODEL_PATH);
	CreateVertexBuffer();
	CreateIndicesBuffer();
	CreateUniformBuffers();
	CreateDescriptorPool();
	CreateDescriptorSets();
	CreateCommandBuffers();
	CreateSyncObjects();
}

static void ResizeCallback(GLFWwindow* window, int width, int height) {
	Engine* application = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	application->resizeTriggered = true;
	application->WIN_W = (size_t)width;
	application->WIN_H = (size_t)height;
}

static void KeyPressCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	Engine* application = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));

	if (action == GLFW_PRESS || action == GLFW_REPEAT) {
		switch (key) {
		case GLFW_KEY_KP_SUBTRACT:
			application->FOV -= 1.0f;
			break;
		case GLFW_KEY_KP_ADD:
			application->FOV += 1.0f;
			break;
		case GLFW_KEY_W:
			break;
		case GLFW_KEY_A:
			break;
		case GLFW_KEY_S:
			break;
		case GLFW_KEY_D:
			break;
		case GLFW_KEY_Q:
			application->ROTATION_ANGLE += 1.0f;
			break;
		case GLFW_KEY_E:
			application->ROTATION_ANGLE -= 1.0f;
			break;
		}
	}
}

void Engine::Start() {

	glfwSetWindowUserPointer(this->window, this);
	glfwSetFramebufferSizeCallback(this->window, ResizeCallback);
	glfwSetKeyCallback(this->window, KeyPressCallback);

	while (!glfwWindowShouldClose(this->window)) {
		glfwPollEvents();

		Render();
	}

	vkDeviceWaitIdle(this->logicalDevice);
}

void Engine::Render() {
	vkWaitForFences(this->logicalDevice, 1, &this->inFlightFences[this->currentFrame], VK_TRUE, UINT64_MAX);
	
	uint32_t imageIndex;

	VkResult result = vkAcquireNextImageKHR(this->logicalDevice, this->swapchain, UINT64_MAX, this->imagesAvailableSemaphores[this->currentFrame], VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RecreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("Could not acquire image.");
	}

	UpdateUniformBuffers(imageIndex);

	if (this->imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
		vkWaitForFences(this->logicalDevice, 1, &this->imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	}

	this->imagesInFlight[imageIndex] = this->inFlightFences[this->currentFrame];


	VkPipelineStageFlags stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore waitSemaphores[] = { this->imagesAvailableSemaphores[this->currentFrame] };
	VkSemaphore signalSemaphores[] = { this->imagesRenderedSemaphores[this->currentFrame] };

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pCommandBuffers = &this->commandBuffers[imageIndex];
	submitInfo.pSignalSemaphores = signalSemaphores;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = stages;
	submitInfo.commandBufferCount = 1;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.waitSemaphoreCount = 1;
	

	vkResetFences(this->logicalDevice, 1, &this->inFlightFences[this->currentFrame]);

	VKCheck("Could not submit queue.", vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, this->inFlightFences[this->currentFrame]));

	std::vector<VkSwapchainKHR> swapchains = { this->swapchain };

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = swapchains.size();
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pSwapchains = swapchains.data();
	presentInfo.pWaitSemaphores = signalSemaphores;
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(this->presentationQueue, &presentInfo);

	if (this->resizeTriggered || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		this->resizeTriggered = false;
		RecreateSwapchain();
	}
	else if (result != VK_SUCCESS) {
		throw std::runtime_error("Could not present queue.");
	}

	this->currentFrame = (this->currentFrame + 1) % this->MAX_CONCURRENT_FRAMES;
}

void Engine::UpdateUniformBuffers(uint32_t currentImage) {
	static std::chrono::time_point startTime = std::chrono::high_resolution_clock::now();

	std::chrono::time_point currentTime = std::chrono::high_resolution_clock::now();

	float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	UniformBufferObject UBO = {};
	//UBO.model = glm::rotate(glm::mat4(1.0f), elapsed * glm::radians(this->ROTATION_ANGLE), this->ROTATION_AXIS);
	UBO.model = glm::rotate(glm::mat4(1.0f), glm::radians(this->ROTATION_ANGLE), this->ROTATION_AXIS);
	UBO.view = glm::lookAt(this->CAMERA_POSITION, this->CENTER, this->UP);
	UBO.projection = glm::perspective(glm::radians(this->FOV), this->swapImageSize.width / (float)this->swapImageSize.height, this->NEAREST, this->FARTHEST);
	UBO.projection[1][1] *= -1;

	void* data;
	vkMapMemory(this->logicalDevice, this->uniformBuffersMemory[currentImage], 0, sizeof(UBO), 0, &data);
	memcpy(data, &UBO, sizeof(UBO));
	vkUnmapMemory(this->logicalDevice, this->uniformBuffersMemory[currentImage]);
}

void Engine::RecreateSwapchain() {
	int width, height = 0;

	while (this->WIN_W == 0 || this->WIN_H == 0) {
		glfwGetFramebufferSize(this->window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(this->logicalDevice);
	CloseSwapchain();

	GetSwapchainDetails(this->physicalDevice, this->swapchainDetails);
	CreateSwapchain(this->swapchain, this->WIN_W, this->WIN_H);
	GetSwapImages(this->swapchain, this->swapImages);
	CreateImageViews();
	CreateRenderPass();
	CreateGraphicsPipeline();
	CreateColorResources();
	CreateDepthResources();
	CreateFramebuffers();
	//CreateTextureImage(this->TEXTURE_PATH);
	//CreateTextureImage("textures/pokeball.png");
	//CreateTextureImage("textures/pokeball.png");
	//CreateTextureImage("textures/naruto.jpg");
	//CreateTextureImageViews(this->mipLevels);
	CreateUniformBuffers();
	CreateDescriptorPool();
	CreateDescriptorSets();
	CreateCommandBuffers();
}

void Engine::CloseSwapchain() {
	vkDestroyImage(this->logicalDevice, this->depthImage, nullptr);
	vkDestroyImageView(this->logicalDevice, this->depthImageView, nullptr);
	vkFreeMemory(this->logicalDevice, this->depthImageMemory, nullptr);

	vkDestroyImage(this->logicalDevice, this->colorImage, nullptr);
	vkDestroyImageView(this->logicalDevice, this->colorImageView, nullptr);
	vkFreeMemory(this->logicalDevice, this->colorImageMemory, nullptr);

	//this->textureImages.clear();
	//this->textureImageViews.clear();
	//this->textureImagesMemory.clear();

	DestroyFramebuffers();
	vkFreeCommandBuffers(this->logicalDevice, this->commandPool, static_cast<uint32_t>(this->commandBuffers.size()), this->commandBuffers.data());
	vkDestroyPipeline(this->logicalDevice, this->pipeline, nullptr);
	vkDestroyPipelineLayout(this->logicalDevice, this->pipelineLayout, nullptr);
	vkDestroyRenderPass(this->logicalDevice, this->renderPass, nullptr);
	DestroySwapImageViews();
	vkDestroySwapchainKHR(this->logicalDevice, this->swapchain, nullptr);

	for (int i = 0; i < this->swapImages.size(); i++) {
		vkDestroyBuffer(this->logicalDevice, this->uniformBuffers[i], nullptr);
		vkFreeMemory(this->logicalDevice, this->uniformBuffersMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(this->logicalDevice, this->descriptorPool, nullptr);
}

void Engine::Close() {
	CloseSwapchain();

	vkDestroySampler(this->logicalDevice, this->sampler, nullptr);

	for (int i = 0; i < this->textureImages.size(); i++) {
		vkDestroyImage(this->logicalDevice, this->textureImages[i], nullptr);
		vkDestroyImageView(this->logicalDevice, this->textureImageViews[i], nullptr);
		vkFreeMemory(this->logicalDevice, this->textureImagesMemory[i], nullptr);
	}

	this->textureImages.clear();
	this->textureImageViews.clear();
	this->textureImagesMemory.clear();

	vkDestroyDescriptorSetLayout(this->logicalDevice, this->descriptorSetLayout, nullptr);
	DestroySyncObjects();
	vkDestroyBuffer(this->logicalDevice, this->indicesBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, this->indicesMemory, nullptr);
	vkDestroyBuffer(this->logicalDevice, this->vertexBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, this->vertexMemory, nullptr);
	vkDestroyCommandPool(this->logicalDevice, this->commandPool, nullptr);
	vkDestroyCommandPool(this->logicalDevice, this->copyPool, nullptr);
	vkDestroyDevice(this->logicalDevice, nullptr);
	vkDestroySurfaceKHR(this->instance, this->surface, nullptr);
	vkDestroyInstance(this->instance, nullptr);
	glfwDestroyWindow(this->window);
	glfwTerminate();
}

void Engine::DestroySyncObjects() {
	for (size_t i = 0; i < this->MAX_CONCURRENT_FRAMES; i++) {
		vkDestroySemaphore(this->logicalDevice, this->imagesAvailableSemaphores[i], nullptr);
		vkDestroySemaphore(this->logicalDevice, this->imagesRenderedSemaphores[i], nullptr);
		vkDestroyFence(this->logicalDevice, this->inFlightFences[i], nullptr);
	}
}

void Engine::DestroyFramebuffers() {
	for (VkFramebuffer fb : this->framebuffers) {
		vkDestroyFramebuffer(this->logicalDevice, fb, nullptr);
	}
}

void Engine::DestroySwapImageViews() {
	for (VkImageView imageView : this->swapImageViews) {
		vkDestroyImageView(this->logicalDevice, imageView, nullptr);
	}
}

void Engine::DestroyTextureImageViews() {
	for (VkImageView imageView : this->textureImageViews) {
		vkDestroyImageView(this->logicalDevice, imageView, nullptr);
	}
}

void Engine::ValidateDebugLayers(const std::vector<const char*>& debugLayers) {
	uint32_t availableLayersCount = 0;
	vkEnumerateInstanceLayerProperties(&availableLayersCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(availableLayersCount);
	vkEnumerateInstanceLayerProperties(&availableLayersCount, availableLayers.data());

	std::set<std::string> remainingLayers(debugLayers.begin(), debugLayers.end());

	for (VkLayerProperties layerProperty : availableLayers) {
		remainingLayers.erase(layerProperty.layerName);
	}

	if (!remainingLayers.empty()) {
		for (std::string layer : remainingLayers) {
			std::cout << "Error Layer: " << layer << std::endl;
		}

		throw std::runtime_error("Invalid/Incompatible debug layers.");
	}
}

bool Engine::ValidateDeviceExtensions(VkPhysicalDevice& physicalDevice, const std::vector<const char*>& deviceExtensions) {
	uint32_t availableExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableExtensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableExtensionCount, availableExtensions.data());

	std::set<std::string> remainingExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (VkExtensionProperties availableExtension : availableExtensions) {
		remainingExtensions.erase(availableExtension.extensionName);
	}

	if (!remainingExtensions.empty()) {
		std::cout << "Device incompatible with device extensions or invalid extensions." << std::endl;

		for (std::string extName : remainingExtensions) {
			std::cout << "Extension: " << extName << std::endl;
		}

		return false;
	}

	return true;
}

std::vector<char> Engine::ReadFile(const std::string& fileName) {
	std::ifstream file(fileName, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Could not open file " + fileName);
	}

	size_t fileSize = (size_t)file.tellg();

	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

VkSurfaceFormatKHR Engine::GetSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
	for (const VkSurfaceFormatKHR surfaceFormat : formats) {
		if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return surfaceFormat;
		}
	}

	return formats[0];
}

VkPresentModeKHR Engine::GetSurfacePresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
	for (int i = 0; i < presentModes.size(); i++) {
		if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			return presentModes[i];
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Engine::GetSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, size_t& WIN_W, size_t& WIN_H) {

	if (capabilities.currentExtent.width != UINT32_MAX) {
		return capabilities.currentExtent;
	}
	else {
		VkExtent2D extent = { static_cast<uint32_t>(WIN_W), static_cast<uint32_t>(WIN_H) };

		extent.width = max(capabilities.minImageExtent.width, min(capabilities.maxImageExtent.width, extent.width));
		extent.height = max(capabilities.minImageExtent.height, min(capabilities.maxImageExtent.height, extent.height));

		return extent;
	}
}

VkFormat Engine::GetSupportedFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (VkFormat format : formats) {
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(this->physicalDevice, format, &properties);

		if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("Could not find an appropriate format.");
}

VkShaderModule Engine::CreateShaderModule(const std::vector<char>& byteCode) {
	VkShaderModule shaderModule;

	VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
	shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderModuleCreateInfo.codeSize = byteCode.size();
	shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(byteCode.data());

	VKCheck("Could not create shader module.", vkCreateShaderModule(this->logicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule));

	return shaderModule;
}

VkBuffer Engine::CreateBuffer(VkDeviceMemory& bufferMemory, VkDeviceSize& size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlagBits) {
	VkBuffer buffer;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usageFlags;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VKCheck("Could not create buffer.", vkCreateBuffer(this->logicalDevice, &bufferInfo, nullptr, &buffer));

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(this->logicalDevice, buffer, &memoryRequirements);

	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	VKCheck("Could not allocate buffer memory.", vkAllocateMemory(this->logicalDevice, &allocateInfo, nullptr, &bufferMemory));

	vkBindBufferMemory(this->logicalDevice, buffer, bufferMemory, 0);

	return buffer;
}

uint32_t Engine::GetMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
		if (typeFilter & (1 << i) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("Could not find an appropriate memory type.");
}

VkSampleCountFlagBits Engine::GetMSAASupport() {
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(this->physicalDevice, &properties);

	VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

void Engine::GetQueueFamilies() {
	uint32_t qfCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &qfCount, nullptr);

	std::vector<VkQueueFamilyProperties> qfProperties(qfCount);
	vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &qfCount, qfProperties.data());

	int idx = 0;

	for (VkQueueFamilyProperties qfProperty : qfProperties) {
		if (qfProperty.queueFlags && VK_QUEUE_GRAPHICS_BIT) {
			this->queueFamilies.graphicsQF = idx;
			this->queueFamilies.count++;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(this->physicalDevice, idx, this->surface, &presentSupport);

		if (presentSupport) {
			this->queueFamilies.presentationQF = idx;

			if (idx != this->queueFamilies.graphicsQF.value()) {
				this->queueFamilies.count++;
			}
		}

		if (this->queueFamilies.isComplete()) {
			break;
		}

		idx++;
	}
}

void Engine::GetSwapchainDetails(VkPhysicalDevice& physicalDevice, SwapchainDetails& details) {
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, this->surface, &details.capabilities);

	uint32_t formatsCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, this->surface, &formatsCount, nullptr);

	if (formatsCount != 0) {
		details.formats.resize(formatsCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, this->surface, &formatsCount, details.formats.data());
	}
	else {
		throw std::runtime_error("Could not get swapchain format details.");
	}

	uint32_t presentModesCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->surface, &presentModesCount, nullptr);

	if (presentModesCount != 0) {
		details.presentModes.resize(presentModesCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->surface, &presentModesCount, details.presentModes.data());
	}
	else {
		throw std::runtime_error("Could not get swapchain present modes details.");
	}
}

void Engine::GetSwapImages(VkSwapchainKHR& swapchain, std::vector<VkImage>& swapImages) {
	uint32_t swapImageCount = 0;
	vkGetSwapchainImagesKHR(this->logicalDevice, swapchain, &swapImageCount, nullptr);

	swapImages.resize(swapImageCount);
	vkGetSwapchainImagesKHR(this->logicalDevice, swapchain, &swapImageCount, swapImages.data());
}

void Engine::CreateInstance() {
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.apiVersion = VK_API_VERSION_1_2;
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pApplicationName = "Vulkan Renderer";
	appInfo.pEngineName = this->TITLE;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	uint32_t extensionCount = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	createInfo.ppEnabledExtensionNames = extensions;
	createInfo.enabledExtensionCount = extensionCount;

#ifdef _DEBUG

	ValidateDebugLayers(this->debugLayers);

	createInfo.enabledLayerCount = static_cast<uint32_t>(this->debugLayers.size());
	createInfo.ppEnabledLayerNames = this->debugLayers.data();
#endif

	VKCheck("Could not create instance.", vkCreateInstance(&createInfo, nullptr, &this->instance));
}

void Engine::CreateWindowSurface() {
	VkWin32SurfaceCreateInfoKHR winCreateSurfaceInfo = {};
	winCreateSurfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	winCreateSurfaceInfo.hinstance = GetModuleHandle(0);
	winCreateSurfaceInfo.hwnd = glfwGetWin32Window(this->window);

	VKCheck("Could not create Win32 surface.", vkCreateWin32SurfaceKHR(this->instance, &winCreateSurfaceInfo, nullptr, &this->surface));
}

void Engine::CreatePhysicalDevice() {
	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(this->instance, &physicalDeviceCount, nullptr);

	if (physicalDeviceCount == 0) {
		throw std::runtime_error("Could not find any compatible device (1).");
	}

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
	vkEnumeratePhysicalDevices(this->instance, &physicalDeviceCount, physicalDevices.data());

	for (VkPhysicalDevice device : physicalDevices) {
		VkPhysicalDeviceProperties physicalDeviceProperties;
		VkPhysicalDeviceFeatures physicalDeviceFeatures;

		vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);
		vkGetPhysicalDeviceFeatures(device, &physicalDeviceFeatures);

		if (!ValidateDeviceExtensions(device, this->deviceExtensions)) {
			continue;
		}

		bool supportsSwapchain = false;
		SwapchainDetails details = {};
		GetSwapchainDetails(device, details);

		if (!details.formats.empty() && !details.presentModes.empty()) {
			supportsSwapchain = true;
		}

		if (supportsSwapchain && physicalDeviceFeatures.samplerAnisotropy && physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			std::cout << "Using GPU: " << physicalDeviceProperties.deviceName << std::endl;
			this->physicalDevice = device;
			this->msaaSamples = GetMSAASupport();
			return;
		}
	}

	if (physicalDevices.size() > 0) {
		VkPhysicalDeviceProperties physicalDeviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevices[0], &physicalDeviceProperties);
		std::cout << "Using Fallback Device: " << physicalDeviceProperties.deviceName << std::endl;
		this->physicalDevice = physicalDevices[0];
		return;
	}
	else {
		throw std::runtime_error("Could not find a physical device compatible device (2).");
	}
}

void Engine::CreateLogicalDevice() {
	if (!this->queueFamilies.isComplete()) {
		throw std::runtime_error("Device is not compatible. Queue families are not complete.");
	}

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	std::set<uint32_t> qfs = { this->queueFamilies.graphicsQF.value(), this->queueFamilies.presentationQF.value() };

	float priority = 1.0f;
	for (uint32_t qf : qfs) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.pQueuePriorities = &priority;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.queueFamilyIndex = qf;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

	deviceCreateInfo.ppEnabledExtensionNames = this->deviceExtensions.data();
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(this->deviceExtensions.size());

	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	VKCheck("Could not create device.", vkCreateDevice(this->physicalDevice, &deviceCreateInfo, nullptr, &this->logicalDevice));

	vkGetDeviceQueue(this->logicalDevice, this->queueFamilies.graphicsQF.value(), 0, &this->graphicsQueue);
	vkGetDeviceQueue(this->logicalDevice, this->queueFamilies.presentationQF.value(), 0, &this->presentationQueue);
}

void Engine::CreateSwapchain(VkSwapchainKHR& swapchain, size_t& WIN_W, size_t& WIN_H) {
	VkSurfaceFormatKHR surfaceFormat = GetSurfaceFormat(this->swapchainDetails.formats);
	VkPresentModeKHR presentMode = GetSurfacePresentMode(this->swapchainDetails.presentModes);
	VkExtent2D swapExtent = GetSwapExtent(this->swapchainDetails.capabilities, WIN_W, WIN_H);

	uint32_t imageCount = this->swapchainDetails.capabilities.minImageCount + 1;

	if (this->swapchainDetails.capabilities.maxImageCount > 0 && imageCount > this->swapchainDetails.capabilities.maxImageCount) {
		imageCount = this->swapchainDetails.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
	swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChainCreateInfo.surface = this->surface;
	swapChainCreateInfo.minImageCount = imageCount;
	swapChainCreateInfo.imageFormat = surfaceFormat.format;
	swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapChainCreateInfo.imageExtent = swapExtent;
	swapChainCreateInfo.imageArrayLayers = 1;
	swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapChainCreateInfo.presentMode = presentMode;
	swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChainCreateInfo.preTransform = this->swapchainDetails.capabilities.currentTransform;
	swapChainCreateInfo.clipped = VK_TRUE;
	swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	uint32_t indices[2] = { this->queueFamilies.graphicsQF.value(), this->queueFamilies.presentationQF.value() };

	if (this->queueFamilies.graphicsQF.value() != this->queueFamilies.presentationQF.value()) {
		swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapChainCreateInfo.queueFamilyIndexCount = 2;
		swapChainCreateInfo.pQueueFamilyIndices = indices;
	}
	else {
		swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapChainCreateInfo.queueFamilyIndexCount = 0;
		swapChainCreateInfo.pQueueFamilyIndices = nullptr;
	}

	VKCheck("Could not create swapchain.", vkCreateSwapchainKHR(this->logicalDevice, &swapChainCreateInfo, nullptr, &swapchain));

	this->swapImageFormat = swapChainCreateInfo.imageFormat;
	this->swapImageSize = swapChainCreateInfo.imageExtent;
}

void Engine::CreateColorImageView() {

	VkImageViewCreateInfo imageViewCreateInfo = { };
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.image = this->colorImage;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = this->swapImageFormat;
	imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	imageViewCreateInfo.subresourceRange.levelCount = 1;

	vkCreateImageView(this->logicalDevice, &imageViewCreateInfo, nullptr, &this->colorImageView);
}

void Engine::CreateImageViews() {
	this->swapImageViews.resize(this->swapImages.size());

	uint32_t idx = 0;
	for (VkImage image : this->swapImages) {
		VkImageViewCreateInfo imageViewCreateInfo = { };
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.image = image;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = this->swapImageFormat;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		imageViewCreateInfo.subresourceRange.levelCount = 1;

		vkCreateImageView(this->logicalDevice, &imageViewCreateInfo, nullptr, &this->swapImageViews[idx]);
		idx++;
	}
}

void Engine::CreateRenderPass() {
	const std::vector<VkFormat> formats = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkFormat depthFormat = GetSupportedFormat(formats, tiling, features);

	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = this->swapImageFormat;
	colorAttachment.samples = this->msaaSamples;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depthFormat;
	depthAttachment.samples = this->msaaSamples;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription colorAttachmentResolve = {};
	colorAttachmentResolve.format = this->swapImageFormat;
	colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;



	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachmentResolveRef = {};
	colorAttachmentResolveRef.attachment = 2;
	colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorAttachmentRef;
	subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;
	subpassDescription.pResolveAttachments = &colorAttachmentResolveRef;

	VkSubpassDependency subpassDependency = {};
	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	std::vector<VkAttachmentDescription> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = attachments.size();
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &subpassDependency;

	VKCheck("Could not create render pass.", vkCreateRenderPass(this->logicalDevice, &renderPassInfo, nullptr, &this->renderPass));
}

void Engine::CreateDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.pImmutableSamplers = nullptr;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 1;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding, samplerLayoutBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();

	VKCheck("Could not create descriptor set layout.", vkCreateDescriptorSetLayout(this->logicalDevice, &layoutInfo, nullptr, &this->descriptorSetLayout));
}

void Engine::CreateGraphicsPipeline() {
	std::vector<char> shaderVert = ReadFile("shaders/vert.spv");
	std::vector<char> shaderFrag = ReadFile("shaders/frag.spv");

	VkShaderModule shaderVertModule = CreateShaderModule(shaderVert);
	VkShaderModule shaderFragModule = CreateShaderModule(shaderFrag);

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
	depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilInfo.depthTestEnable = VK_TRUE;
	depthStencilInfo.depthWriteEnable = VK_TRUE;
	depthStencilInfo.stencilTestEnable = VK_FALSE;
	depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencilInfo.minDepthBounds = 0.0f;
	depthStencilInfo.maxDepthBounds = 1.0f;
	depthStencilInfo.front = {};
	depthStencilInfo.back = {};
 
	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
	shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageCreateInfo.module = shaderVertModule;
	shaderStageCreateInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragStageCreateInfo = {};
	fragStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStageCreateInfo.module = shaderFragModule;
	fragStageCreateInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = { shaderStageCreateInfo, fragStageCreateInfo };

	std::vector<VkVertexInputAttributeDescription> attributeDescriptions = Vertex::GetAttributeDescriptions();
	VkVertexInputBindingDescription bindingDescriptions = Vertex::GetBindingDescription();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescriptions;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo assemblyInputInfo = {};
	assemblyInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInputInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInputInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewPort = {};
	viewPort.x = 0.0f;
	viewPort.y = 0.0f;
	viewPort.width = (float)this->swapImageSize.width;
	viewPort.height = (float)this->swapImageSize.height;
	viewPort.minDepth = 0.0f;
	viewPort.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.extent = this->swapImageSize;

	VkPipelineViewportStateCreateInfo viewPortStateInfo = {};
	viewPortStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewPortStateInfo.pScissors = &scissor;
	viewPortStateInfo.pViewports = &viewPort;
	viewPortStateInfo.scissorCount = 1;
	viewPortStateInfo.viewportCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizationStateInfo = {};
	rasterizationStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizationStateInfo.depthClampEnable = VK_FALSE;
	rasterizationStateInfo.depthBiasEnable = VK_FALSE;
	rasterizationStateInfo.depthBiasConstantFactor = 0.0f;
	rasterizationStateInfo.depthBiasSlopeFactor = 0.0f;
	rasterizationStateInfo.depthBiasClamp = 0.0f;
	rasterizationStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateInfo.lineWidth = 1.0f;
	rasterizationStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo multiSamplingInfo = {};
	multiSamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multiSamplingInfo.sampleShadingEnable = VK_FALSE;
	multiSamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multiSamplingInfo.minSampleShading = 1.0f;
	multiSamplingInfo.pSampleMask = nullptr;
	multiSamplingInfo.alphaToOneEnable = VK_FALSE;
	multiSamplingInfo.alphaToCoverageEnable = VK_FALSE;
	multiSamplingInfo.rasterizationSamples = this->msaaSamples;

	VkPipelineColorBlendAttachmentState colorBlendingAttachment = {};
	colorBlendingAttachment.colorWriteMask = VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendingAttachment.blendEnable = VK_FALSE;
	colorBlendingAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendingAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendingAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendingAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendingAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendingAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo = {};
	colorBlendingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendingCreateInfo.logicOpEnable = VK_FALSE;
	colorBlendingCreateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendingCreateInfo.attachmentCount = 1;
	colorBlendingCreateInfo.pAttachments = &colorBlendingAttachment;
	colorBlendingCreateInfo.blendConstants[0] = 0.0f;
	colorBlendingCreateInfo.blendConstants[1] = 0.0f;
	colorBlendingCreateInfo.blendConstants[2] = 0.0f;
	colorBlendingCreateInfo.blendConstants[3] = 0.0f;

	VkPipelineLayout pipelineLayout = {};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;
	pipelineLayoutInfo.pSetLayouts = &this->descriptorSetLayout;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pushConstantRangeCount = 0;

	VKCheck("Could not create pipeline layout.", vkCreatePipelineLayout(this->logicalDevice, &pipelineLayoutInfo, nullptr, &this->pipelineLayout));

	VkGraphicsPipelineCreateInfo graphicsPipelineInfo = {};
	graphicsPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphicsPipelineInfo.stageCount = 2;
	graphicsPipelineInfo.pStages = shaderStages;
	graphicsPipelineInfo.pColorBlendState = &colorBlendingCreateInfo;
	graphicsPipelineInfo.pMultisampleState = &multiSamplingInfo;
	graphicsPipelineInfo.pRasterizationState = &rasterizationStateInfo;
	graphicsPipelineInfo.pInputAssemblyState = &assemblyInputInfo;
	graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
	graphicsPipelineInfo.pViewportState = &viewPortStateInfo;
	graphicsPipelineInfo.pDynamicState = nullptr;
	graphicsPipelineInfo.pDepthStencilState = &depthStencilInfo;
	graphicsPipelineInfo.layout = this->pipelineLayout;
	graphicsPipelineInfo.renderPass = this->renderPass;
	graphicsPipelineInfo.subpass = 0;
	graphicsPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	graphicsPipelineInfo.basePipelineIndex = -1;

	VKCheck("Could not create graphics pipeline.", vkCreateGraphicsPipelines(this->logicalDevice, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, nullptr, &this->pipeline));

	vkDestroyShaderModule(this->logicalDevice, shaderVertModule, nullptr);
	vkDestroyShaderModule(this->logicalDevice, shaderFragModule, nullptr);
}

void Engine::CreateFramebuffers() {
	this->framebuffers.resize(this->swapImageViews.size());

	uint32_t idx = 0;
	for (VkImageView imageView : this->swapImageViews) {
		std::vector<VkImageView> attachments = { this->colorImageView, this->depthImageView,  imageView};

		VkFramebufferCreateInfo frameBufferInfo = {};
		frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferInfo.renderPass = this->renderPass;
		frameBufferInfo.attachmentCount = attachments.size();
		frameBufferInfo.pAttachments = attachments.data();
		frameBufferInfo.width = this->swapImageSize.width;
		frameBufferInfo.height = this->swapImageSize.height;
		frameBufferInfo.layers = 1;

		VKCheck("Could not create framebuffer.", vkCreateFramebuffer(this->logicalDevice, &frameBufferInfo, nullptr, &this->framebuffers[idx]));
		idx++;
	}
}

void Engine::CreateCommandPool(VkCommandPool& commandPool, uint32_t& familyIndex) {
	VkCommandPoolCreateInfo commandPoolCreateInfo = {};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = familyIndex;

	VKCheck("Could not create command pool.", vkCreateCommandPool(this->logicalDevice, &commandPoolCreateInfo, nullptr, &commandPool));
}

bool Engine::hasStencil(VkFormat format) {
	if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
		return true;
	}
}

void Engine::CreateDepthResources() {
	const std::vector<VkFormat> formats = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkFormat depthFormat = GetSupportedFormat(formats, tiling, features);

	CreateImage(this->depthImage, this->depthImageMemory, this->swapImageSize.width, this->swapImageSize.height, 1, this->msaaSamples, depthFormat, tiling, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	CreateDepthImageView(depthFormat);
	//TransitionImageLayout(this->depthImage, mipLevels, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void Engine::CreateDepthImageView(VkFormat& depthFormat) {

	VkImageViewCreateInfo imageViewCreateInfo = { };
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.image = this->depthImage;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = depthFormat;
	imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	imageViewCreateInfo.subresourceRange.levelCount = 1;

	vkCreateImageView(this->logicalDevice, &imageViewCreateInfo, nullptr, &this->depthImageView);

}

void Engine::CreateColorResources() {
	VkFormat colorFormat = this->swapImageFormat;

	CreateImage(this->colorImage, this->colorImageMemory, this->swapImageSize.width, this->swapImageSize.height, 1, this->msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	CreateColorImageView();
}

void Engine::CreateImage(VkImage& image, VkDeviceMemory& imageMemory, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.arrayLayers = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = samples;
	imageInfo.flags = 0;

	VKCheck("Could not create image.", vkCreateImage(this->logicalDevice, &imageInfo, nullptr, &image));

	VkMemoryRequirements memoryRequirements = {};
	vkGetImageMemoryRequirements(this->logicalDevice, image, &memoryRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memoryRequirements.size;
	allocInfo.memoryTypeIndex = GetMemoryType(memoryRequirements.memoryTypeBits, properties);

	VKCheck("Could not allocate memory to image.", vkAllocateMemory(this->logicalDevice, &allocInfo, nullptr, &imageMemory));

	vkBindImageMemory(this->logicalDevice, image, imageMemory, 0);
}

void Engine::CreateModel(const char* name) {
	tinyobj::attrib_t attrib;

	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, name)) {
		throw std::runtime_error("Could not load model " + *name + *": " + warn + err);
	}

	std::unordered_map<Vertex, uint32_t> uniqueVertices = {};

	for (tinyobj::shape_t shape : shapes) {
		for (tinyobj::index_t index : shape.mesh.indices) {
			Vertex vertex = {};

			vertex.position = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2],
			};

			vertex.texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
			};

			vertex.color = { 1.0f, 1.0f, 1.0f };

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(this->vertices.size());
				this->vertices.push_back(vertex);
			}

			this->indices.push_back(uniqueVertices[vertex]);
		}
	}
}

void Engine::GenerateMipmaps(VkImage& image, int32_t im_w, int32_t im_h, uint32_t mipLevels, VkFormat imgFormat) {

	VkFormatProperties properties;

	vkGetPhysicalDeviceFormatProperties(this->physicalDevice, imgFormat, &properties);

	if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		throw std::runtime_error("Texture image format not supported for linear blitting.");
	}

	VkCommandBuffer commandBuffer;
	BeginSingleTimeCommands(commandBuffer, this->commandPool);

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mip_w = im_w;
	int32_t mip_h = im_h;

	for (uint32_t i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mip_w, mip_h, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;

		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mip_w > 1 ? mip_w / 2 : 1, mip_h > 1 ? mip_h / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		if (mip_w > 1) {
			mip_w /= 2;
		}

		if (mip_h > 1) {
			mip_h /= 2;
		}
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	EndSingleTimeCommands(commandBuffer, this->commandPool);
}

void Engine::CreateTextureImage(const char* name) {
	VkImage image;
	VkDeviceMemory imageMemory;

	int im_w, im_h, channels;

	stbi_uc* pixels = stbi_load(name, &im_w, &im_h, &channels, STBI_rgb_alpha);

	this->mipLevels = static_cast<uint32_t>(std::floor(std::log2(max(im_w, im_h)))) + 1;

	if (!pixels) {
		throw std::runtime_error("Could not load image " + *name);
	}

	VkDeviceSize imageSize = im_w * im_h * 4;

	VkDeviceMemory stagingMemory = 0;
	VkBuffer stagingBuffer = CreateBuffer(stagingMemory, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	void* data;
	vkMapMemory(this->logicalDevice, stagingMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vkUnmapMemory(this->logicalDevice, stagingMemory);

	stbi_image_free(pixels);

	CreateImage(image, imageMemory, im_w, im_h, mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	TransitionImageLayout(image, mipLevels, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	CopyBufferToImage(stagingBuffer, image, im_w, im_h);
	GenerateMipmaps(image, im_w, im_h, mipLevels, VK_FORMAT_R8G8B8A8_SRGB);
	//TransitionImageLayout(image, mipLevels, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	
	vkDestroyBuffer(this->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, stagingMemory, nullptr);

	this->textureImages.push_back(image);
	this->textureImagesMemory.push_back(imageMemory);
}

void Engine::CreateTextureImageViews(uint32_t mipLevels) {
	this->textureImageViews.resize(this->textureImages.size());

	uint32_t idx = 0;
	for (VkImage image : this->textureImages) {
		VkImageViewCreateInfo imageViewCreateInfo = { };
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.image = image;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		imageViewCreateInfo.subresourceRange.levelCount = mipLevels;

		vkCreateImageView(this->logicalDevice, &imageViewCreateInfo, nullptr, &this->textureImageViews[idx]);
		idx++;
	}
}

void Engine::CreateTextureSampler() {
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = static_cast<float>(this->mipLevels);

	VKCheck("Could not create texture sampler.", vkCreateSampler(this->logicalDevice, &samplerInfo, nullptr, &this->sampler));
}

void Engine::TransitionImageLayout(VkImage& image, uint32_t mipLevels, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
	VkCommandBuffer commandBuffer;
	BeginSingleTimeCommands(commandBuffer, this->commandPool);

	VkImageMemoryBarrier memoryBarrier = {};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	memoryBarrier.image = image;
	memoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrier.subresourceRange.baseArrayLayer = 0;
	memoryBarrier.subresourceRange.baseMipLevel = 0;
	memoryBarrier.subresourceRange.layerCount = 1;
	memoryBarrier.subresourceRange.levelCount = mipLevels;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.oldLayout = oldLayout;
	memoryBarrier.newLayout = newLayout;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		memoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	}

	else {
		throw std::invalid_argument("Invalid image transition layout.");
	}

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

	EndSingleTimeCommands(commandBuffer, this->commandPool);
}

void Engine::CopyBufferToImage(VkBuffer& srcBuffer, VkImage& srcImage, uint32_t width, uint32_t height) {
	VkCommandBuffer commandBuffer;
	BeginSingleTimeCommands(commandBuffer, this->commandPool);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = { 0, 0 };
	region.imageExtent = {
		width,
		height,
		1
	};

	vkCmdCopyBufferToImage(commandBuffer, srcBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	EndSingleTimeCommands(commandBuffer, this->commandPool);
}

void Engine::BeginSingleTimeCommands(VkCommandBuffer& commandBuffer, VkCommandPool& commandPool) {
	VkCommandBufferAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	allocateInfo.commandPool = commandPool;

	vkAllocateCommandBuffers(this->logicalDevice, &allocateInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = { };
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);
}

void Engine::EndSingleTimeCommands(VkCommandBuffer& commandBuffer, VkCommandPool& commandPool) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(this->graphicsQueue);
	vkFreeCommandBuffers(this->logicalDevice, commandPool, 1, &commandBuffer);
}

void Engine::CopyBuffer(VkBuffer& srcBuffer, VkBuffer& dstBuffer, VkDeviceSize& size, VkCommandPool& commandPool) {
	VkCommandBuffer commandBuffer;
	BeginSingleTimeCommands(commandBuffer, commandPool);

	VkBufferCopy copyRegion = {};
	copyRegion.size = size;

	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	EndSingleTimeCommands(commandBuffer, commandPool);

}

void Engine::CreateVertexBuffer() {
	VkDeviceSize stagingBufferSize = sizeof(this->vertices[0]) * this->vertices.size();
	VkDeviceMemory stagingBufferMemory = 0;
	VkBuffer stagingBuffer = 0;
	stagingBuffer = CreateBuffer(stagingBufferMemory, stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	void* data;
	vkMapMemory(this->logicalDevice, stagingBufferMemory, 0, stagingBufferSize, 0, &data);
	memcpy(data, this->vertices.data(), (size_t) stagingBufferSize);
	vkUnmapMemory(this->logicalDevice, stagingBufferMemory);

	VkDeviceSize vertexBufferSize = stagingBufferSize;
	VkDeviceMemory vertexBufferMemory = 0;
	this->vertexBuffer = CreateBuffer(vertexBufferMemory, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	this->vertexMemory = vertexBufferMemory;

	CopyBuffer(stagingBuffer, this->vertexBuffer, vertexBufferSize, this->copyPool);
	vkDestroyBuffer(this->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, stagingBufferMemory, nullptr);
}

void Engine::CreateIndicesBuffer() {
	VkDeviceSize stagingBufferSize = sizeof(this->indices[0]) * this->indices.size();
	VkDeviceMemory stagingBufferMemory = 0;
	VkBuffer stagingBuffer = 0;
	stagingBuffer = CreateBuffer(stagingBufferMemory, stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	void* data;
	vkMapMemory(this->logicalDevice, stagingBufferMemory, 0, stagingBufferSize, 0, &data);
	memcpy(data, this->indices.data(), (size_t)stagingBufferSize);
	vkUnmapMemory(this->logicalDevice, stagingBufferMemory);

	VkDeviceSize indicesBufferSize = stagingBufferSize;
	VkDeviceMemory indicesBufferMemory = 0;
	this->indicesBuffer = CreateBuffer(indicesBufferMemory, indicesBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	this->indicesMemory = indicesBufferMemory;

	CopyBuffer(stagingBuffer, this->indicesBuffer, indicesBufferSize, this->copyPool);
	vkDestroyBuffer(this->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, stagingBufferMemory, nullptr);
}

void Engine::CreateUniformBuffers() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	this->uniformBuffers.resize(this->swapImages.size());
	this->uniformBuffersMemory.resize(this->swapImages.size());



	//UniformBufferObject obj = {  };
	//obj.model = glm::mat4(0.0f);
	//obj.view = glm::mat4(2.0f);
	//obj.projection = glm::mat4(1.0f);

	for (int i = 0; i < this->swapImages.size(); i++) {
		this->uniformBuffers[i] = CreateBuffer(this->uniformBuffersMemory[i], bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
}

void Engine::CreateDescriptorPool() {
	VkDescriptorPoolSize swapPoolSize = {};
	swapPoolSize.descriptorCount = static_cast<uint32_t>(this->swapImages.size());
	swapPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

	VkDescriptorPoolSize texPoolSize = {};
	texPoolSize.descriptorCount = static_cast<uint32_t>(this->swapImages.size());
	texPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	std::vector<VkDescriptorPoolSize> poolSizes = { swapPoolSize, texPoolSize };

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.poolSizeCount = poolSizes.size();
	poolInfo.maxSets = static_cast<uint32_t>(this->swapImages.size());

	VKCheck("Could not create descriptor pool.", vkCreateDescriptorPool(this->logicalDevice, &poolInfo, nullptr, &this->descriptorPool));
}

void Engine::CreateDescriptorSets() {
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts(this->swapImages.size(), this->descriptorSetLayout);

	VkDescriptorSetAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = this->descriptorPool;
	allocateInfo.descriptorSetCount = static_cast<uint32_t>(this->swapImages.size());
	allocateInfo.pSetLayouts = descriptorSetLayouts.data();

	this->descriptorSets.resize(this->swapImages.size());
	vkAllocateDescriptorSets(this->logicalDevice, &allocateInfo, this->descriptorSets.data());

	for (int i = 0; i < this->swapImages.size(); i++) {
		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = this->uniformBuffers[i];
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = this->textureImageViews[0];
		imageInfo.sampler = this->sampler;

		VkWriteDescriptorSet uboWrite = {};
		uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		uboWrite.descriptorCount = 1;
		uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboWrite.pBufferInfo = &bufferInfo;
		uboWrite.pImageInfo = nullptr;
		uboWrite.pTexelBufferView = nullptr;
		uboWrite.dstSet = this->descriptorSets[i];
		uboWrite.dstArrayElement = 0;
		uboWrite.dstBinding = 0;

		VkWriteDescriptorSet imgWrite = {};
		imgWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		imgWrite.descriptorCount = 1;
		imgWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		imgWrite.dstSet = this->descriptorSets[i];
		imgWrite.dstArrayElement = 0;
		imgWrite.dstBinding = 1;
		imgWrite.pImageInfo = &imageInfo;

		std::vector<VkWriteDescriptorSet> descriptorWrites = { uboWrite, imgWrite };

		vkUpdateDescriptorSets(this->logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
	}

}

void Engine::CreateCommandBuffers() {
	this->commandBuffers.resize(this->framebuffers.size());

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.commandPool = this->commandPool;
	commandBufferAllocateInfo.commandBufferCount = (uint32_t)commandBuffers.size();
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VKCheck("Could not allocate command buffers.", vkAllocateCommandBuffers(this->logicalDevice, &commandBufferAllocateInfo, this->commandBuffers.data()));


	VkClearValue clearColor = { 0.25f, 0.50f, 0.75f, 1.0f };
	VkClearValue depthStencil = { 1.0f, 0.0f };

	std::vector<VkClearValue> clearValues = { clearColor, depthStencil };

	for (size_t i = 0; i < this->commandBuffers.size(); i++) {
		VkCommandBufferBeginInfo commandBufferBeginInfo = {};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBufferBeginInfo.pInheritanceInfo = nullptr;
		commandBufferBeginInfo.flags = 0;

		VKCheck("Could not begin command buffer.", vkBeginCommandBuffer(this->commandBuffers[i], &commandBufferBeginInfo));

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.framebuffer = this->framebuffers[i];
		renderPassBeginInfo.renderPass = this->renderPass;
		renderPassBeginInfo.renderArea.extent = this->swapImageSize;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.clearValueCount = clearValues.size();
		renderPassBeginInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(this->commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(this->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline);

		VkBuffer vertexBuffers[] = { this->vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(this->commandBuffers[i], 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(this->commandBuffers[i], this->indicesBuffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(this->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout, 0, 1, &this->descriptorSets[i], 0, nullptr);

		vkCmdDrawIndexed(this->commandBuffers[i], static_cast<uint32_t>(this->indices.size()), 1, 0, 0, 0);
		vkCmdEndRenderPass(this->commandBuffers[i]);
		VKCheck("Failed to record command buffer.", vkEndCommandBuffer(this->commandBuffers[i]));
	}
}

void Engine::CreateSyncObjects() {
	this->imagesAvailableSemaphores.resize(this->MAX_CONCURRENT_FRAMES);
	this->imagesRenderedSemaphores.resize(this->MAX_CONCURRENT_FRAMES);
	this->inFlightFences.resize(this->MAX_CONCURRENT_FRAMES);
	this->imagesInFlight.resize(this->swapImages.size(), VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < this->MAX_CONCURRENT_FRAMES; i++) {
		VKCheck("Could not create signal sempahores.", vkCreateSemaphore(this->logicalDevice, &semaphoreCreateInfo, nullptr, &this->imagesAvailableSemaphores[i]));
		VKCheck("Could not create wait sempahores.", vkCreateSemaphore(this->logicalDevice, &semaphoreCreateInfo, nullptr, &this->imagesRenderedSemaphores[i]));
		VKCheck("Could not create working fences.", vkCreateFence(this->logicalDevice, &fenceCreateInfo, nullptr, &this->inFlightFences[i]));
	}
}