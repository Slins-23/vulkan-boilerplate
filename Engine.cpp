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
	this->descriptorSetLayout = CreateDescriptorSetLayout(this->logicalDevice);
	this->pipeline = CreateGraphicsPipeline(this->logicalDevice, this->descriptorSetLayout, this->renderPass, this->swapImageSize);
	this->framebuffers = CreateFramebuffer(this->logicalDevice, this->swapImageViews, this->renderPass, this->swapImageSize);
	this->commandPool = CreateCommandPool(this->logicalDevice, this->queueFamilies.graphicsQF.value());
	this->copyPool = CreateCommandPool(this->logicalDevice, this->queueFamilies.graphicsQF.value());
	this->vertexMemory = CreateVertexBuffer(this->logicalDevice, this->physicalDevice, this->vertexBuffer, this->vertexData, this->copyPool);
	this->indicesMemory = CreateIndicesBuffer(this->logicalDevice, this->physicalDevice, this->indicesBuffer, this->vertexIndices, this->copyPool);
	CreateUniformBuffers(this->uniformBuffers, this->uniformBuffersMemory);
	CreateDescriptorPool();
	CreateDescriptorSets();
	this->commandBuffers = CreateCommandBuffers(this->logicalDevice, this->commandPool, this->framebuffers, this->vertexData, this->vertexIndices, this->vertexBuffer, this->indicesBuffer);
	CreateSyncObjects(this->imagesAvailableSemaphores, this->imagesRenderedSemaphores, this->inFlightFences, this->imagesInFlight);
}

static void ResizeCallback(GLFWwindow* window, int width, int height) {
	Engine* application = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	application->resizeTriggered = true;
	application->WIN_W = (size_t)width;
	application->WIN_H = (size_t)height;
}

