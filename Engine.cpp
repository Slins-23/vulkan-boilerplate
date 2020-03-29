#include "Engine.h"

Engine::Engine() {

}

Engine::~Engine() {

}

bool Engine::VKCheck(const char& message, VkResult result) {
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		return true;
	}
	else if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error(message + " Error code: " + result);
	}
}

void Engine::Load() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	this->instance = CreateInstance(this->debugLayers);
	this->window = glfwCreateWindow(this->WIN_W, this->WIN_H, this->TITLE, nullptr, nullptr);
	this->surface = CreateWindowSurface(this->instance, this->window);
	this->physicalDevice = CreatePhysicalDevice(this->instance, this->surface, this->deviceExtensions);
	this->queueFamilies = GetQueueFamilies(this->physicalDevice, this->surface);
	this->logicalDevice = CreateLogicalDevice(this->physicalDevice, this->queueFamilies, this->deviceExtensions);
	this->swapchainDetails = GetSwapchainDetails(this->physicalDevice, this->surface);
	this->swapchain = CreateSwapchain(this->WIN_W, this->WIN_H, this->logicalDevice, this->surface, this->swapchainDetails, this->queueFamilies);
	this->swapImages = GetSwapImages(this->logicalDevice, this->swapchain);
	this->swapImageViews = CreateImageViews(this->logicalDevice, this->swapImages, this->swapImageFormat);
	this->renderPass = CreateRenderPass(this->logicalDevice, this->swapImageFormat);
	this->pipeline = CreateGraphicsPipeline(this->logicalDevice, this->renderPass, this->swapImageSize);
	this->framebuffers = CreateFramebuffer(this->logicalDevice, this->swapImageViews, this->renderPass, this->swapImageSize);
	this->commandPool = CreateCommandPool(this->logicalDevice, this->queueFamilies.graphicsQF.value());
	this->commandBuffers = CreateCommandBuffers(this->logicalDevice, this->commandPool, this->framebuffers);
	CreateSyncObjects(this->imagesAvailableSemaphores, this->imagesRenderedSemaphores, this->inFlightFences, this->imagesInFlight);
}

static void ResizeCallback(GLFWwindow* window, int width, int height) {
	Engine* application = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	application->resizeTriggered = true;
	application->WIN_W = (size_t)width;
	application->WIN_H = (size_t)height;
}

