
#define VOLK_IMPLEMENTATION
#include<volk/volk.h>

#include<SDL3/SDL.h>
#include<SDL3/SDL_vulkan.h>

#define VMA_IMPLEMENTATION
#include<vma/vk_mem_alloc.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/quaternion.hpp>

#include "slang/slang.h"
#include "slang/slang-com-ptr.h"

#define KHRONOS_STATIC 
#include<ktx.h>
#include<ktxvulkan.h>

//currently, data might not be per mesh but collective for handlers buffers etc...
//For now, remember inside of the cpp equivalent of imgui_compile => we have used define for it to use volk
//cross quads for fuller flowers
//removing LOD, because LOD is hard to think without chunking, so will implement it later with chunking

#include "imgui_compile.h"

#include<iostream>
#include<vector>
#include<array>
#include<string>
#include<fstream>

struct ValidationHelpers {
	void validateResult( bool result, std::string message = "ERROR!") {
		if (!result) {
			std::cout << std::endl << "ERROR: " << message << std::endl;
			std::cout << "Exiting Program....";
			exit(0);
		}
	}

	void validateResult( VkResult result, std::string message = "ERROR!") {
		if ( result!= VK_SUCCESS ) {
			std::cout << std::endl << "ERROR: " << message << std::endl;
			std::cout << "Exiting Program....";
			exit(0);
		}
	}

	static void imguiWrapper(VkResult result) {
		if (result != VK_SUCCESS) {
			std::cout << std::endl << "ERROR: " << std::endl;
			std::cout << "Exiting Program....";
			exit(0);
		}
	}

	bool validateSwapchain(VkResult result ) {
		if (result < VK_SUCCESS) {
			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				return true;
			}

			std::cerr << "Vulkan call returned an error: " << result << std::endl;
			exit(result);
		}

		return false;
	}
};

class Mesh {
public:
	std::vector< uint16_t > _indices{};

	VkDeviceSize indicesBufferSize{};

public:
	void compute() {
		indicesBufferSize = _indices.size() * sizeof(uint16_t);
	}

	void setIndices(std::vector< uint16_t > indices) {
		_indices = indices;
	}

	VkDeviceSize getIndicesBufferSize() {
		return indicesBufferSize;
	}

public:
		VkBuffer indicesBuffer{};
		VmaAllocation indicesBufferAllocation{};
		VmaAllocationInfo indicesBufferAllocationInfo{};

	std::unordered_map< std::string, float > userData{};

	glm::fvec3 BASE_COLOR;
	glm::fvec3 TIP_COLOR;
	glm::fvec3 PATCH_BASE_COLOR;
	glm::fvec3 PATCH_TIP_COLOR;
	std::string textureName{};
	int textureIndex = -1;
	bool patchyGrass = false;
	bool clump = false;
};

class UserInterface {

};

class Simulation {
	public:
		void setupLibraries() {
			validationHelpers.validateResult(SDL_Init(SDL_INIT_VIDEO), "Failed to Initialize SDL");
			validationHelpers.validateResult(SDL_Vulkan_LoadLibrary(NULL), "Failed to ask SDL to load Vulkan Dynamically");
			volkInitialize();
		}

		void setupInstance() {
			extensionsInterface.instance.extensions = SDL_Vulkan_GetInstanceExtensions(&extensionsInterface.instance.count);

			VkInstanceCreateInfo instanceCreateInfo{
				.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
				.pApplicationInfo = &windowConfiguration.applicationInfo,
				.enabledExtensionCount = extensionsInterface.instance.count,
				.ppEnabledExtensionNames = extensionsInterface.instance.extensions
			};

			validationHelpers.validateResult(vkCreateInstance(&instanceCreateInfo, nullptr, &windowConfiguration.vkInstance ));
			volkLoadInstance(windowConfiguration.vkInstance);
		}

		void pickPhysicalDeviceAndQueue() {
			validationHelpers.validateResult( vkEnumeratePhysicalDevices( windowConfiguration.vkInstance, &deviceInterface.physical.count, nullptr ) );

			deviceInterface.physical.devices.resize(deviceInterface.physical.count);
			deviceInterface.queue.properties.resize(deviceInterface.physical.count);

			validationHelpers.validateResult( vkEnumeratePhysicalDevices(windowConfiguration.vkInstance, &deviceInterface.physical.count, deviceInterface.physical.devices.data()));

			for (uint32_t i = 0; i < deviceInterface.physical.count; i++) {
				VkPhysicalDeviceProperties2 properties{
					.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
				};

				vkGetPhysicalDeviceProperties2(deviceInterface.physical.devices[i], &properties);
				deviceInterface.physical.properties.push_back(properties);

				uint32_t queueFamiliesCount{};
				vkGetPhysicalDeviceQueueFamilyProperties2(deviceInterface.physical.devices[i], &queueFamiliesCount, nullptr);
				deviceInterface.queue.properties[i].resize(queueFamiliesCount);

				for (uint32_t j = 0; j < queueFamiliesCount; j++) {
					deviceInterface.queue.properties[i][j].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
				}

				vkGetPhysicalDeviceQueueFamilyProperties2(deviceInterface.physical.devices[i], &queueFamiliesCount, deviceInterface.queue.properties[i].data());
			}

			for( uint32_t i = 0; i < deviceInterface.physical.count; i++ ){
				bool cond1 = deviceInterface.physical.properties[i].properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
				bool found = false;

				for (uint32_t j = 0; j < deviceInterface.queue.properties[i].size(); j++) {
					if (deviceInterface.queue.properties[i][j].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && cond1) {
						deviceInterface.physical.index = i;
						deviceInterface.queue.index = j;

						found = true;

						break;
					}
				}

				if (found) {
					break;
				}
			}

			validationHelpers.validateResult(SDL_Vulkan_GetPresentationSupport( windowConfiguration.vkInstance, deviceInterface.physical.devices[ deviceInterface.physical.index], deviceInterface.queue.index ));
		}

		void setupLogicalDevice() {
			VkDeviceQueueCreateInfo queueCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = deviceInterface.queue.index,
				.queueCount = 1,
				.pQueuePriorities = &deviceInterface.queue.priorities
			};

