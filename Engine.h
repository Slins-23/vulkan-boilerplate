#define GLFW_EXPOSE_NATIVE_WIN32
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
#include <chrono>

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

struct UniformBufferObject {
	glm::mat4x4 model;
	glm::mat4x4 view;
	glm::mat4x4 projection;
};

struct Vertex {
	glm::vec2 position;
	glm::vec3 color;

	static VkVertexInputBindingDescription GetBindingDescription() {
		VkVertexInputBindingDescription inputBindingDescription = {};
		inputBindingDescription.binding = 0;
		inputBindingDescription.stride = sizeof(Vertex);
		inputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return inputBindingDescription;
	}

	static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, position);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		return attributeDescriptions;
	}
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
	VkDescriptorSetLayout descriptorSetLayout = 0;
	VkBuffer vertexBuffer = 0;
	VkBuffer indicesBuffer = 0;
	VkDeviceMemory vertexMemory = 0;
	VkDeviceMemory indicesMemory = 0;
	VkCommandPool copyPool = 0;
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

	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;

	VkDescriptorPool descriptorPool = 0;

	std::vector<VkDescriptorSet> descriptorSets;
public:
	size_t WIN_W = 800;
	size_t WIN_H = 600;

	std::vector<Vertex> vertexData = {
{
	{{ -0.5f }, { -0.5f }},
	{{ 1.0f }, { 0.5f }, { 0.5f }},
},
{
	{{ 0.5f }, { -0.5f }},
	{{ 0.5f }, { 0.1f }, { 1.0f }},
},
{
	{{ 0.5f }, { 0.5f }},
	{{ 0.0f }, { 0.5f }, { 1.0f }},
},
{
	{{ -0.5f }, { 0.5 }},
	{{0.23f}, { 0.10f }, {0.00f}},
}
	};

	std::vector<uint16_t> vertexIndices = {0, 1, 2, 2, 3, 0};

	bool resizeTriggered = false;

	Engine();
	~Engine();

	bool VKCheck(const char& message, VkResult result);

	void Load();
	void Start();
	void Render();
	void UpdateUniformBuffers(uint32_t currentImage);
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
	uint32_t GetMemoryType(VkPhysicalDevice& physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

	VkInstance CreateInstance(const std::vector<const char*>& debugLayers);
	VkSurfaceKHR CreateWindowSurface(VkInstance& instance, GLFWwindow* window);
	VkPhysicalDevice CreatePhysicalDevice(VkInstance& instance, VkSurfaceKHR& surface, const std::vector<const char*> deviceExtensions);
	VkDevice CreateLogicalDevice(VkPhysicalDevice& physicalDevice, QueueFamilies& qf, const std::vector<const char*>& deviceExtensions);
	VkSwapchainKHR CreateSwapchain(size_t& WIN_W, size_t& WIN_H, VkDevice& device, VkSurfaceKHR& surface, SwapchainDetails& swapChainDetails, QueueFamilies& qf);
	std::vector<VkImageView> CreateImageViews(VkDevice& device, std::vector<VkImage>& images, VkFormat& imageFormat);
	VkShaderModule CreateShaderModule(VkDevice& logicalDevice, const std::vector<char>& byteCode);
	VkRenderPass CreateRenderPass(VkDevice& logicalDevice, VkFormat& format);
	VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice& logicalDevice);
	VkPipeline CreateGraphicsPipeline(VkDevice& device, VkDescriptorSetLayout& descriptorLayout, VkRenderPass& renderPass, VkExtent2D& extent);
	std::vector<VkFramebuffer> CreateFramebuffer(VkDevice& logicalDevice, std::vector<VkImageView>& imageViews, VkRenderPass& renderPass, VkExtent2D& extent);
	VkCommandPool CreateCommandPool(VkDevice& device, uint32_t& familyIndex);
	VkBuffer CreateBuffer(VkDevice& logicalDevice, VkPhysicalDevice& physicalDevice, VkDeviceMemory& bufferMemory, VkDeviceSize& size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlagBits);
	void CopyBuffer(VkDevice& logicalDevice, VkBuffer& srcBuffer, VkBuffer& dstBuffer, VkDeviceSize& size, VkCommandPool& commandPool);
	VkDeviceMemory CreateVertexBuffer(VkDevice& logicalDevice, VkPhysicalDevice& physicalDevice, VkBuffer& vertexBuffer, std::vector<Vertex>& vertexData, VkCommandPool& copyPool);
	VkDeviceMemory CreateIndicesBuffer(VkDevice& logicalDevice, VkPhysicalDevice& physicalDevice, VkBuffer& indicesBuffer, std::vector<uint16_t>& indices, VkCommandPool& copyPool);
	void CreateUniformBuffers(std::vector<VkBuffer>& uniformBuffers, std::vector<VkDeviceMemory>& uniformBuffersMemory);
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	std::vector<VkCommandBuffer> CreateCommandBuffers(VkDevice& logicalDevice, VkCommandPool& commandPool, std::vector<VkFramebuffer>& framebuffers, std::vector<Vertex>& vertexData, std::vector<uint16_t>& vertexIndices, VkBuffer& vertexBuffer, VkBuffer& indicesBuffer);
	void CreateSyncObjects(std::vector<VkSemaphore>& imageAvailableSemaphores, std::vector<VkSemaphore>& imageRenderedSemaphores, std::vector<VkFence>& inFlightFences, std::vector<VkFence>& imagesInFlight);
};