void Engine::Start() {

	while (!glfwWindowShouldClose(this->window)) {
		glfwPollEvents();
		glfwSetWindowUserPointer(this->window, this);
		glfwSetFramebufferSizeCallback(this->window, ResizeCallback);
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

	VKCheck(*"Could not submit queue.", vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, this->inFlightFences[this->currentFrame]));

	VkSwapchainKHR swapchains[] = { this->swapchain };

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pSwapchains = swapchains;
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

void Engine::RecreateSwapchain() {

	int width, height = 0;

	while (this->WIN_W == 0 || this->WIN_H == 0) {
		glfwGetFramebufferSize(this->window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(this->logicalDevice);
	CloseSwapchain();

	this->swapchainDetails = GetSwapchainDetails(this->physicalDevice, this->surface);
	this->swapchain = CreateSwapchain(this->WIN_W, this->WIN_H, this->logicalDevice, this->surface, this->swapchainDetails, this->queueFamilies);
	this->swapImages = GetSwapImages(this->logicalDevice, this->swapchain);
	this->swapImageViews = CreateImageViews(this->logicalDevice, this->swapImages, this->swapImageFormat);
	this->renderPass = CreateRenderPass(this->logicalDevice, this->swapImageFormat);
	this->pipeline = CreateGraphicsPipeline(this->logicalDevice, this->renderPass, this->swapImageSize);
	this->framebuffers = CreateFramebuffer(this->logicalDevice, this->swapImageViews, this->renderPass, this->swapImageSize);
	this->commandBuffers = CreateCommandBuffers(this->logicalDevice, this->commandPool, this->framebuffers);
}

void Engine::CloseSwapchain() {
	DestroyFramebuffers();
	vkFreeCommandBuffers(this->logicalDevice, this->commandPool, static_cast<uint32_t>(this->commandBuffers.size()), this->commandBuffers.data());
	vkDestroyPipeline(this->logicalDevice, this->pipeline, nullptr);
	vkDestroyPipelineLayout(this->logicalDevice, this->pipelineLayout, nullptr);
	vkDestroyRenderPass(this->logicalDevice, this->renderPass, nullptr);
	DestroyImageViews();
	vkDestroySwapchainKHR(this->logicalDevice, this->swapchain, nullptr);
}

void Engine::Close() {
	CloseSwapchain();
	DestroySyncObjects();
	vkDestroyCommandPool(this->logicalDevice, this->commandPool, nullptr);
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

void Engine::DestroyImageViews() {
	for (VkImageView imageView : this->swapImageViews) {
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

QueueFamilies Engine::GetQueueFamilies(VkPhysicalDevice& physicalDevice, VkSurfaceKHR& surface) {
	QueueFamilies qf = {};

	uint32_t qfCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);

	std::vector<VkQueueFamilyProperties> qfProperties(qfCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfProperties.data());

	int idx = 0;

	for (VkQueueFamilyProperties qfProperty : qfProperties) {
		if (qfProperty.queueFlags && VK_QUEUE_GRAPHICS_BIT) {
			qf.graphicsQF = idx;
			qf.count++;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, idx, surface, &presentSupport);

		if (presentSupport) {
			qf.presentationQF = idx;

			if (idx != qf.graphicsQF.value()) {
				qf.count++;
			}
		}

		if (qf.isComplete()) {
			break;
		}

		idx++;
	}

	return qf;
}

SwapchainDetails Engine::GetSwapchainDetails(VkPhysicalDevice& physicalDevice, VkSurfaceKHR& surface) {
	SwapchainDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

	uint32_t formatsCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatsCount, nullptr);

	if (formatsCount != 0) {
		details.formats.resize(formatsCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatsCount, details.formats.data());
	}
	else {
		throw std::runtime_error("Could not get swapchain format details.");
	}

	uint32_t presentModesCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, nullptr);

	if (presentModesCount != 0) {
		details.presentModes.resize(presentModesCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModesCount, details.presentModes.data());
	}
	else {
		throw std::runtime_error("Could not get swapchain present modes details.");
	}

	return details;
}

VkSurfaceFormatKHR Engine::GetSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& surfaceFormats) {
	for (const VkSurfaceFormatKHR surfaceFormat : surfaceFormats) {
		if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return surfaceFormat;
		}
	}

	return surfaceFormats[0];
}

VkPresentModeKHR Engine::GetSurfacePresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
	for (VkPresentModeKHR presentMode : presentModes) {
		if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return presentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Engine::GetSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, size_t& WIN_W, size_t& WIN_H) {

	if (capabilities.currentExtent.width != UINT32_MAX) {
		return capabilities.currentExtent;
	}
	else {
		VkExtent2D extent = { WIN_W, WIN_H };

		extent.width = max(capabilities.minImageExtent.width, min(capabilities.maxImageExtent.width, extent.width));
		extent.height = max(capabilities.minImageExtent.height, min(capabilities.maxImageExtent.height, extent.height));

		return extent;
	}
}

std::vector<VkImage> Engine::GetSwapImages(VkDevice& logicalDevice, VkSwapchainKHR& swapchain) {
	uint32_t swapImageCount = 0;
	vkGetSwapchainImagesKHR(logicalDevice, swapchain, &swapImageCount, nullptr);

	std::vector<VkImage> swapImages(swapImageCount);
	vkGetSwapchainImagesKHR(logicalDevice, swapchain, &swapImageCount, swapImages.data());

	return swapImages;
}

VkInstance Engine::CreateInstance(const std::vector<const char*>& debugLayers) {
	VkInstance instance = 0;

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

	ValidateDebugLayers(debugLayers);

	createInfo.enabledLayerCount = debugLayers.size();
	createInfo.ppEnabledLayerNames = debugLayers.data();
#endif

	VKCheck(*"Could not create instance.", vkCreateInstance(&createInfo, nullptr, &instance));

	return instance;
}

VkSurfaceKHR Engine::CreateWindowSurface(VkInstance& instance, GLFWwindow* window) {
	VkSurfaceKHR SFC;

	VkWin32SurfaceCreateInfoKHR winCreateSurfaceInfo = {};
	winCreateSurfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	winCreateSurfaceInfo.hinstance = GetModuleHandle(0);
	winCreateSurfaceInfo.hwnd = glfwGetWin32Window(window);

	VKCheck(*"Could not create Win32 surface.", vkCreateWin32SurfaceKHR(instance, &winCreateSurfaceInfo, nullptr, &SFC));

	return SFC;
}

VkPhysicalDevice Engine::CreatePhysicalDevice(VkInstance& instance, VkSurfaceKHR& surface, const std::vector<const char*> deviceExtensions) {
	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);

	if (physicalDeviceCount == 0) {
		throw std::runtime_error("Could not find any compatible device (1).");
	}

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
	vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

	for (VkPhysicalDevice device : physicalDevices) {
		VkPhysicalDeviceProperties physicalDeviceProperties;
		VkPhysicalDeviceFeatures physicalDeviceFeatures;
		vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);
		vkGetPhysicalDeviceFeatures(device, &physicalDeviceFeatures);

		if (!ValidateDeviceExtensions(device, deviceExtensions)) {
			continue;
		}

		bool supportsSwapchain = false;
		SwapchainDetails details = GetSwapchainDetails(device, surface);

		if (!details.formats.empty() && !details.presentModes.empty()) {
			supportsSwapchain = true;
		}

		if (supportsSwapchain && physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			std::cout << "Using GPU: " << physicalDeviceProperties.deviceName << std::endl;
			return device;
		}
	}

	if (physicalDevices.size() > 0) {
		VkPhysicalDeviceProperties physicalDeviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevices[0], &physicalDeviceProperties);
		std::cout << "Using Fallback Device: " << physicalDeviceProperties.deviceName << std::endl;
		return physicalDevices[0];
	}
	else {
		throw std::runtime_error("Could not find a physical device compatible device (2).");
	}
}