			VkPhysicalDeviceVulkan11Features enabledVk11Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
				.shaderDrawParameters = VK_TRUE
			};

			VkPhysicalDeviceVulkan12Features enabledVk12Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES	,
				.pNext = &enabledVk11Features,
				.descriptorIndexing = true,
				.shaderSampledImageArrayNonUniformIndexing = true,
				.descriptorBindingVariableDescriptorCount = true,
				.runtimeDescriptorArray = true,
				.bufferDeviceAddress = true
			};

			VkPhysicalDeviceVulkan13Features enabledVk13Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
				.pNext = &enabledVk12Features,
				.synchronization2 = true,
				.dynamicRendering = true
			};

			VkPhysicalDeviceFeatures enabledVk10Features{
				.samplerAnisotropy = VK_TRUE,
			};

			VkDeviceCreateInfo deviceCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.pNext = &enabledVk13Features,
				.queueCreateInfoCount = 1,
				.pQueueCreateInfos = &queueCreateInfo,
				.enabledExtensionCount = static_cast<uint32_t> ( extensionsInterface.device.extensions.size()),
				.ppEnabledExtensionNames = extensionsInterface.device.extensions.data(),
				.pEnabledFeatures = &enabledVk10Features
			};

			validationHelpers.validateResult(vkCreateDevice( deviceInterface.physical.devices[ deviceInterface.physical.index ], &deviceCreateInfo, nullptr, &windowConfiguration.device ));

			volkLoadDevice( windowConfiguration.device );
			vkGetDeviceQueue( windowConfiguration.device, deviceInterface.queue.index, 0, &windowConfiguration.queue );
		}

		void setupVMA() {
			VmaVulkanFunctions vulkanFunctions{
				.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
				.vkGetDeviceProcAddr = vkGetDeviceProcAddr
			};

			VmaAllocatorCreateInfo allocatorCreateInfo{
				.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
				.physicalDevice = deviceInterface.physical.devices[ deviceInterface.physical.index ],
				.device = windowConfiguration.device,
				.pVulkanFunctions = &vulkanFunctions,
				.instance = windowConfiguration.vkInstance
			};

			validationHelpers.validateResult(vmaCreateAllocator(&allocatorCreateInfo, &windowConfiguration.allocator));
		}

		void setupSynchronization() {
			VkSemaphoreCreateInfo semaphoreCreateInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
			};

			VkFenceCreateInfo fenceCreateInfo{
				.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.flags = VK_FENCE_CREATE_SIGNALED_BIT
			};

			for (uint32_t index = 0; index < framesConfiguration.maxFramesInFlight; index++) {
				validationHelpers.validateResult(vkCreateFence( windowConfiguration.device, &fenceCreateInfo, nullptr, &framesConfiguration.fences[index]));
				validationHelpers.validateResult(vkCreateSemaphore( windowConfiguration.device, &semaphoreCreateInfo, nullptr, &framesConfiguration.presentSemaphores[index]));
			}

			deviceInterface.logical.swapchainConfiguration.renderSemaphores.resize(deviceInterface.logical.swapchainConfiguration.imageCount);
			for (VkSemaphore& semaphore : deviceInterface.logical.swapchainConfiguration.renderSemaphores) {
				validationHelpers.validateResult(vkCreateSemaphore( windowConfiguration.device, &semaphoreCreateInfo, nullptr, &semaphore));
			}
		}

		void setupSwapChain() {
			deviceInterface.logical.swapchainConfiguration.swapchainCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
				.surface = windowConfiguration.surface,
				.minImageCount = windowConfiguration.surfaceCapabilities.minImageCount,
				.imageFormat = deviceInterface.logical.swapchainConfiguration.imageFormat,
				.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
				.imageExtent = {
					windowConfiguration.surfaceCapabilities.currentExtent.width,
					windowConfiguration.surfaceCapabilities.currentExtent.height
				},
				.imageArrayLayers = 1,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
				.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				.presentMode = VK_PRESENT_MODE_FIFO_KHR
			};

			validationHelpers.validateResult(vkCreateSwapchainKHR( windowConfiguration.device, &deviceInterface.logical.swapchainConfiguration.swapchainCreateInfo, nullptr, &deviceInterface.logical.swapchainConfiguration.swapchain));

			validationHelpers.validateResult(vkGetSwapchainImagesKHR( windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.swapchain, &deviceInterface.logical.swapchainConfiguration.imageCount, nullptr));
			deviceInterface.logical.swapchainConfiguration.images.resize(deviceInterface.logical.swapchainConfiguration.imageCount);
			validationHelpers.validateResult(vkGetSwapchainImagesKHR( windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.swapchain, &deviceInterface.logical.swapchainConfiguration.imageCount, deviceInterface.logical.swapchainConfiguration.images.data()));
			deviceInterface.logical.swapchainConfiguration.imageViews.resize(deviceInterface.logical.swapchainConfiguration.imageCount);

			for (uint32_t index = 0; index < deviceInterface.logical.swapchainConfiguration.imageCount; index++) {
				VkImageViewCreateInfo imageViewCreateInfo{
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.image = deviceInterface.logical.swapchainConfiguration.images[index],
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = deviceInterface.logical.swapchainConfiguration.imageFormat,
					.subresourceRange{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.levelCount = 1,
						.layerCount = 1
					}
				};

				validationHelpers.validateResult(vkCreateImageView( windowConfiguration.device, &imageViewCreateInfo, nullptr, &deviceInterface.logical.swapchainConfiguration.imageViews[index]));
			}
		}

		void setupUIUI() {
			windowConfiguration.window = SDL_CreateWindow(
				windowConfiguration.windowName.c_str(),
				windowConfiguration.windowWidth,
				windowConfiguration.windowHeight,
				windowConfiguration.windowFlags
			);

			validationHelpers.validateResult(SDL_GetWindowSize(windowConfiguration.window, &windowConfiguration.windowSize.x, &windowConfiguration.windowSize.y), "Failed to Get Window Size");

			validationHelpers.validateResult(SDL_Vulkan_CreateSurface(windowConfiguration.window, windowConfiguration.vkInstance, nullptr, &windowConfiguration.surface));
			validationHelpers.validateResult(
				vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
					deviceInterface.physical.devices[deviceInterface.physical.index],
					windowConfiguration.surface, &windowConfiguration.surfaceCapabilities
				)
			);
		}

		void setupUI() {

			VkDescriptorPoolSize pool_sizes[] = {
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
				{ VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
			};

			VkDescriptorPoolCreateInfo pool_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
				.maxSets = 1000,
				.poolSizeCount = (uint32_t)std::size(pool_sizes),
				.pPoolSizes = pool_sizes
			};

			
			vkCreateDescriptorPool( windowConfiguration.device, &pool_info, nullptr, &imguiPool);

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

			ImGui_ImplSDL3_InitForVulkan( windowConfiguration.window );
			ImGui_ImplVulkan_InitInfo init_info = {};
			init_info.ApiVersion = VK_API_VERSION_1_4; 
			init_info.Instance = windowConfiguration.vkInstance;
			init_info.PhysicalDevice = deviceInterface.physical.devices[ deviceInterface.physical.index ];
			init_info.Device = windowConfiguration.device;
			init_info.QueueFamily = deviceInterface.queue.index;
			init_info.Queue = windowConfiguration.queue;
			init_info.PipelineCache = pipelineCache;
			init_info.DescriptorPool = imguiPool;
			init_info.MinImageCount = windowConfiguration.surfaceCapabilities.minImageCount;
			init_info.ImageCount = deviceInterface.logical.swapchainConfiguration.imageCount;
			init_info.Allocator = nullptr;
			init_info.UseDynamicRendering = true;
			init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &deviceInterface.logical.swapchainConfiguration.imageFormat,
				.depthAttachmentFormat = depthAttachmentConfiguration.format
			};
			init_info.CheckVkResultFn = ValidationHelpers::imguiWrapper;

			ImGui_ImplVulkan_Init(&init_info);
		}

		void setupDepthAttachment() {
			for (VkFormat& format : depthAttachmentConfiguration.formatList) {
				vkGetPhysicalDeviceFormatProperties2( deviceInterface.physical.devices[ deviceInterface.physical.index ], format, &depthAttachmentConfiguration.formatProperties);
				if (depthAttachmentConfiguration.formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
					depthAttachmentConfiguration.format = format;
					break;
				}
			}

			depthAttachmentConfiguration.imageCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = depthAttachmentConfiguration.format,
				.extent = {
					.width = static_cast<uint32_t> (windowConfiguration.windowSize.x),
					.height = static_cast<uint32_t>(windowConfiguration.windowSize.y),
					.depth = 1
				},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
			};

			VmaAllocationCreateInfo allocationCreateInfo{
				.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO
			};

			validationHelpers.validateResult(vmaCreateImage( windowConfiguration.allocator, &depthAttachmentConfiguration.imageCreateInfo, &allocationCreateInfo, &depthAttachmentConfiguration.image, &depthAttachmentConfiguration.allocation, nullptr));

			VkImageViewCreateInfo imageViewCreateInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = depthAttachmentConfiguration.image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = depthAttachmentConfiguration.format,
				.subresourceRange {
					.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
					.levelCount = 1,
					.layerCount = 1
				}
			};

			validationHelpers.validateResult(vkCreateImageView( windowConfiguration.device, &imageViewCreateInfo, nullptr, &depthAttachmentConfiguration.imageView));
		}

		void setupCommandBuffers() {
			VkCommandPoolCreateInfo commandPoolCreateInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
				.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
				.queueFamilyIndex = deviceInterface.queue.index
			};

			validationHelpers.validateResult(vkCreateCommandPool( windowConfiguration.device, &commandPoolCreateInfo, nullptr, &windowConfiguration.commandPool));

			VkCommandBufferAllocateInfo commandBufferAllocateCreateInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = windowConfiguration.commandPool,
				.commandBufferCount = framesConfiguration.maxFramesInFlight
			};

			validationHelpers.validateResult(vkAllocateCommandBuffers( windowConfiguration.device, &commandBufferAllocateCreateInfo, framesConfiguration.commandBuffers.data()));
		}

		void loadShaders() {
			slang::createGlobalSession(slangGlobalSession.writeRef());

			auto slangTargets{
				std::to_array< slang::TargetDesc >({{
					.format{SLANG_SPIRV},
					.profile{slangGlobalSession->findProfile("spirv_1_4")}
				}})
			};

			auto slangOptions{
				std::to_array < slang::CompilerOptionEntry>({{
					slang::CompilerOptionName::EmitSpirvDirectly,
					{
						slang::CompilerOptionValueKind::Int, 1
					}
				}})
			};

			slang::SessionDesc slangSessionDesc{
				.targets{
					slangTargets.data()
				},
				.targetCount{
					SlangInt(slangTargets.size())
				},
				.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
				.compilerOptionEntries{
					slangOptions.data()
				},
				.compilerOptionEntryCount{
					uint32_t(slangOptions.size())
				}
			};

			Slang::ComPtr< slang::ISession > slangSession;
			slangGlobalSession->createSession(slangSessionDesc, slangSession.writeRef());

			Slang::ComPtr< slang::IModule > slangModule{
				slangSession->loadModuleFromSource("Modern Vulkan SLANG Shader", "assets/shaders/shader.slang", nullptr, nullptr)
			};

			Slang::ComPtr< ISlangBlob > spirv;
			slangModule->getTargetCode(0, spirv.writeRef());

			VkShaderModuleCreateInfo shaderModuleCreateInfo{
				.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
				.codeSize = spirv->getBufferSize(),
				.pCode = (uint32_t*)spirv->getBufferPointer()
			};

			validationHelpers.validateResult(vkCreateShaderModule( windowConfiguration.device, &shaderModuleCreateInfo, nullptr, &shaderModule), "Failed to Create Shader Module");
		
			
			Slang::ComPtr<slang::IEntryPoint> vsSkyEntryPoint;
			slangModule->findEntryPointByName("vs_sky", vsSkyEntryPoint.writeRef());

			Slang::ComPtr<slang::IEntryPoint> psSkyEntryPoint;
			slangModule->findEntryPointByName("ps_sky", psSkyEntryPoint.writeRef());

			slang::IComponentType* skyComponents[] = { slangModule, vsSkyEntryPoint, psSkyEntryPoint };
			Slang::ComPtr<slang::IComponentType> skyProgram;
			slangSession->createCompositeComponentType(skyComponents, 3, skyProgram.writeRef());

			Slang::ComPtr<ISlangBlob> skySpirv;
			skyProgram->getTargetCode(0, skySpirv.writeRef());

			VkShaderModuleCreateInfo skyInfo{
				.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
				.codeSize = skySpirv->getBufferSize(),
				.pCode = (uint32_t*)skySpirv->getBufferPointer()
			};
			vkCreateShaderModule( windowConfiguration.device, &skyInfo, nullptr, &skyModule);

			
		}

		void loadTextures() {
			int textureSlot = 0;
			for (uint32_t i = 0; i < meshes.size(); i++) {
				
				if (meshes[i].textureName == "") continue;

				ktxTexture* ktxTexture{ nullptr };
				std::string path = "assets/textures/" + meshes[i].textureName;
				ktxTexture_CreateFromNamedFile(path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);

				meshes[i].textureIndex = textureSlot;

				VkImageCreateInfo textureImageCreateInfo{
					.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
					.imageType = VK_IMAGE_TYPE_2D,
					.format = ktxTexture_GetVkFormat(ktxTexture),
					.extent = {
						.width = ktxTexture->baseWidth,
						.height = ktxTexture->baseHeight,
						.depth = 1
					},
					.mipLevels = ktxTexture->numLevels,
					.arrayLayers = 1,
					.samples = VK_SAMPLE_COUNT_1_BIT,
					.tiling = VK_IMAGE_TILING_OPTIMAL,
					.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
				};

				VmaAllocationCreateInfo textureImageAllocationCreateInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
				validationHelpers.validateResult(vmaCreateImage( windowConfiguration.allocator, &textureImageCreateInfo, &textureImageAllocationCreateInfo, &textureList[textureSlot].image, &textureList[textureSlot].allocation, nullptr), "Failed to Create Image for textures");

				VkImageViewCreateInfo textureImageViewCreateInfo{
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.image = textureList[textureSlot].image,
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = textureImageCreateInfo.format,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.levelCount = ktxTexture->numLevels,
						.layerCount = 1
					}
				};

				validationHelpers.validateResult(vkCreateImageView( windowConfiguration.device, &textureImageViewCreateInfo, nullptr, &textureList[textureSlot].view), "Failed to Create texture image view");

				VkBuffer imageSourceBuffer{};
				VmaAllocation imageSourceAllocation{};
				VkBufferCreateInfo imageSourceBufferCreateInfo{
					.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
					.size = (uint32_t)ktxTexture->dataSize,
					.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				};

				VmaAllocationCreateInfo imageSourceAllocationCreateInfo{
					.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
					.usage = VMA_MEMORY_USAGE_AUTO
				};

				validationHelpers.validateResult(vmaCreateBuffer( windowConfiguration.allocator, &imageSourceBufferCreateInfo, &imageSourceAllocationCreateInfo, &imageSourceBuffer, &imageSourceAllocation, nullptr), "Failed to Create VMA Buffer");

				void* imageSourceBufferPtr{ nullptr };
				validationHelpers.validateResult(vmaMapMemory( windowConfiguration.allocator, imageSourceAllocation, &imageSourceBufferPtr), "Failed to Map Memory for Staging Buffer of Image");
				memcpy(imageSourceBufferPtr, ktxTexture->pData, ktxTexture->dataSize);

				VkFenceCreateInfo fenceOneTimeCreateInfo{
					.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
				};
				VkFence fenceOneTime{};

				validationHelpers.validateResult(vkCreateFence( windowConfiguration.device, &fenceOneTimeCreateInfo, nullptr, &fenceOneTime), "Failed to Create one time Fence");

				VkCommandBuffer commandBufferOneTime{};
				VkCommandBufferAllocateInfo commandBufferOneTimeAllocationInfo{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
					.commandPool = windowConfiguration.commandPool,
					.commandBufferCount = 1
				};

				validationHelpers.validateResult(vkAllocateCommandBuffers( windowConfiguration.device, &commandBufferOneTimeAllocationInfo, &commandBufferOneTime), "Failed to Allocate one time command buffer");

				VkCommandBufferBeginInfo commandBufferOneTimeBeginInfo{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
				};

				validationHelpers.validateResult(vkBeginCommandBuffer(commandBufferOneTime, &commandBufferOneTimeBeginInfo), "Failed To Begin Command Buffer");

				VkImageMemoryBarrier2 barrierTextureImage{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
					.srcAccessMask = VK_ACCESS_2_NONE,
					.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image = textureList[textureSlot].image,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.levelCount = ktxTexture->numLevels,
						.layerCount = 1
					}
				};

				VkDependencyInfo barrierTextureInfo{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.imageMemoryBarrierCount = 1,
					.pImageMemoryBarriers = &barrierTextureImage
				};

				vkCmdPipelineBarrier2(commandBufferOneTime, &barrierTextureInfo);

				std::vector< VkBufferImageCopy > copyRegions{};
				for (uint32_t j = 0; j < ktxTexture->numLevels; j++) {
					ktx_size_t mipOffset{ 0 };
					KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
					copyRegions.push_back({
						.bufferOffset = mipOffset,
						.imageSubresource {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = (uint32_t)j,
							.layerCount = 1
						},
						.imageExtent {
							.width = ktxTexture->baseWidth >> j,
							.height = ktxTexture->baseHeight >> j,
							.depth = 1
						}
					});
				}
				vkCmdCopyBufferToImage(commandBufferOneTime, imageSourceBuffer, textureList[textureSlot].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

				VkImageMemoryBarrier2 barrierTextureRead{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
					.image = textureList[textureSlot].image,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.levelCount = ktxTexture->numLevels,
						.layerCount = 1
					}
				};

				barrierTextureInfo.pImageMemoryBarriers = &barrierTextureRead;

				vkCmdPipelineBarrier2(commandBufferOneTime, &barrierTextureInfo);

				validationHelpers.validateResult(vkEndCommandBuffer(commandBufferOneTime), "Failed to end Command buffer");

				VkSubmitInfo oneTimeSubmitInfo{
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					.commandBufferCount = 1,
					.pCommandBuffers = &commandBufferOneTime
				};

				validationHelpers.validateResult(vkQueueSubmit( windowConfiguration.queue, 1, &oneTimeSubmitInfo, fenceOneTime), "Failed to submit to queue");
				validationHelpers.validateResult(vkWaitForFences( windowConfiguration.device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX), "Failed to Wait for Fences");

				VkSamplerCreateInfo samplerCreateInfo{
					.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
					.magFilter = VK_FILTER_LINEAR,
					.minFilter = VK_FILTER_LINEAR,
					.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
					.anisotropyEnable = VK_TRUE,
					.maxAnisotropy = 8.0f,
					.maxLod = (float)ktxTexture->numLevels
				};

				validationHelpers.validateResult(vkCreateSampler( windowConfiguration.device, &samplerCreateInfo, nullptr, &textureList[textureSlot].sampler), "Failed to create sampler");

				ktxTexture_Destroy(ktxTexture);
				textureDescriptors.push_back({
					.sampler = textureList[textureSlot].sampler,
					.imageView = textureList[textureSlot].view,
					.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
				});

				textureSlot = textureSlot + 1;
			}

			VkDescriptorPoolSize poolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = static_cast<uint32_t>(textureList.size())
			};

			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = 1,
				.poolSizeCount = 1,
				.pPoolSizes = &poolSize
			};

			validationHelpers.validateResult(vkCreateDescriptorPool( windowConfiguration.device, &descriptorPoolCreateInfo, nullptr, &descriptorPool), "Failed to Create Descriptor Pool");

			VkDescriptorBindingFlags descriptorVariableFlag{ VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT };
			VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorBindingFlags{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
				.bindingCount = 1,
				.pBindingFlags = &descriptorVariableFlag
			};

			VkDescriptorSetLayoutBinding descriptorLayoutBindingTexture{
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = static_cast<uint32_t>(textureList.size()),
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
			};

			VkDescriptorSetLayoutCreateInfo descriptorLayoutTextureCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.pNext = &descriptorBindingFlags,
				.bindingCount = 1,
				.pBindings = &descriptorLayoutBindingTexture
			};

			validationHelpers.validateResult(vkCreateDescriptorSetLayout( windowConfiguration.device, &descriptorLayoutTextureCreateInfo, nullptr, &descriptorSetLayoutTex), "Failed to Create a Descriptor Set Layout");

			uint32_t variableDescriptorCount{ static_cast<uint32_t>(textureList.size()) };
			VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
				.descriptorSetCount = 1,
				.pDescriptorCounts = &variableDescriptorCount
			};

			VkDescriptorSetAllocateInfo textureDescriptorSetAllocateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = &variableDescriptorCountAllocateInfo,
				.descriptorPool = descriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &descriptorSetLayoutTex
			};

			validationHelpers.validateResult(vkAllocateDescriptorSets( windowConfiguration.device, &textureDescriptorSetAllocateInfo, &descriptorSetTex), "Failed to Allocate Descriptor Set");

			VkWriteDescriptorSet writeDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptorSetTex,
				.dstBinding = 0,
				.descriptorCount = static_cast<uint32_t>(textureList.size()),
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = textureDescriptors.data()
			};

			vkUpdateDescriptorSets( windowConfiguration.device, 1, &writeDescriptorSet, 0, nullptr);
		}

		void uploadModel(std::vector< Mesh >& models) {
			meshes = models;

			int textureCount = 0;
			for (uint16_t index = 0; index < models.size(); index++) {
				Mesh* mesh = &meshes[index];
				mesh->compute();
				if (mesh->textureName != "") textureCount = textureCount + 1;

			}

			textureList.resize(textureCount);
			loadTextures();

			for (uint16_t index = 0; index < models.size(); index++) {
				Mesh* mesh = &meshes[index];

				VkBufferCreateInfo bufferCreateInfo{
					.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
					.size = mesh->getIndicesBufferSize(),
					.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
				};

				VmaAllocationCreateInfo allocationCreateInfo{
					.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
					.usage = VMA_MEMORY_USAGE_AUTO
				};

				validationHelpers.validateResult(vmaCreateBuffer( windowConfiguration.allocator, &bufferCreateInfo, &allocationCreateInfo, &mesh->indicesBuffer, &mesh->indicesBufferAllocation, &mesh->indicesBufferAllocationInfo));
				memcpy(mesh->indicesBufferAllocationInfo.pMappedData, mesh->_indices.data(), mesh->getIndicesBufferSize());
				auto instancePos = glm::vec3(0.0f, 0.0f, 0.0f);
				uniformData.meshData.push_back(UniformData::Mesh{
					.segments = mesh->userData["segments"],
					.vertices = mesh->userData["vertices"],
					.textureIndex = mesh->textureIndex,
					.patchyGrass = mesh -> patchyGrass,
					.model = glm::translate(glm::mat4(1.0f), instancePos),
					.BASE_COLOR = mesh->BASE_COLOR,
					.TIP_COLOR = mesh->TIP_COLOR,
					.PATCH_BASE_COLOR = mesh -> PATCH_BASE_COLOR,
					.PATCH_TIP_COLOR = mesh -> PATCH_TIP_COLOR,
					.clump = mesh -> clump
				});
			}

			for (uint32_t index = 0; index < framesConfiguration.maxFramesInFlight; index++) {
				VkBufferCreateInfo uniformBufferCreateInfo{
					.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
					.size = sizeof(UniformData::Frame),
					.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};

				VmaAllocationCreateInfo uniformBufferAllocationCreateInfo{
					.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
					.usage = VMA_MEMORY_USAGE_AUTO
				};

				validationHelpers.validateResult(vmaCreateBuffer(windowConfiguration.allocator, &uniformBufferCreateInfo, &uniformBufferAllocationCreateInfo, &framesConfiguration.uniformDataBuffers[index].buffer, &framesConfiguration.uniformDataBuffers[index].allocation, &framesConfiguration.uniformDataBuffers[index].allocationInfo));

				VkBufferDeviceAddressInfo uniformBDAInfo{
					.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
					.buffer = framesConfiguration.uniformDataBuffers[index].buffer
				};

				framesConfiguration.uniformDataBuffers[index].deviceAddress = vkGetBufferDeviceAddress(windowConfiguration.device, &uniformBDAInfo);
			}


			VkBufferCreateInfo uniformBufferCreateInfo{
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = sizeof(UniformData::Mesh) * meshes.size(),
				.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			};

			VmaAllocationCreateInfo uniformBufferAllocationCreateInfo{
				.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO
			};

			validationHelpers.validateResult(vmaCreateBuffer(windowConfiguration.allocator, &uniformBufferCreateInfo, &uniformBufferAllocationCreateInfo, &uniformData.meshHandle.buffer, &uniformData.meshHandle.allocation, &uniformData.meshHandle.allocationInfo));

			VkBufferDeviceAddressInfo uniformBDAInfo{
				.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
				.buffer = uniformData.meshHandle.buffer
			};

			uniformData.meshHandle.deviceAddress = vkGetBufferDeviceAddress(windowConfiguration.device, &uniformBDAInfo);

			memcpy(uniformData.meshHandle.allocationInfo.pMappedData, uniformData.meshData.data(), uniformData.meshData.size() * sizeof(UniformData::Mesh));
		}

		struct PushConstants {
			VkDeviceAddress frameBDA;
			VkDeviceAddress meshBDA;
			uint32_t meshIndex;
			int instanceCount;
		};

		void setupPipelineSky() {


			std::vector<char> initialData;

			std::ifstream file("pipelineSky.cache", std::ios::binary | std::ios::ate);
			if (file.is_open()) {
				size_t size = file.tellg();
				initialData.resize(size);
				file.seekg(0);
				file.read(initialData.data(), size);
				file.close();
			}

			VkPipelineCacheCreateInfo cacheInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
				.initialDataSize = initialData.size(),
				.pInitialData = initialData.empty() ? nullptr : initialData.data()
			};

			vkCreatePipelineCache(windowConfiguration.device, &cacheInfo, nullptr, &pipelineCache);

			VkPushConstantRange pushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.size = sizeof(PushConstants)
			};

			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &descriptorSetLayoutTex,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pushConstantRange
			};

			validationHelpers.validateResult(vkCreatePipelineLayout(windowConfiguration.device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout), "Failed to Create Pipeline Layout");

			VkPipelineVertexInputStateCreateInfo vertexInputState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
				.vertexBindingDescriptionCount = 0,
				.pVertexBindingDescriptions = nullptr,
				.vertexAttributeDescriptionCount = 0,
				.pVertexAttributeDescriptions = nullptr
			};

			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			};

			std::vector< VkPipelineShaderStageCreateInfo > shaderStages{
				{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_VERTEX_BIT,
					.module = skyModule,
					.pName = "vs_sky"
				},
				{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
					.module = skyModule,
					.pName = "ps_sky"
				}
			};

			std::vector< VkDynamicState > dynamicStates{
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			};

			VkPipelineDynamicStateCreateInfo dynamicState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
				.dynamicStateCount = 2,
				.pDynamicStates = dynamicStates.data()
			};

			VkPipelineViewportStateCreateInfo viewportState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
				.viewportCount = 1,
				.scissorCount = 1
			};

			VkPipelineDepthStencilStateCreateInfo depthStencilState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
				.depthTestEnable = VK_TRUE,
				.depthWriteEnable = VK_TRUE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
			};

			VkPipelineRenderingCreateInfo renderingCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &deviceInterface.logical.swapchainConfiguration.imageFormat,
				.depthAttachmentFormat = depthAttachmentConfiguration.format
			};

			VkPipelineColorBlendAttachmentState blendAttachment{
				.colorWriteMask = 0xF
			};

			VkPipelineColorBlendStateCreateInfo colorBlendState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
				.attachmentCount = 1,
				.pAttachments = &blendAttachment
			};

			VkPipelineRasterizationStateCreateInfo rasterizationState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			};

			VkPipelineMultisampleStateCreateInfo multisampleState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
				.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
			};

			VkGraphicsPipelineCreateInfo pipelineCreateInfo{
				.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
				.pNext = &renderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages.data(),
				.pVertexInputState = &vertexInputState,
				.pInputAssemblyState = &inputAssemblyState,
				.pViewportState = &viewportState,
				.pRasterizationState = &rasterizationState,
				.pMultisampleState = &multisampleState,
				.pDepthStencilState = &depthStencilState,
				.pColorBlendState = &colorBlendState,
				.pDynamicState = &dynamicState,
				.layout = pipelineLayout
			};

			validationHelpers.validateResult(vkCreateGraphicsPipelines(windowConfiguration.device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelineSky), "Failed to Create Graphics Pipeline");

			std::cout << "Pipeline handle: " << pipeline << std::endl;

			size_t cacheSize = 0;
			vkGetPipelineCacheData(windowConfiguration.device, pipelineCache, &cacheSize, nullptr);

			std::vector<char> cacheData(cacheSize);
			vkGetPipelineCacheData(windowConfiguration.device, pipelineCache, &cacheSize, cacheData.data());

			std::ofstream file2("pipelineSky.cache", std::ios::binary);
			file2.write(cacheData.data(), cacheSize);
		}

		void setupPipeline() {


			std::vector<char> initialData;

			std::ifstream file("pipeline.cache", std::ios::binary | std::ios::ate);
			if (file.is_open()) {
				size_t size = file.tellg();
				initialData.resize(size);
				file.seekg(0);
				file.read(initialData.data(), size);
				file.close();
			}

			VkPipelineCacheCreateInfo cacheInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
				.initialDataSize = initialData.size(),
				.pInitialData = initialData.empty() ? nullptr : initialData.data()
			};

			vkCreatePipelineCache( windowConfiguration.device, &cacheInfo, nullptr, &pipelineCache);

			VkPushConstantRange pushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.size = sizeof(PushConstants)
			};

			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &descriptorSetLayoutTex,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pushConstantRange
			};

			validationHelpers.validateResult(vkCreatePipelineLayout(windowConfiguration.device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout), "Failed to Create Pipeline Layout");

			VkPipelineVertexInputStateCreateInfo vertexInputState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
				.vertexBindingDescriptionCount = 0,
				.pVertexBindingDescriptions = nullptr,
				.vertexAttributeDescriptionCount = 0,
				.pVertexAttributeDescriptions = nullptr
			};

			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
				.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
			};

			std::vector< VkPipelineShaderStageCreateInfo > shaderStages{
				{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_VERTEX_BIT,
					.module = shaderModule,
					.pName = "main"
				},
				{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
					.module = shaderModule,
					.pName = "main"
				}
			};

			std::vector< VkDynamicState > dynamicStates{
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			};

			VkPipelineDynamicStateCreateInfo dynamicState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
				.dynamicStateCount = 2,
				.pDynamicStates = dynamicStates.data()
			};

			VkPipelineViewportStateCreateInfo viewportState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
				.viewportCount = 1,
				.scissorCount = 1
			};

			VkPipelineDepthStencilStateCreateInfo depthStencilState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
				.depthTestEnable = VK_TRUE,
				.depthWriteEnable = VK_TRUE,
				.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
			};

			VkPipelineRenderingCreateInfo renderingCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &deviceInterface.logical.swapchainConfiguration.imageFormat,
				.depthAttachmentFormat = depthAttachmentConfiguration.format
			};

			VkPipelineColorBlendAttachmentState blendAttachment{
				.colorWriteMask = 0xF
			};

			VkPipelineColorBlendStateCreateInfo colorBlendState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
				.attachmentCount = 1,
				.pAttachments = &blendAttachment
			};

			VkPipelineRasterizationStateCreateInfo rasterizationState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
				.cullMode = VK_CULL_MODE_BACK_BIT,
				.frontFace = VK_FRONT_FACE_CLOCKWISE,
				.lineWidth = 1.0f
			};

			VkPipelineMultisampleStateCreateInfo multisampleState{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
				.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
			};

			VkGraphicsPipelineCreateInfo pipelineCreateInfo{
				.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
				.pNext = &renderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages.data(),
				.pVertexInputState = &vertexInputState,
				.pInputAssemblyState = &inputAssemblyState,
				.pViewportState = &viewportState,
				.pRasterizationState = &rasterizationState,
				.pMultisampleState = &multisampleState,
				.pDepthStencilState = &depthStencilState,
				.pColorBlendState = &colorBlendState,
				.pDynamicState = &dynamicState,
				.layout = pipelineLayout
			};

			validationHelpers.validateResult(vkCreateGraphicsPipelines(windowConfiguration.device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline), "Failed to Create Graphics Pipeline");
	
			std::cout << "Pipeline handle: " << pipeline << std::endl;

			size_t cacheSize = 0;
			vkGetPipelineCacheData(windowConfiguration.device, pipelineCache, &cacheSize, nullptr);

			std::vector<char> cacheData(cacheSize);
			vkGetPipelineCacheData(windowConfiguration.device, pipelineCache, &cacheSize, cacheData.data());

			// write to file
			std::ofstream file2("pipeline.cache", std::ios::binary);
			file2.write(cacheData.data(), cacheSize);
		}

		void animate() {
			bool quit{ false };
			Uint64 start = SDL_GetPerformanceCounter();
			Uint64 freq = SDL_GetPerformanceFrequency();

			while (!quit) {
				SDL_Event event;

				while (SDL_PollEvent(&event)) {
					ImGui_ImplSDL3_ProcessEvent(&event);
					if (event.type == SDL_EVENT_QUIT) {
						quit = true;
					}

					if (event.type == SDL_EVENT_WINDOW_RESIZED) {
						deviceInterface.logical.swapchainConfiguration.updateSwapchain = true;
					}
				}

				ImGui_ImplVulkan_NewFrame();
				ImGui_ImplSDL3_NewFrame();
				ImGui::NewFrame();
				ImGui::ShowDemoWindow();

				validationHelpers.validateResult(vkWaitForFences( windowConfiguration.device, 1, &framesConfiguration.fences[framesConfiguration.frameIndex], true, UINT64_MAX));
				validationHelpers.validateResult(vkResetFences( windowConfiguration.device, 1, &framesConfiguration.fences[framesConfiguration.frameIndex]));

				bool verdict = validationHelpers.validateSwapchain(vkAcquireNextImageKHR( windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.swapchain, UINT64_MAX, framesConfiguration.presentSemaphores[framesConfiguration.frameIndex], VK_NULL_HANDLE, &deviceInterface.logical.swapchainConfiguration.imageIndex));
				deviceInterface.logical.swapchainConfiguration.updateSwapchain = verdict;

				auto commandBuffer = framesConfiguration.commandBuffers[framesConfiguration.frameIndex];
				validationHelpers.validateResult(vkResetCommandBuffer(commandBuffer, 0));

				VkCommandBufferBeginInfo commandBufferBeginInfo{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
				};

				validationHelpers.validateResult(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

				VkRenderingAttachmentInfo colorAttachmentInfo{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = deviceInterface.logical.swapchainConfiguration.imageViews[deviceInterface.logical.swapchainConfiguration.imageIndex],
					.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.clearValue{
						windowConfiguration.clearColor
					}
				};

				VkRenderingAttachmentInfo depthAttachmentInfo{
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = depthAttachmentConfiguration.imageView,
					.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.clearValue{
						.depthStencil = { 1.0f, 0 }
					}
				};

				VkRenderingInfo renderingInfo{
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea{
						.extent{
							.width = static_cast<uint32_t>(windowConfiguration.windowSize.x),
							.height = static_cast<uint32_t>(windowConfiguration.windowSize.y)
						},
					},
					.layerCount = 1,
					.colorAttachmentCount = 1,
					.pColorAttachments = &colorAttachmentInfo,
					.pDepthAttachment = &depthAttachmentInfo
				};

				std::array< VkImageMemoryBarrier2, 2 > outputBarriers{
					VkImageMemoryBarrier2{
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						.srcStageMask = VK_PIPELINE_STAGE_NONE,
						.srcAccessMask = 0,
						.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
						.image = deviceInterface.logical.swapchainConfiguration.images[deviceInterface.logical.swapchainConfiguration.imageIndex],
						.subresourceRange {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.levelCount = 1,
							.layerCount = 1
						}
					},
					VkImageMemoryBarrier2{
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
						.image = depthAttachmentConfiguration.image,
						.subresourceRange {
							.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
							.levelCount = 1,
							.layerCount = 1
						}
					}
				};

				VkDependencyInfo barrierDependencyInfo{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.imageMemoryBarrierCount = 2,
					.pImageMemoryBarriers = outputBarriers.data()
				};

				vkCmdPipelineBarrier2(commandBuffer, &barrierDependencyInfo);

				VkViewport viewport{
					.width = static_cast<float>(windowConfiguration.windowSize.x),
					.height = static_cast<float>(windowConfiguration.windowSize.y),
					.minDepth = 0.0f,
					.maxDepth = 1.0f
				};

				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
				VkRect2D scissor{
					.extent {
						.width = static_cast<uint32_t> (windowConfiguration.windowSize.x),
						.height = static_cast<uint32_t>(windowConfiguration.windowSize.y)
					}
				};
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				uniformData.frame.projection = glm::perspective(glm::radians(45.0f), (float)windowConfiguration.windowSize.x / (float)windowConfiguration.windowSize.y, 0.1f, 200.0f);
				uniformData.frame.projection[1][1] *= -1;
				uniformData.frame.view = glm::lookAt(camera.position, camera.lookAt, camera.cameraUp);
				uniformData.frame.windSpeed = 4.0;
				uniformData.frame.cameraPosition = glm::vec4(camera.position, 0.0f);

				Uint64 now = SDL_GetPerformanceCounter();

				float time = (float)((double)(now - start) / (double)freq);


				//std::cout << time << std::endl;

				uniformData.frame.time = time;

				memcpy(framesConfiguration.uniformDataBuffers[framesConfiguration.frameIndex].allocationInfo.pMappedData, &uniformData.frame, sizeof(UniformData::Frame));

				vkCmdBeginRendering(commandBuffer, &renderingInfo);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineSky);
				vkCmdDraw(commandBuffer, 3, 1, 0, 0);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				int _instanceCount = 0;
				for (uint32_t index = 0; index < meshes.size(); index++)
				{
					PushConstants pc{};
					pc.frameBDA = framesConfiguration.uniformDataBuffers[framesConfiguration.frameIndex].deviceAddress;
					pc.meshBDA = uniformData.meshHandle.deviceAddress;
					pc.meshIndex = index; // index of the mesh in the buffer
					pc.instanceCount = _instanceCount + meshes[index].userData["count"];

					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetTex, 0, nullptr);
					vkCmdBindIndexBuffer(commandBuffer, meshes[index].indicesBuffer, 0, VK_INDEX_TYPE_UINT16);
					vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
					vkCmdDrawIndexed(commandBuffer, meshes[index]._indices.size(), meshes[index].userData["count"], 0, 0, 0);
				
					_instanceCount = _instanceCount + meshes[index].userData["count"];
				}

				vkCmdEndRendering(commandBuffer);

				ImGui::Render();
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

				vkEndCommandBuffer(commandBuffer);

				VkImageMemoryBarrier2 barrierPresent{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.dstAccessMask = 0,
					.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					.image = deviceInterface.logical.swapchainConfiguration.images[deviceInterface.logical.swapchainConfiguration.imageIndex],
					.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
				};
				VkDependencyInfo barrierPresentDependencyInfo{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.imageMemoryBarrierCount = 1,
					.pImageMemoryBarriers = &barrierPresent
				};
				vkCmdPipelineBarrier2(commandBuffer, &barrierPresentDependencyInfo);



				VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				VkSubmitInfo submitInfo{
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					.waitSemaphoreCount = 1,
					.pWaitSemaphores = &framesConfiguration.presentSemaphores[framesConfiguration.frameIndex],
					.pWaitDstStageMask = &waitStages,
					.commandBufferCount = 1,
					.pCommandBuffers = &commandBuffer,
					.signalSemaphoreCount = 1,
					.pSignalSemaphores = &deviceInterface.logical.swapchainConfiguration.renderSemaphores[deviceInterface.logical.swapchainConfiguration.imageIndex],
				};
				validationHelpers.validateResult(vkQueueSubmit( windowConfiguration.queue, 1, &submitInfo, framesConfiguration.fences[framesConfiguration.frameIndex]), "Failed to Submit Queue");

				framesConfiguration.frameIndex = (framesConfiguration.frameIndex + 1) % framesConfiguration.maxFramesInFlight;

				VkPresentInfoKHR presentInfo{
					.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
					.waitSemaphoreCount = 1,
					.pWaitSemaphores = &deviceInterface.logical.swapchainConfiguration.renderSemaphores[deviceInterface.logical.swapchainConfiguration.imageIndex],
					.swapchainCount = 1,
					.pSwapchains = &deviceInterface.logical.swapchainConfiguration.swapchain,
					.pImageIndices = &deviceInterface.logical.swapchainConfiguration.imageIndex
				};
				validationHelpers.validateResult(vkQueuePresentKHR( windowConfiguration.queue, &presentInfo), "Failed To Present Queue");

				if (deviceInterface.logical.swapchainConfiguration.updateSwapchain) {
					deviceInterface.logical.swapchainConfiguration.updateSwapchain = false;
					vkDeviceWaitIdle( windowConfiguration.device );
					validationHelpers.validateResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR( deviceInterface.physical.devices[ deviceInterface.physical.index ], windowConfiguration.surface, &windowConfiguration.surfaceCapabilities), "Failed to Get Surface Capabilities");
					deviceInterface.logical.swapchainConfiguration.swapchainCreateInfo.oldSwapchain = deviceInterface.logical.swapchainConfiguration.swapchain;
					deviceInterface.logical.swapchainConfiguration.swapchainCreateInfo.imageExtent = { .width = static_cast<uint32_t>(windowConfiguration.windowSize.x), .height = static_cast<uint32_t>(windowConfiguration.windowSize.y) };
					validationHelpers.validateResult(vkCreateSwapchainKHR(windowConfiguration.device, &deviceInterface.logical.swapchainConfiguration.swapchainCreateInfo, nullptr, &deviceInterface.logical.swapchainConfiguration.swapchain), "Failed to Create Swap chain");
					for (uint32_t i = 0; i < deviceInterface.logical.swapchainConfiguration.imageCount; i++) {
						vkDestroyImageView(windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.imageViews[i], nullptr);
					}
					validationHelpers.validateResult(vkGetSwapchainImagesKHR(windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.swapchain, &deviceInterface.logical.swapchainConfiguration.imageCount, nullptr), "Failed To Create Swap Chain Images");
					deviceInterface.logical.swapchainConfiguration.images.resize(deviceInterface.logical.swapchainConfiguration.imageCount);
					validationHelpers.validateResult(vkGetSwapchainImagesKHR(windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.swapchain, &deviceInterface.logical.swapchainConfiguration.imageCount, deviceInterface.logical.swapchainConfiguration.images.data()), "Failed To Get Swap Chain Images");
					deviceInterface.logical.swapchainConfiguration.imageViews.resize(deviceInterface.logical.swapchainConfiguration.imageCount);
					for (uint32_t i = 0; i < deviceInterface.logical.swapchainConfiguration.imageCount; i++) {
						VkImageViewCreateInfo viewCI{
							.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							.image = deviceInterface.logical.swapchainConfiguration.images[i],
							.viewType = VK_IMAGE_VIEW_TYPE_2D,
							.format = deviceInterface.logical.swapchainConfiguration.imageFormat,
							.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
						};
						validationHelpers.validateResult(vkCreateImageView(windowConfiguration.device, &viewCI, nullptr, &deviceInterface.logical.swapchainConfiguration.imageViews[i]), "Failed To Create Image View");
					}
					vkDestroySwapchainKHR(windowConfiguration.device, deviceInterface.logical.swapchainConfiguration.swapchainCreateInfo.oldSwapchain, nullptr);
					vmaDestroyImage( windowConfiguration.allocator, depthAttachmentConfiguration.image, depthAttachmentConfiguration.allocation);
					vkDestroyImageView(windowConfiguration.device, depthAttachmentConfiguration.imageView, nullptr);
					depthAttachmentConfiguration.imageCreateInfo.extent = { .width = static_cast<uint32_t>(windowConfiguration.windowSize.x), .height = static_cast<uint32_t>(windowConfiguration.windowSize.y), .depth = 1 };
					VmaAllocationCreateInfo allocCI{
						.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
						.usage = VMA_MEMORY_USAGE_AUTO
					};
					validationHelpers.validateResult(vmaCreateImage(windowConfiguration.allocator, &depthAttachmentConfiguration.imageCreateInfo, &allocCI, &depthAttachmentConfiguration.image, &depthAttachmentConfiguration.allocation, nullptr), "Faile to VMA create image");
					VkImageViewCreateInfo viewCI{
						.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						.image = depthAttachmentConfiguration.image,
						.viewType = VK_IMAGE_VIEW_TYPE_2D,
						.format = depthAttachmentConfiguration.format,
						.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
					};
					validationHelpers.validateResult(vkCreateImageView(windowConfiguration.device, &viewCI, nullptr, &depthAttachmentConfiguration.imageView), "Failed To Create Image View");
				}
			}

			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplSDL3_Shutdown();
			ImGui::DestroyContext();
		}

		void adjustCamera() {
			camera.position = glm::vec3(uniformData.frame.fieldSize / 2.0f, 4, uniformData.frame.fieldSize + 5.0);
			camera.lookAt = glm::vec3(uniformData.frame.fieldSize / 2.0f, 0, 0);
		}

		void setup() {
			setupLibraries();
			setupInstance();
			pickPhysicalDeviceAndQueue();
			setupLogicalDevice();
			setupVMA();
			setupUIUI();
			setupSwapChain();
			setupDepthAttachment();
			setupSynchronization();
			setupCommandBuffers();
			loadShaders();
			adjustCamera();
		}
	
	public:
		struct WindowConfiguration {
			VkApplicationInfo applicationInfo{
				.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
				.pApplicationName = "FoliageLab",
				.apiVersion = VK_API_VERSION_1_4
			};

			VkInstance vkInstance;

			VkDevice device{};
			VkQueue queue{};
			VmaAllocator allocator{};

			SDL_Window *window{};
			std::string windowName{ "Foliage Lab" };
			int windowWidth{ 400u };
			int windowHeight{ 400u };
			SDL_WindowFlags windowFlags{
				SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN
			};

			VkSurfaceKHR surface{};
			VkSurfaceCapabilitiesKHR surfaceCapabilities{};

			glm::ivec2 windowSize{};

			VkClearColorValue clearColor{ 0.53f, 0.81f, 0.92f, 1.0f };
			VkCommandPool commandPool{};
		};

		Slang::ComPtr< slang::IGlobalSession > slangGlobalSession;
		VkShaderModule shaderModule{};
		VkShaderModule skyModule{};


		VkPipelineLayout pipelineLayout{};
		VkPipeline pipeline{};
		VkPipeline pipelineSky{};

		VkPipelineCache pipelineCache{};

		struct GrassStrand {};


		std::vector< Mesh > meshes{};

		std::vector<VkDescriptorImageInfo> textureDescriptors{};
		VkDescriptorSetLayout descriptorSetLayoutTex{ VK_NULL_HANDLE };
		VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
		VkDescriptorSet descriptorSetTex{ VK_NULL_HANDLE };

		struct Texture {
			VmaAllocation allocation{ VK_NULL_HANDLE };
			VkImage image{ VK_NULL_HANDLE };
			VkImageView view{ VK_NULL_HANDLE };
			VkSampler sampler{ VK_NULL_HANDLE };
		};
		std::vector< Texture > textureList;

		VkDescriptorPool imguiPool;
		struct ExtensionsInterface {
			struct Instance {
				uint32_t count{};
				char const* const* extensions{ nullptr };
			} instance;

			struct Device {
				const std::vector< const char* > extensions{
					VK_KHR_SWAPCHAIN_EXTENSION_NAME

				};
			} device;
		};

		struct DeviceInterface {
			struct Physical {
				uint32_t count{};
				uint32_t index{};
				std::vector< VkPhysicalDevice > devices{};
				std::vector< VkPhysicalDeviceProperties2 > properties{};
			} physical; 

			struct Queue {
				const float priorities{ 1.0f };
				std::vector< std::vector< VkQueueFamilyProperties2 > > properties{};
				uint32_t index{};
			} queue;

			struct Logical {
				struct SwapchainConfiguration {
					VkSwapchainCreateInfoKHR swapchainCreateInfo{};

					const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
					VkSwapchainKHR swapchain{};
					uint32_t imageCount{ 0 };
					std::vector< VkImage > images{};
					std::vector< VkImageView > imageViews{};

					std::vector< VkSemaphore > renderSemaphores;

					uint32_t imageIndex{ 0 };

					bool updateSwapchain{ false };
				} swapchainConfiguration;
			} logical;
		};

		struct DepthAttachmentConfiguration {

			VkImageCreateInfo imageCreateInfo{};

			std::vector< VkFormat > formatList{
				VK_FORMAT_D32_SFLOAT_S8_UINT,
				VK_FORMAT_D24_UNORM_S8_UINT
			};

			VkFormat format{ VK_FORMAT_UNDEFINED };
			VkFormatProperties2 formatProperties{
				.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2
			};

			VkImage image{};
			VkImageView imageView{};

			VmaAllocation allocation{};

		} depthAttachmentConfiguration;

		public:
			struct Model {
				std::vector< uint16_t > indices{};

				VkDeviceSize indexBufferSize{};

				VkBuffer buffer{};
				VmaAllocation allocation{};
				VmaAllocationInfo allocationInfo{};
			} Model;

	public:

		struct UniformData {
			struct Frame {
				glm::mat4 projection{};
				glm::mat4 view{};

				float windSpeed{};
				float time{};
				float fieldSize{ 100.0f };
				glm::vec4 cameraPosition{};
			} frame{};

			struct FrameHandle {
				VkBuffer buffer{ VK_NULL_HANDLE };

				VmaAllocation allocation{ VK_NULL_HANDLE };
				VmaAllocationInfo allocationInfo{};

				VkDeviceAddress deviceAddress{};
				void* mapped{ nullptr };
			} frameHandle{};

			struct Mesh {
				float segments;
				float vertices;
				int textureIndex;
				int patchyGrass; // Changed to int for 4-byte alignment

				glm::mat4 model;

				glm::vec3 BASE_COLOR; float pad1;
				glm::vec3 TIP_COLOR; float pad2;
				glm::vec3 PATCH_BASE_COLOR; float pad3;
				glm::vec3 PATCH_TIP_COLOR; float pad4;

				int clump;       
				float pad5, pad6, pad7; 
			};

			struct MeshHandle {
				VkBuffer buffer{ VK_NULL_HANDLE };

				VmaAllocation allocation{ VK_NULL_HANDLE };
				VmaAllocationInfo allocationInfo{};

				VkDeviceAddress deviceAddress{};
				void* mapped{ nullptr };
			} meshHandle{};

			std::vector< Mesh > meshData{};
		} uniformData;

		struct FramesConfiguration {
			static constexpr uint32_t maxFramesInFlight{ 2 };

			std::array< UniformData::FrameHandle, maxFramesInFlight > uniformDataBuffers{};
			std::array< VkCommandBuffer, maxFramesInFlight > commandBuffers{};

			std::array< VkFence, maxFramesInFlight > fences;
			std::array< VkSemaphore, maxFramesInFlight > presentSemaphores;

			int frameIndex{ 0 };
		} framesConfiguration;

	public:
		struct Camera {
			glm::vec3 position{ 12.5f, 4, 25.0f };
			glm::vec3 cameraUp{ 0, 1, 0 };
			glm::vec3 lookAt{ 12.5f, 0, 0.0f };
		} camera;

	public:
		ValidationHelpers validationHelpers;
		WindowConfiguration windowConfiguration;
		ExtensionsInterface extensionsInterface;
		DeviceInterface deviceInterface;
};

