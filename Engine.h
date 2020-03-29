#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <iostream>
#include <vector>
#include <assert.h>
#include <optional>
#include <set>
#include <fstream>
#include <stdexcept>

#pragma once

struct QueueFamilies {
	int count = 0;

	std::optional<uint32_t> graphicsQF = {};
	std::optional<uint32_t> presentationQF = {};

	bool isComplete() {
		return graphicsQF.has_value() && presentationQF.has_value();
	}

	bool isEmpty() {
		return (!this->graphicsQF.has_value()) && (!this->presentationQF.has_value()) && count == 0;
	}
};

struct SwapchainDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

class Engine
{
private:
	const char* TITLE = "V-Renderer";

	const uint32_t MAX_CONCURRENT_FRAMES = 2;

	const std::vector<const char*> debugLayers = { "VK_LAYER_KHRONOS_validation" };
	const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	GLFWwindow* window = nullptr;

	VkInstance instance = 0;
	VkSurfaceKHR surface = 0;
	VkPhysicalDevice physicalDevice = 0;
	VkQueue graphicsQueue = 0;
	VkQueue presentationQueue = 0;
	VkDevice logicalDevice = 0;
	VkSwapchainKHR swapchain = 0;
	VkRenderPass renderPass = 0;
	VkPipelineLayout pipelineLayout = 0;
	VkPipeline pipeline = 0;
	VkCommandPool commandPool = 0;

	std::vector<VkSemaphore> imagesAvailableSemaphores = {};
	std::vector<VkSemaphore> imagesRenderedSemaphores = {};

	std::vector<VkFence> inFlightFences = {};
	std::vector<VkFence> imagesInFlight = {};

	size_t currentFrame = 0;

	VkFormat swapImageFormat = {};
	VkExtent2D swapImageSize = {};

	QueueFamilies queueFamilies = {};
	SwapchainDetails swapchainDetails = {};

	std::vector<VkImage> swapImages = {};
	std::vector<VkImageView> swapImageViews = {};
	std::vector<VkFramebuffer> framebuffers = {};
	std::vector<VkCommandBuffer> commandBuffers = {};
public:
	size_t WIN_W = 800;
	size_t WIN_H = 600;

	bool resizeTriggered = false;

	Engine();
	~Engine();

	bool VKCheck(const char& message, VkResult result);

	void Load();
	void Start();
	void Render();
	void RecreateSwapchain();
	void CloseSwapchain();
	void Close();

	void DestroyFramebuffers();
	void DestroyImageViews();
	void DestroySyncObjects();

	void ValidateDebugLayers(const std::vector<const char*>& debugLayers);
	bool ValidateDeviceExtensions(VkPhysicalDevice& physicalDevice, const std::vector<const char*>& deviceExtensions);

	std::vector<char> ReadFile(const std::string& fileName);

	QueueFamilies GetQueueFamilies(VkPhysicalDevice& physicalDevice, VkSurfaceKHR& surface);
	SwapchainDetails GetSwapchainDetails(VkPhysicalDevice& physicalDevice, VkSurfaceKHR& surface);
	VkSurfaceFormatKHR GetSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& surfaceFormats);
	VkPresentModeKHR GetSurfacePresentMode(const std::vector<VkPresentModeKHR>& presentModes);
	VkExtent2D GetSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, size_t& WIN_W, size_t& WIN_H);
	std::vector<VkImage> GetSwapImages(VkDevice& logicalDevice, VkSwapchainKHR& swapchain);

	VkInstance CreateInstance(const std::vector<const char*>& debugLayers);
	VkSurfaceKHR CreateWindowSurface(VkInstance& instance, GLFWwindow* window);
	VkPhysicalDevice CreatePhysicalDevice(VkInstance& instance, VkSurfaceKHR& surface, const std::vector<const char*> deviceExtensions);
	VkDevice CreateLogicalDevice(VkPhysicalDevice& physicalDevice, QueueFamilies& qf, const std::vector<const char*>& deviceExtensions);
	VkSwapchainKHR CreateSwapchain(size_t& WIN_W, size_t& WIN_H, VkDevice& device, VkSurfaceKHR& surface, SwapchainDetails& swapChainDetails, QueueFamilies& qf);
	std::vector<VkImageView> CreateImageViews(VkDevice& device, std::vector<VkImage>& images, VkFormat& imageFormat);
	VkShaderModule CreateShaderModule(VkDevice& logicalDevice, const std::vector<char>& byteCode);
	VkRenderPass CreateRenderPass(VkDevice& logicalDevice, VkFormat& format);
	VkPipeline CreateGraphicsPipeline(VkDevice& device, VkRenderPass& renderPass, VkExtent2D& extent);
	std::vector<VkFramebuffer> CreateFramebuffer(VkDevice& logicalDevice, std::vector<VkImageView>& imageViews, VkRenderPass& renderPass, VkExtent2D& extent);
	VkCommandPool CreateCommandPool(VkDevice& device, uint32_t& familyIndex);
	std::vector<VkCommandBuffer> CreateCommandBuffers(VkDevice& logicalDevice, VkCommandPool& commandPool, std::vector<VkFramebuffer>& framebuffers);
	void CreateSyncObjects(std::vector<VkSemaphore>& imageAvailableSemaphores, std::vector<VkSemaphore>& imageRenderedSemaphores, std::vector<VkFence>& inFlightFences, std::vector<VkFence>& imagesInFlight);
};