VkDevice Engine::CreateLogicalDevice(VkPhysicalDevice& physicalDevice, QueueFamilies& qf, const std::vector<const char*>& deviceExtensions) {
	if (!qf.isComplete()) {
		throw std::runtime_error("Device is not compatible. Queue families are not complete.");
	}

	VkDevice device = 0;
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	std::set<uint32_t> qfs = { qf.graphicsQF.value(), qf.presentationQF.value() };

	float priority = 1.0f;
	for (uint32_t qf : qfs) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.pQueuePriorities = &priority;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.queueFamilyIndex = qf;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());

	VKCheck(*"Could not create device.", vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

	vkGetDeviceQueue(device, qf.graphicsQF.value(), 0, &this->graphicsQueue);
	vkGetDeviceQueue(device, qf.presentationQF.value(), 0, &this->presentationQueue);

	return device;
}

VkSwapchainKHR Engine::CreateSwapchain(size_t& WIN_W, size_t& WIN_H, VkDevice& device, VkSurfaceKHR& surface, SwapchainDetails& swapChainDetails, QueueFamilies& qf) {
	VkSurfaceFormatKHR surfaceFormat = GetSurfaceFormat(swapChainDetails.formats);
	VkPresentModeKHR presentMode = GetSurfacePresentMode(swapChainDetails.presentModes);
	VkExtent2D swapExtent = GetSwapExtent(swapChainDetails.capabilities, WIN_W, WIN_H);

	uint32_t imageCount = swapChainDetails.capabilities.minImageCount + 1;

	if (swapChainDetails.capabilities.maxImageCount > 0 && imageCount > swapChainDetails.capabilities.maxImageCount) {
		imageCount = swapChainDetails.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
	swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChainCreateInfo.surface = surface;
	swapChainCreateInfo.minImageCount = imageCount;
	swapChainCreateInfo.imageFormat = surfaceFormat.format;
	swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapChainCreateInfo.imageExtent = swapExtent;
	swapChainCreateInfo.imageArrayLayers = 1;
	swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapChainCreateInfo.presentMode = presentMode;
	swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChainCreateInfo.preTransform = swapChainDetails.capabilities.currentTransform;
	swapChainCreateInfo.clipped = VK_TRUE;
	swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	uint32_t indices[2] = { qf.graphicsQF.value(), qf.presentationQF.value() };

	if (qf.graphicsQF.value() != qf.presentationQF.value()) {
		swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapChainCreateInfo.queueFamilyIndexCount = 2;
		swapChainCreateInfo.pQueueFamilyIndices = indices;
	}
	else {
		swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapChainCreateInfo.queueFamilyIndexCount = 0;
		swapChainCreateInfo.pQueueFamilyIndices = nullptr;
	}

	VkSwapchainKHR swapChain = 0;

	VKCheck(*"Could not create swapchain.", vkCreateSwapchainKHR(device, &swapChainCreateInfo, nullptr, &swapChain));

	this->swapImageFormat = swapChainCreateInfo.imageFormat;
	this->swapImageSize = swapChainCreateInfo.imageExtent;

	return swapChain;
}

std::vector<VkImageView> Engine::CreateImageViews(VkDevice& device, std::vector<VkImage>& images, VkFormat& imageFormat) {

	std::vector<VkImageView> imageViews(images.size());

	uint32_t idx = 0;
	for (VkImage image : images) {
		VkImageViewCreateInfo imageViewCreateInfo = { };
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.image = image;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = imageFormat;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		imageViewCreateInfo.subresourceRange.levelCount = 1;

		VkImageView imageView;
		vkCreateImageView(device, &imageViewCreateInfo, nullptr, &imageViews[idx]);

		idx++;
	}

	return imageViews;
}

VkShaderModule Engine::CreateShaderModule(VkDevice& device, const std::vector<char>& byteCode) {
	VkShaderModule shaderModule;

	VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
	shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderModuleCreateInfo.codeSize = byteCode.size();
	shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(byteCode.data());

	VKCheck(*"Could not create shader module.", vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule));

	return shaderModule;
}

