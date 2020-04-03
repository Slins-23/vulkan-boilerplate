#define GLFW_EXPOSE_NATIVE_WIN32
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
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
#include <unordered_map>

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
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projection;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec2 texCoord;

	static VkVertexInputBindingDescription GetBindingDescription() {
		VkVertexInputBindingDescription inputBindingDescription = {};
		inputBindingDescription.binding = 0;
		inputBindingDescription.stride = sizeof(Vertex);
		inputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return inputBindingDescription;
	}

	static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions(3);

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, position);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

		return attributeDescriptions;
	}

	bool operator==(const Vertex& other) const {
		return this->position == other.position && this->texCoord == other.texCoord && this->color == other.color;
	}
};

template<> struct std::hash<Vertex> {
	size_t operator()(Vertex const& vertex) const {
		return ((std::hash<glm::vec3>()(vertex.position) ^ (std::hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (std::hash<glm::vec2>()(vertex.texCoord) << 1);
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

	VkSampler sampler = 0;

	std::vector<VkImage> textureImages = {};
	std::vector<VkImageView> textureImageViews = {};
	std::vector<VkDeviceMemory> textureImagesMemory = {};

	VkImage depthImage;
	VkImageView depthImageView;
	VkDeviceMemory depthImageMemory;

	uint32_t mipLevels;

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	VkImage colorImage;
	VkImageView colorImageView;
	VkDeviceMemory colorImageMemory;
public:
	size_t WIN_W = 800;
	size_t WIN_H = 600;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	float FOV = 45.0f;
	float NEAREST = 0.1f;
	float FARTHEST = 10.0f;
	float ROTATION_ANGLE = 0.0f;
	glm::vec3 UP ={0.0f, 0.0f, 1.0f};
	glm::vec3 CENTER = { 0.0f, 0.0f, 0.0f };
	glm::vec3 ROTATION_AXIS = { 0.0f, 0.0f, 1.0f };
	glm::vec3 CAMERA_POSITION = { 2.0f, 2.0f, 2.0f };

	const char* MODEL_PATH = "models/chalet.obj";
	const char* TEXTURE_PATH = "textures/chalet.jpg";

	bool resizeTriggered = false;

	Engine();
	~Engine();

	void VKCheck(const char* message, VkResult result);

	void Load();
	void Start();
	void Render();
	void UpdateUniformBuffers(uint32_t currentImage);
	void RecreateSwapchain();
	void CloseSwapchain();
	void Close();

	void DestroyFramebuffers();
	void DestroySwapImageViews();
	void DestroyTextureImageViews();
	void DestroySyncObjects();

	void ValidateDebugLayers(const std::vector<const char*>& debugLayers);
	bool ValidateDeviceExtensions(VkPhysicalDevice& physicalDevice, const std::vector<const char*>& deviceExtensions);

	std::vector<char> ReadFile(const std::string& fileName);

	VkShaderModule CreateShaderModule(const std::vector<char>& byteCode);
	VkBuffer CreateBuffer(VkDeviceMemory& bufferMemory, VkDeviceSize& size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlagBits);

	VkSurfaceFormatKHR GetSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
	VkPresentModeKHR GetSurfacePresentMode(const std::vector<VkPresentModeKHR>& presentModes);
	VkExtent2D GetSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, size_t& WIN_W, size_t& WIN_H);
	VkFormat GetSupportedFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags features);
	VkSampleCountFlagBits GetMSAASupport();
	uint32_t GetMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
	void GetQueueFamilies();
	void GetSwapImages(VkSwapchainKHR& swapchain, std::vector<VkImage>& swapImages);
	void GetSwapchainDetails(VkPhysicalDevice& physicalDevice, SwapchainDetails& details);

	void BeginSingleTimeCommands(VkCommandBuffer& commandBuffer, VkCommandPool& commandPool);
	void EndSingleTimeCommands(VkCommandBuffer& commandBuffer, VkCommandPool& commandPool);

	void CreateColorResources();
	void CreateColorImageView();

	void CreateInstance();
	void CreateWindowSurface();
	void CreatePhysicalDevice();
	void CreateLogicalDevice();
	void CreateSwapchain(VkSwapchainKHR& swapchain, size_t& WIN_W, size_t& WIN_H);
	void CreateImageViews();
	void CreateRenderPass();
	void CreateDescriptorSetLayout();
	void CreateGraphicsPipeline();
	void CreateFramebuffers();
	void CreateCommandPool(VkCommandPool& commandPool, uint32_t& familyIndex);
	bool hasStencil(VkFormat format);
	void CreateDepthResources();
	void CreateDepthImageView(VkFormat& depthFormat);
	void CreateImage(VkImage& image, VkDeviceMemory& imageMemory, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
	void CreateModel(const char* name);
	void CreateTextureImage(const char* name);
	void CreateTextureImageViews(uint32_t mipLevels);
	void CreateTextureSampler();
	void TransitionImageLayout(VkImage& image, uint32_t mipLevels, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
	void GenerateMipmaps(VkImage& image, int32_t im_w, int32_t im_h, uint32_t mipLevels, VkFormat imgFormat);
	void CopyBufferToImage(VkBuffer& srcBuffer, VkImage& srcImage, uint32_t width, uint32_t height);
	void CopyBuffer(VkBuffer& srcBuffer, VkBuffer& dstBuffer, VkDeviceSize& size, VkCommandPool& commandPool);
	void CreateVertexBuffer();
	void CreateIndicesBuffer();
	void CreateUniformBuffers();
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void CreateSyncObjects();
	void CreateCommandBuffers();
};