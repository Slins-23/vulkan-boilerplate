#include "Engine.h"

Engine::Engine() {

}

Engine::~Engine() {

}

void Engine::VKCheck(const char& message, VkResult result) {
	 if (result != VK_SUCCESS) {
		throw std::runtime_error(message + " Error code: " + result);
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
	CreateFramebuffers();
	CreateCommandPool(this->commandPool, this->queueFamilies.graphicsQF.value());
	CreateCommandPool(this->copyPool, this->queueFamilies.graphicsQF.value());
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

	GetSwapchainDetails(this->physicalDevice, this->swapchainDetails);
	CreateSwapchain(this->swapchain, this->WIN_W, this->WIN_H);
	GetSwapImages(this->swapchain, this->swapImages);
	CreateImageViews();
	CreateRenderPass();
	CreateGraphicsPipeline();
	CreateFramebuffers();
	CreateUniformBuffers();
	CreateDescriptorPool();
	CreateDescriptorSets();
	CreateCommandBuffers();
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

VkShaderModule Engine::CreateShaderModule(const std::vector<char>& byteCode) {
	VkShaderModule shaderModule;

	VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
	shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderModuleCreateInfo.codeSize = byteCode.size();
	shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(byteCode.data());

	VKCheck(*"Could not create shader module.", vkCreateShaderModule(this->logicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule));

	return shaderModule;
}

VkBuffer Engine::CreateBuffer(VkDeviceMemory& bufferMemory, VkDeviceSize& size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlagBits) {
	VkBuffer buffer;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usageFlags;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VKCheck(*"Could not create buffer.", vkCreateBuffer(this->logicalDevice, &bufferInfo, nullptr, &buffer));

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(this->logicalDevice, buffer, &memoryRequirements);

	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = GetMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	VKCheck(*"Could not allocate buffer memory.", vkAllocateMemory(this->logicalDevice, &allocateInfo, nullptr, &bufferMemory));

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

	VKCheck(*"Could not create instance.", vkCreateInstance(&createInfo, nullptr, &this->instance));
}

void Engine::CreateWindowSurface() {
	VkWin32SurfaceCreateInfoKHR winCreateSurfaceInfo = {};
	winCreateSurfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	winCreateSurfaceInfo.hinstance = GetModuleHandle(0);
	winCreateSurfaceInfo.hwnd = glfwGetWin32Window(this->window);

	VKCheck(*"Could not create Win32 surface.", vkCreateWin32SurfaceKHR(this->instance, &winCreateSurfaceInfo, nullptr, &this->surface));
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

		if (supportsSwapchain && physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			std::cout << "Using GPU: " << physicalDeviceProperties.deviceName << std::endl;
			this->physicalDevice = device;
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

	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

	deviceCreateInfo.ppEnabledExtensionNames = this->deviceExtensions.data();
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(this->deviceExtensions.size());

	VKCheck(*"Could not create device.", vkCreateDevice(this->physicalDevice, &deviceCreateInfo, nullptr, &this->logicalDevice));

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

	VKCheck(*"Could not create swapchain.", vkCreateSwapchainKHR(this->logicalDevice, &swapChainCreateInfo, nullptr, &swapchain));

	this->swapImageFormat = swapChainCreateInfo.imageFormat;
	this->swapImageSize = swapChainCreateInfo.imageExtent;
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
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = this->swapImageFormat;
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

	VKCheck(*"Could not create render pass.", vkCreateRenderPass(this->logicalDevice, &renderPassInfo, nullptr, &this->renderPass));
}

void Engine::CreateDescriptorSetLayout() {
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

	VKCheck(*"Could not create descriptor set layout.", vkCreateDescriptorSetLayout(this->logicalDevice, &layoutInfo, nullptr, &this->descriptorSetLayout));
}

void Engine::CreateGraphicsPipeline() {
	std::vector<char> shaderVert = ReadFile("shaders/vert.spv");
	std::vector<char> shaderFrag = ReadFile("shaders/frag.spv");

	VkShaderModule shaderVertModule = CreateShaderModule(shaderVert);
	VkShaderModule shaderFragModule = CreateShaderModule(shaderFrag);
 
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

	VKCheck(*"Could not create pipeline layout.", vkCreatePipelineLayout(this->logicalDevice, &pipelineLayoutInfo, nullptr, &this->pipelineLayout));

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

	VKCheck(*"Could not create graphics pipeline.", vkCreateGraphicsPipelines(this->logicalDevice, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, nullptr, &this->pipeline));

	vkDestroyShaderModule(this->logicalDevice, shaderVertModule, nullptr);
	vkDestroyShaderModule(this->logicalDevice, shaderFragModule, nullptr);
}

void Engine::CreateFramebuffers() {
	this->framebuffers.resize(this->swapImageViews.size());

	uint32_t idx = 0;
	for (VkImageView imageView : this->swapImageViews) {
		VkFramebufferCreateInfo frameBufferInfo = {};
		frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferInfo.renderPass = this->renderPass;
		frameBufferInfo.attachmentCount = 1;
		frameBufferInfo.pAttachments = &imageView;
		frameBufferInfo.width = this->swapImageSize.width;
		frameBufferInfo.height = this->swapImageSize.height;
		frameBufferInfo.layers = 1;

		VKCheck(*"Could not create framebuffer.", vkCreateFramebuffer(this->logicalDevice, &frameBufferInfo, nullptr, &this->framebuffers[idx]));
		idx++;
	}
}

void Engine::CreateCommandPool(VkCommandPool& commandPool, uint32_t& familyIndex) {
	VkCommandPoolCreateInfo commandPoolCreateInfo = {};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = familyIndex;

	VKCheck(*"Could not create command pool.", vkCreateCommandPool(this->logicalDevice, &commandPoolCreateInfo, nullptr, &commandPool));
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

void Engine::CreateVertexBuffer() {
	VkDeviceSize stagingBufferSize = sizeof(this->vertexData[0]) * this->vertexData.size();
	VkDeviceMemory stagingBufferMemory = 0;
	VkBuffer stagingBuffer = 0;
	stagingBuffer = CreateBuffer(stagingBufferMemory, stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	void* data;
	vkMapMemory(this->logicalDevice, stagingBufferMemory, 0, stagingBufferSize, 0, &data);
	memcpy(data, this->vertexData.data(), (size_t) stagingBufferSize);
	vkUnmapMemory(this->logicalDevice, stagingBufferMemory);

	VkDeviceSize vertexBufferSize = stagingBufferSize;
	VkDeviceMemory vertexBufferMemory = 0;
	this->vertexBuffer = CreateBuffer(vertexBufferMemory, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	this->vertexMemory = vertexBufferMemory;

	CopyBuffer(this->logicalDevice, stagingBuffer, this->vertexBuffer, vertexBufferSize, this->copyPool);
	vkDestroyBuffer(this->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, stagingBufferMemory, nullptr);
}

void Engine::CreateIndicesBuffer() {
	VkDeviceSize stagingBufferSize = sizeof(this->vertexIndices[0]) * this->vertexIndices.size();
	VkDeviceMemory stagingBufferMemory = 0;
	VkBuffer stagingBuffer = 0;
	stagingBuffer = CreateBuffer(stagingBufferMemory, stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	void* data;
	vkMapMemory(this->logicalDevice, stagingBufferMemory, 0, stagingBufferSize, 0, &data);
	memcpy(data, this->vertexIndices.data(), (size_t)stagingBufferSize);
	vkUnmapMemory(this->logicalDevice, stagingBufferMemory);

	VkDeviceSize indicesBufferSize = stagingBufferSize;
	VkDeviceMemory indicesBufferMemory = 0;
	this->indicesBuffer = CreateBuffer(indicesBufferMemory, indicesBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	this->indicesMemory = indicesBufferMemory;

	CopyBuffer(this->logicalDevice, stagingBuffer, this->indicesBuffer, indicesBufferSize, this->copyPool);
	vkDestroyBuffer(this->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(this->logicalDevice, stagingBufferMemory, nullptr);
}

void Engine::CreateUniformBuffers() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	this->uniformBuffers.resize(this->swapImages.size());
	this->uniformBuffersMemory.resize(this->swapImages.size());

	UniformBufferObject obj = {  };
	obj.model = glm::mat4x4(0.0f);
	obj.view = glm::mat4x4(2.0f);
	obj.projection = glm::mat4x4(1.0f);

	for (int i = 0; i < this->swapImages.size(); i++) {
		this->uniformBuffers[i] = CreateBuffer(this->uniformBuffersMemory[i], bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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

void Engine::CreateCommandBuffers() {
	this->commandBuffers.resize(this->framebuffers.size());

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.commandPool = this->commandPool;
	commandBufferAllocateInfo.commandBufferCount = (uint32_t)commandBuffers.size();
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VKCheck(*"Could not allocate command buffers.", vkAllocateCommandBuffers(this->logicalDevice, &commandBufferAllocateInfo, this->commandBuffers.data()));

	VkClearValue clearColor = { 0.25f, 0.50f, 0.75f, 1.0f };

	for (size_t i = 0; i < this->commandBuffers.size(); i++) {
		VkCommandBufferBeginInfo commandBufferBeginInfo = {};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBufferBeginInfo.pInheritanceInfo = nullptr;
		commandBufferBeginInfo.flags = 0;

		VKCheck(*"Could not begin command buffer.", vkBeginCommandBuffer(this->commandBuffers[i], &commandBufferBeginInfo));

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.framebuffer = this->framebuffers[i];
		renderPassBeginInfo.renderPass = this->renderPass;
		renderPassBeginInfo.renderArea.extent = this->swapImageSize;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(this->commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(this->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline);

		VkBuffer vertexBuffers[] = { this->vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(this->commandBuffers[i], 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(this->commandBuffers[i], this->indicesBuffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdBindDescriptorSets(this->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout, 0, 1, &this->descriptorSets[i], 0, nullptr);

		vkCmdDrawIndexed(this->commandBuffers[i], static_cast<uint32_t>(this->vertexIndices.size()), 1, 0, 0, 0);
		vkCmdEndRenderPass(this->commandBuffers[i]);
		VKCheck(*"Failed to record command buffer.", vkEndCommandBuffer(this->commandBuffers[i]));
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
		VKCheck(*"Could not create signal sempahores.", vkCreateSemaphore(this->logicalDevice, &semaphoreCreateInfo, nullptr, &this->imagesAvailableSemaphores[i]));
		VKCheck(*"Could not create wait sempahores.", vkCreateSemaphore(this->logicalDevice, &semaphoreCreateInfo, nullptr, &this->imagesRenderedSemaphores[i]));
		VKCheck(*"Could not create working fences.", vkCreateFence(this->logicalDevice, &fenceCreateInfo, nullptr, &this->inFlightFences[i]));
	}
}