VkRenderPass Engine::CreateRenderPass(VkDevice& logicalDevice, VkFormat& format) {
	VkRenderPass renderPass = {};

	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = format;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorAttachmentRef;

	VkSubpassDependency subpassDependency = {};
	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependency.srcSubpass = VK_NULL_HANDLE;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &subpassDependency;

	VKCheck(*"Could not create render pass.", vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass));

	return renderPass;
}

VkPipeline Engine::CreateGraphicsPipeline(VkDevice& device, VkRenderPass& renderPass, VkExtent2D& extent) {
	std::vector<char> shaderVert = ReadFile("shaders/vert.spv");
	std::vector<char> shaderFrag = ReadFile("shaders/frag.spv");

	VkShaderModule shaderVertModule = CreateShaderModule(logicalDevice, shaderVert);
	VkShaderModule shaderFragModule = CreateShaderModule(logicalDevice, shaderFrag);

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

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.pVertexAttributeDescriptions = nullptr;
	vertexInputInfo.pVertexBindingDescriptions = nullptr;

	VkPipelineInputAssemblyStateCreateInfo assemblyInputInfo = {};
	assemblyInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInputInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInputInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewPort = {};
	viewPort.x = 0.0f;
	viewPort.y = 0.0f;
	viewPort.width = (float)extent.width;
	viewPort.height = (float)extent.height;
	viewPort.minDepth = 0.0f;
	viewPort.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.extent = extent;

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
	rasterizationStateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo multiSamplingInfo = {};
	multiSamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multiSamplingInfo.sampleShadingEnable = VK_FALSE;
	multiSamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multiSamplingInfo.minSampleShading = 1.0f;
	multiSamplingInfo.pSampleMask = nullptr;
	multiSamplingInfo.alphaToOneEnable = VK_FALSE;
	multiSamplingInfo.alphaToCoverageEnable = VK_FALSE;

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
	pipelineLayoutInfo.pSetLayouts = nullptr;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.setLayoutCount = 0;

	VKCheck(*"Could not create pipeline layout.", vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	this->pipelineLayout = pipelineLayout;

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
	graphicsPipelineInfo.pDepthStencilState = nullptr;
	graphicsPipelineInfo.layout = this->pipelineLayout;
	graphicsPipelineInfo.renderPass = this->renderPass;
	graphicsPipelineInfo.subpass = 0;
	graphicsPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	graphicsPipelineInfo.basePipelineIndex = -1;

	VkPipeline pipeline = 0;

	VKCheck(*"Could not create graphics pipeline.", vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, nullptr, &pipeline));

	vkDestroyShaderModule(device, shaderVertModule, nullptr);
	vkDestroyShaderModule(device, shaderFragModule, nullptr);

	return pipeline;
}