class GrassFieldSimulation {
public:
	void start() {
		generateModels();

		simulation.setup();
		simulation.uploadModel(models);
		simulation.setupPipeline();
		simulation.setupPipelineSky();
		simulation.setupUI();
		simulation.animate();
	}

	struct GrassInformation {
		glm::fvec3 BASE_COLOR{ 0.02, 0.12, 0.01 };
		glm::fvec3 TIP_COLOR{ 0.18, 0.32, 0.12 };
		glm::fvec3 PATCH_BASE_COLOR{ 0.12, 0.08, 0.01 };
		glm::fvec3 PATCH_TIP_COLOR{ 0.38, 0.32, 0.05 };
		bool patchyGrass{ true };
		bool clump{ false };
		std::string textureName{""};
	};

	Mesh generateGrassStrand(int layerIndex, std::vector< GrassInformation > info ) {
		int grassStrandCount = 200000;
		int totalCount = fmax( grassStrandCount, weights.size() );

		float totalWeight = 0;
		for (int i = 0; i < weights.size(); i++) {
			totalWeight = totalWeight + weights[i];
		}

		Mesh mesh{};
		float segments = 4; //LOD
		float vertices = (segments + 1) * 2;

		mesh.userData["segments"] = segments;
		mesh.userData["vertices"] = vertices;
		mesh.userData["count"] = int(( weights[layerIndex] / totalWeight ) * totalCount) + 1; //LOD
		mesh.BASE_COLOR = info[layerIndex].BASE_COLOR;
		mesh.TIP_COLOR = info[layerIndex].TIP_COLOR;
		mesh.textureName = info[layerIndex].textureName;
		mesh.PATCH_BASE_COLOR = info[layerIndex].PATCH_BASE_COLOR;
		mesh.PATCH_TIP_COLOR = info[layerIndex].PATCH_TIP_COLOR;
		mesh.patchyGrass = info[layerIndex].patchyGrass;
		mesh.clump = info[layerIndex].clump;

		std::cout << std::endl << mesh.userData["count"] << std::endl;

		std::vector< uint16_t > indices{};
		for (uint32_t index = 0; index < segments; index++) {
			const int frontFaceIndexID = index * 2;

			indices.push_back(static_cast<uint16_t>(frontFaceIndexID + 0));
			indices.push_back(static_cast<uint16_t>(frontFaceIndexID + 1));
			indices.push_back(static_cast<uint16_t>(frontFaceIndexID + 2));

			indices.push_back(static_cast<uint16_t>(frontFaceIndexID + 2));
			indices.push_back(static_cast<uint16_t>(frontFaceIndexID + 1));
			indices.push_back(static_cast<uint16_t>(frontFaceIndexID + 3));

			const int backFaceIndexID = vertices + frontFaceIndexID;
			indices.push_back(static_cast<uint16_t>(backFaceIndexID + 2));
			indices.push_back(static_cast<uint16_t>(backFaceIndexID + 1));
			indices.push_back(static_cast<uint16_t>(backFaceIndexID + 0));

			indices.push_back(static_cast<uint16_t>(backFaceIndexID + 3));
			indices.push_back(static_cast<uint16_t>(backFaceIndexID + 1));
			indices.push_back(static_cast<uint16_t>(backFaceIndexID + 2));
		}

		mesh.setIndices(indices);

		return mesh;
	}

	void generateModels() {

		std::vector< std::string > layers{
			"Layer_1",
			"Layer_2"
		};

		weights.insert(weights.end(), {
			20, 
			1
		});

		std::vector< GrassInformation > grassInformation{
			{
				
			},
			{
				.clump = true,
				.textureName = "flower_2.ktx",
			}
		};

		for (uint16_t index = 0; index < layers.size(); index++) {
			Mesh mesh = generateGrassStrand( index, grassInformation );
			models.push_back(mesh);
		}
	}

public:
	Simulation simulation;

	std::vector< Mesh > models{};
	std::vector<int> weights{};
};

int main() {
	GrassFieldSimulation simulation;
	simulation.start();

	return 0;
}