void Engine::Start() {

	glfwSetWindowUserPointer(this->window, this);
	glfwSetFramebufferSizeCallback(this->window, ResizeCallback);

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

void Engine::UpdateUniformBuffers(uint32_t currentImage) {
	static std::chrono::time_point startTime = std::chrono::high_resolution_clock::now();


	std::chrono::time_point currentTime = std::chrono::high_resolution_clock::now();

	float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	UniformBufferObject UBO = {};
	UBO.model = glm::rotate(glm::mat4(1.0f), elapsed * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	UBO.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	UBO.projection = glm::perspective(glm::radians(45.0f), this->swapImageSize.width / (float)this->swapImageSize.height, 0.1f, 10.0f);
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

	this->swapchainDetails = GetSwapchainDetails(this->physicalDevice, this->surface);
	this->swapchain = CreateSwapchain(this->WIN_W, this->WIN_H, this->logicalDevice, this->surface, this->swapchainDetails, this->queueFamilies);
	this->swapImages = GetSwapImages(this->logicalDevice, this->swapchain);
	this->swapImageViews = CreateImageViews(this->logicalDevice, this->swapImages, this->swapImageFormat);
	this->renderPass = CreateRenderPass(this->logicalDevice, this->swapImageFormat);
	this->pipeline = CreateGraphicsPipeline(this->logicalDevice, this->descriptorSetLayout, this->renderPass, this->swapImageSize);
	this->framebuffers = CreateFramebuffer(this->logicalDevice, this->swapImageViews, this->renderPass, this->swapImageSize);
	CreateUniformBuffers(this->uniformBuffers, this->uniformBuffersMemory);
	CreateDescriptorPool();
	CreateDescriptorSets();
	this->commandBuffers = CreateCommandBuffers(this->logicalDevice, this->commandPool, this->framebuffers, this->vertexData, this->vertexIndices, this->vertexBuffer, this->indicesBuffer);
}

void Engine::CloseSwapchain() {
	for (int i = 0; i < this->swapImages.size(); i++) {
		vkDestroyBuffer(this->logicalDevice, this->uniformBuffers[i], nullptr);
		vkFreeMemory(this->logicalDevice, this->uniformBuffersMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(this->logicalDevice, this->descriptorPool, nullptr);

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

uint32_t Engine::GetMemoryType(VkPhysicalDevice& physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
		if (typeFilter & (1 << i) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("Could not find an appropriate memory type.");
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

VkDescriptorSetLayout Engine::CreateDescriptorSetLayout(VkDevice& logicalDevice) {
	VkDescriptorSetLayoutBinding layoutBinding = {};
	layoutBinding.binding = 0;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	layoutBinding.descriptorCount = 1;
	layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	layoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding bindings[] = { layoutBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = bindings;

	VkDescriptorSetLayout layout;

	VKCheck(*"Could not create descriptor set layout.", vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &layout));

	return layout;
}

VkPipeline Engine::CreateGraphicsPipeline(VkDevice& device, VkDescriptorSetLayout& descriptorLayout, VkRenderPass& renderPass, VkExtent2D& extent) {
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
	rasterizationStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
	pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pushConstantRangeCount = 0;

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

VkBuffer Engine::CreateBuffer(VkDevice& logicalDevice, VkPhysicalDevice& physicalDevice, VkDeviceMemory& bufferMemory, VkDeviceSize& size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlagBits) {
	VkBuffer buffer;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usageFlags;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VKCheck(*"Could not create buffer.", vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer));

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(logicalDevice, buffer, &memoryRequirements);

	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	VKCheck(*"Could not allocate buffer memory.", vkAllocateMemory(logicalDevice, &allocateInfo, nullptr, &bufferMemory));

	vkBindBufferMemory(logicalDevice, buffer, bufferMemory, 0);

	return buffer;
}

void Engine::CopyBuffer(VkDevice& logicalDevice, VkBuffer& srcBuffer, VkBuffer& dstBuffer, VkDeviceSize& size, VkCommandPool& commandPool) {
	VkCommandBufferAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	allocateInfo.commandPool = commandPool;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(logicalDevice, &allocateInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = { };
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size = size;

	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(this->graphicsQueue);
	vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
}

VkDeviceMemory Engine::CreateVertexBuffer(VkDevice& logicalDevice, VkPhysicalDevice& physicalDevice, VkBuffer& vertexBuffer, std::vector<Vertex>& vertexData, VkCommandPool& copyPool) {
	VkDeviceSize stagingBufferSize = sizeof(vertexData[0]) * vertexData.size();
	VkDeviceMemory stagingBufferMemory = 0;
	VkBuffer stagingBuffer = 0;
	stagingBuffer = CreateBuffer(logicalDevice, physicalDevice, stagingBufferMemory, stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	void* data;
	vkMapMemory(logicalDevice, stagingBufferMemory, 0, stagingBufferSize, 0, &data);
	memcpy(data, vertexData.data(), (size_t) stagingBufferSize);
	vkUnmapMemory(logicalDevice, stagingBufferMemory);

	VkDeviceSize vertexBufferSize = stagingBufferSize;
	VkDeviceMemory vertexBufferMemory = 0;
	vertexBuffer = CreateBuffer(logicalDevice, physicalDevice, vertexBufferMemory, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	CopyBuffer(logicalDevice, stagingBuffer, vertexBuffer, vertexBufferSize, copyPool);
	vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);

	return vertexBufferMemory;
}

VkDeviceMemory Engine::CreateIndicesBuffer(VkDevice& logicalDevice, VkPhysicalDevice& physicalDevice, VkBuffer& indicesBuffer, std::vector<uint16_t>& indices, VkCommandPool& copyPool) {
	VkDeviceSize stagingBufferSize = sizeof(indices[0]) * indices.size();
	VkDeviceMemory stagingBufferMemory = 0;
	VkBuffer stagingBuffer = 0;
	stagingBuffer = CreateBuffer(logicalDevice, physicalDevice, stagingBufferMemory, stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	void* data;
	vkMapMemory(logicalDevice, stagingBufferMemory, 0, stagingBufferSize, 0, &data);
	memcpy(data, indices.data(), (size_t)stagingBufferSize);
	vkUnmapMemory(logicalDevice, stagingBufferMemory);

	VkDeviceSize indicesBufferSize = stagingBufferSize;
	VkDeviceMemory indicesBufferMemory = 0;
	indicesBuffer = CreateBuffer(logicalDevice, physicalDevice, indicesBufferMemory, indicesBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	CopyBuffer(logicalDevice, stagingBuffer, indicesBuffer, indicesBufferSize, copyPool);
	vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);

	return indicesBufferMemory;
}

void Engine::CreateUniformBuffers(std::vector<VkBuffer>& uniformBuffers, std::vector<VkDeviceMemory>& uniformBuffersMemory) {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	uniformBuffers.resize(this->swapImages.size());
	uniformBuffersMemory.resize(this->swapImages.size());

	UniformBufferObject obj = {  };
	obj.model = glm::mat4x4(0.0f);
	obj.view = glm::mat4x4(2.0f);
	obj.projection = glm::mat4x4(1.0f);

	for (int i = 0; i < this->swapImages.size(); i++) {
		uniformBuffers[i] = CreateBuffer(this->logicalDevice, this->physicalDevice, uniformBuffersMemory[i], bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
}

void Engine::CreateDescriptorPool() {
	VkDescriptorPoolSize poolSize = {};
	poolSize.descriptorCount = static_cast<uint32_t>(this->swapImages.size());
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.poolSizeCount = 1;
	poolInfo.maxSets = static_cast<uint32_t>(this->swapImages.size());

	VKCheck(*"Could not create descriptor pool.", vkCreateDescriptorPool(this->logicalDevice, &poolInfo, nullptr, &this->descriptorPool));
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

		VkWriteDescriptorSet writeInfo = {};
		writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeInfo.descriptorCount = 1;
		writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeInfo.pBufferInfo = &bufferInfo;
		writeInfo.pImageInfo = nullptr;
		writeInfo.pTexelBufferView = nullptr;
		writeInfo.dstSet = this->descriptorSets[i];
		writeInfo.dstArrayElement = 0;
		writeInfo.dstBinding = 0;

		vkUpdateDescriptorSets(this->logicalDevice, 1, &writeInfo, 0, nullptr);
	}

}

std::vector<VkCommandBuffer> Engine::CreateCommandBuffers(VkDevice& logicalDevice, VkCommandPool& commandPool, std::vector<VkFramebuffer>& framebuffers, std::vector<Vertex>& vertexData, std::vector<uint16_t>& vertexIndices, VkBuffer& vertexBuffer, VkBuffer& indicesBuffer) {
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

		VkBuffer vertexBuffers[] = { vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffers[i], indicesBuffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout, 0, 1, &this->descriptorSets[i], 0, nullptr);

		vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(vertexIndices.size()), 1, 0, 0, 0);
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