std::vector<VkFramebuffer> Engine::CreateFramebuffer(VkDevice& device, std::vector<VkImageView>& imageViews, VkRenderPass& renderPass, VkExtent2D& extent) {
	std::vector<VkFramebuffer> frameBuffer(imageViews.size());

	uint32_t idx = 0;
	for (VkImageView imageView : imageViews) {
		VkFramebufferCreateInfo frameBufferInfo = {};
		frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferInfo.renderPass = renderPass;
		frameBufferInfo.attachmentCount = 1;
		frameBufferInfo.pAttachments = &imageView;
		frameBufferInfo.width = extent.width;
		frameBufferInfo.height = extent.height;
		frameBufferInfo.layers = 1;

		VKCheck(*"Could not create framebuffer.", vkCreateFramebuffer(device, &frameBufferInfo, nullptr, &frameBuffer[idx]));

		idx++;
	}

	return frameBuffer;
}

VkCommandPool Engine::CreateCommandPool(VkDevice& device, uint32_t& familyIndex) {
	VkCommandPool commandPool = 0;

	VkCommandPoolCreateInfo commandPoolCreateInfo = {};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = familyIndex;

	VKCheck(*"Could not create command pool.", vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool));

	return commandPool;
}

std::vector<VkCommandBuffer> Engine::CreateCommandBuffers(VkDevice& logicalDevice, VkCommandPool& commandPool, std::vector<VkFramebuffer>& framebuffers) {
	std::vector<VkCommandBuffer> commandBuffers(framebuffers.size());

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.commandPool = commandPool;
	commandBufferAllocateInfo.commandBufferCount = (uint32_t)commandBuffers.size();
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VKCheck(*"Could not allocate command buffers.", vkAllocateCommandBuffers(logicalDevice, &commandBufferAllocateInfo, commandBuffers.data()));

	VkClearValue clearColor = { 0.25f, 0.50f, 0.75f, 1.0f };

	for (size_t i = 0; i < commandBuffers.size(); i++) {
		VkCommandBufferBeginInfo commandBufferBeginInfo = {};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBufferBeginInfo.pInheritanceInfo = nullptr;
		commandBufferBeginInfo.flags = 0;

		VKCheck(*"Could not begin command buffer.", vkBeginCommandBuffer(commandBuffers[i], &commandBufferBeginInfo));

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.framebuffer = framebuffers[i];
		renderPassBeginInfo.renderPass = this->renderPass;
		renderPassBeginInfo.renderArea.extent = this->swapImageSize;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline);
		vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
		vkCmdEndRenderPass(commandBuffers[i]);
		VKCheck(*"Failed to record command buffer.", vkEndCommandBuffer(commandBuffers[i]));
	}

	return commandBuffers;
}

void Engine::CreateSyncObjects(std::vector<VkSemaphore>& imageAvailableSemaphores, std::vector<VkSemaphore>& imageRenderedSemaphores, std::vector<VkFence>& inFlightFences, std::vector<VkFence>& imagesInFlight) {

	imageAvailableSemaphores.resize(this->MAX_CONCURRENT_FRAMES);
	imageRenderedSemaphores.resize(this->MAX_CONCURRENT_FRAMES);
	inFlightFences.resize(this->MAX_CONCURRENT_FRAMES);
	imagesInFlight.resize(this->swapImages.size(), VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < this->MAX_CONCURRENT_FRAMES; i++) {
		VKCheck(*"Could not create signal sempahores.", vkCreateSemaphore(this->logicalDevice, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphores[i]));
		VKCheck(*"Could not create wait sempahores.", vkCreateSemaphore(this->logicalDevice, &semaphoreCreateInfo, nullptr, &imageRenderedSemaphores[i]));
		VKCheck(*"Could not create working fences.", vkCreateFence(this->logicalDevice, &fenceCreateInfo, nullptr, &inFlightFences[i]));
	}
}