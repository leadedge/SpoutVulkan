#include "SpoutVk.h"

spoutVK::spoutVK() {
	m_pSharedTexture = nullptr;
	m_dxShareHandle = nullptr;
	OpenDirectX11(); // Initialize D3D11
}

spoutVK::~spoutVK()
{
	ReleaseSharedDX11texture();
	CloseDirectX11();
}

bool spoutVK::OpenDirectX11()
{
	if (spoutdx.OpenDirectX11()) {
		m_pD3D11Device = spoutdx.GetDX11Device();
		m_pImmediateContext = spoutdx.GetDX11Context();
		return true;
	}
	return false;
}

void spoutVK::CloseDirectX11()
{
	if (m_pD3D11Device)
		spoutdx.CloseDirectX11();
}

bool spoutVK::CreateSharedDX11texture(uint32_t width, uint32_t height, DWORD dwFormat) {
	if (m_pD3D11Device) {
		if (spoutdx.CreateSharedDX11Texture(m_pD3D11Device, width, height,
				(DXGI_FORMAT)dwFormat, &m_pSharedTexture, m_dxShareHandle)) {
		}
		return true;
	}
	return false;
}

void spoutVK::ReleaseSharedDX11texture() {
	if (m_pD3D11Device && m_pSharedTexture) {
		spoutdx.ReleaseDX11Texture(m_pSharedTexture);
		m_pSharedTexture = nullptr;
		m_dxShareHandle = nullptr;
	}
}

//
// Based on the Nvidia article
// https://developer.nvidia.com/getting-vulkan-ready-vr
// Sample Code - Importing a Direct3D 11 Texture
//
// The process is :
//
// 1) Use a handle from a valid shared D3D11 texture
// 2) Create a Vulkan import image corresponding to the D3D11 texture
// 3) Create a Vulkan memory object (VkDeviceMemory handle)
//    from the share handle of the D3D11 texture
// 4) Bind the memory to the Vulkan Image
//
// The D3D11 texture memory is then linked to the Vulkan image
// o Any changes to the D3D11 texture by way of the handle
//   then apply to the Vulkan image (receiver)
// o Any changes to the Vulkan image apply to the D2D11 texture (sender)
//
bool spoutVK::LinkVulkanImage(VkPhysicalDevice physicaldevice,
	VkDevice logicaldevice,	HANDLE dxShareHandle,
	uint32_t width, uint32_t height, DWORD D3D11format)
{

	if(m_bInitialized)
		return false; // ???

	//
	// D3D11 formats supported for a receiver
	//	DXGI_FORMAT_B8G8R8A8_UNORM
	//	DXGI_FORMAT_R8G8B8A8_UNORM
	//	DXGI_FORMAT_R16G16B16A16_UNORM
	//	DXGI_FORMAT_R16G16B16A16_FLOAT
	//	DXGI_FORMAT_R32G32B32A32_FLOAT
	//
	// For this example, a sender is always default
	//   DXGI_FORMAT_B8G8R8A8_UNORM
	//
	VkFormat vulkanformat = GetVulkanFormat((DXGI_FORMAT)D3D11format);
	ReleaseVulkanImage(logicaldevice); // Clean up any previous resources

	//
	// Query the Vulkan driver for Direct3D image support.
	//
	// Specifically, support for VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT
	//
	// The handle type for a Spout sender cannot be NT so KMT must be used.
	// The handle type used in the NVidia article is Nvidia specific :
	//    VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_IMAGE_KMT_BIT_NV
	// The Vulkan standard defines the equivalent :
	//    VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT
	//
	VkExternalMemoryHandleTypeFlags handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

	//
	// Query support for external image format
	//
	// The function vkGetPhysicalDeviceExternalImageFormatPropertiesNV
	// from the Nvidia article may not be defined.
	// vkGetPhysicalDeviceImageFormatProperties2 is a core Vulkan extension
	//
	VkPhysicalDeviceImageFormatInfo2 formatInfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 };
	formatInfo.format = vulkanformat;
	formatInfo.type = VK_IMAGE_TYPE_2D;
	formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	formatInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkPhysicalDeviceExternalImageFormatInfo externalFormatInfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO };
	externalFormatInfo.handleType = (VkExternalMemoryHandleTypeFlagBits)handleType;
	formatInfo.pNext = &externalFormatInfo;

	VkExternalImageFormatProperties externalImageFormatProps = { VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
	VkImageFormatProperties2 imageFormatProps2 = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
	imageFormatProps2.pNext = &externalImageFormatProps;

	VkResult result = vkGetPhysicalDeviceImageFormatProperties2(physicaldevice,
		&formatInfo,
		&imageFormatProps2);
	if (result != VK_SUCCESS) {
		SpoutLogWarning("spoutVK::LinkVulkanImage - KMT handle not supported");
		return false;
	}

	//
	// After checking for basic support, the application should check the values
	// in VkExternalImageFormatProperties to determine whether the desired operations
	// are supported for the requested handle type. In this case, the application wants
	// to import an existing object.
	//
	VkExternalMemoryFeatureFlags externalMemoryFeatures =
		externalImageFormatProps.externalMemoryProperties.externalMemoryFeatures;

	if ((externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) {
		SpoutLogWarning("spoutVK::LinkVulkanImage - cannot import memory with this handle type");
		return false;
	}

	//
	// Create the Vulkan Import Image
	//
	// After verifying the necessary driver capabilities are available,
	// the application can create a Vulkan image with parameters corresponding
	// to those of the Direct3D texture, and bind it to a Vulkan memory object
	// created from the shared handle of the Direct3D texture.
	//
	// Set up external memory info for image creation
	VkExternalMemoryImageCreateInfo extMemoryImageInfo = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.handleTypes = handleType,
	};

	// Create the Vulkan image
	VkImageCreateInfo imageCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = &extMemoryImageInfo,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = vulkanformat,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	result = vkCreateImage(logicaldevice, &imageCreateInfo, nullptr, &m_vkLinkedImage);
	if (result != VK_SUCCESS) {
		SpoutLogWarning("spoutVK::LinkVulkanImage - could not create Vulkan image");
		return false;
	}

	//
	// Query memory requirements and allocate memory.
	//
	// After creating the image, its memory requirements must be queried to determine
	// the supported memory types. The application can then use one of the supported
	// memory types when importing the external DirectX handle to a Vulkan memory object.
	// If a VK_NV_dedicated_allocation is not used, the memory must be bound to an image as usual.
	//

	// Get memory requirements for the image
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(logicaldevice, m_vkLinkedImage, &memRequirements);

	uint32_t memoryTypeIndex = findMemoryType(physicaldevice, memRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (memoryTypeIndex == UINT32_MAX) {
		SpoutLogWarning("spoutVK::LinkVulkanImage - no suitable memory type");
		return false;
	}

	//
	// Set up import memory info
	//
	// This is where the DirectX texture handle is used
	// to import texture memory and bind it to the Vulkan image.
	//
	VkImportMemoryWin32HandleInfoKHR importMemoryInfo = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
		.pNext = nullptr,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT,
		.handle = dxShareHandle,
		.name = nullptr
	};

	// 
	// Some implementations may require use of the VK_NV_dedicated_allocation extension
	// when creating images from some handle types, as indicated by
	// VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT.
	//
	// The function VkDedicatedAllocationImageCreateInfoNV and constants
	//   VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_IMAGE_CREATE_INFO_NV
	//   VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT_NV
	// from the Nvidia article may not be defined.
	// VkMemoryDedicatedAllocateInfo and VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT
	// and vkGetPhysicalDeviceImageFormatProperties2 are core Vulkan
	//

	// Set up dedicated memory allocation info (only if required)
	VkMemoryDedicatedAllocateInfo dedicatedAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.pNext = &importMemoryInfo,
		.image = m_vkLinkedImage,
		.buffer = VK_NULL_HANDLE
	};

	// Choose correct pNext for memory allocation
	void* pNextAllocInfo = nullptr;
	if (externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) {
		pNextAllocInfo = &dedicatedAllocInfo;
	}
	else {
		pNextAllocInfo = &importMemoryInfo;
	}

	// Final memory allocation
	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = pNextAllocInfo,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = memoryTypeIndex
	};

	result = vkAllocateMemory(logicaldevice, &allocInfo, nullptr, &m_vkImageMemory);
	if (result != VK_SUCCESS) {
		SpoutLogWarning("spoutVK::LinkVulkanImage - could not allocate image memory");
		return false;
	}

	// Bind memory to the Vulkan Image
	result = vkBindImageMemory(logicaldevice, m_vkLinkedImage, m_vkImageMemory, 0);
	if (result != VK_SUCCESS) {
		SpoutLogWarning("spoutVK::LinkVulkanImage - could not bind image memory");
		return false;
	}

	m_bInitialized = true;
	return true;

}

// Copy from a Vulkan image to a destination image 
// Images sizes and formats can be different for blit copy
void spoutVK::CopyVulkanImage(VkPhysicalDevice physicaldevice,
	VkCommandBuffer commandBuffer,
	VkImage srcImage, VkImageLayout srcLayout, VkFormat srcFormat,
	VkImage dstImage, VkImageLayout dstLayout, VkFormat dstFormat,
	uint32_t srcWidth, uint32_t srcHeight,
	uint32_t dstWidth, uint32_t dstHeight)
{
	// Transition the source image to TRANSFER_SRC_OPTIMAL layout
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = srcLayout;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.image = srcImage; // source image
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	// Transition destination image to TRANSFER_DST_OPTIMAL layout
	barrier.oldLayout = dstLayout;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.image = dstImage; // destination image
	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	// Check format support for blit
	// Source must support VK_FORMAT_FEATURE_BLIT_SRC_BIT
	// Destination must support VK_FORMAT_FEATURE_BLIT_DST_BIT
	bool bBlitSupported = false;
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(physicaldevice, srcFormat, &props);
	if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) {
		vkGetPhysicalDeviceFormatProperties(physicaldevice, dstFormat, &props);
		if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) {
			bBlitSupported = true;
		}
	}

	//
	// Blit if supported
	//
	// Blit can copy between different image sizes and
	// Higher<>lower bit depth and Float>unorm.
	//
	// Success is dependent on driver support but is most likely
	// for copy between the same format types such as BGRA<>RGBA.
	// (see GetVulkanFormat) .
	//
	if (bBlitSupported) {

		VkImageBlit blitRegion {};
		blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.srcSubresource.layerCount = 1;
		blitRegion.srcOffsets[0] = { 0, 0, 0 };
		blitRegion.srcOffsets[1] = { (int32_t)srcWidth, (int32_t)srcHeight, 1 };
		blitRegion.dstSubresource = blitRegion.srcSubresource;
		blitRegion.dstOffsets[0] = { 0, 0, 0 };
		blitRegion.dstOffsets[1] = { (int32_t)dstWidth, (int32_t)dstHeight, 1 };
		vkCmdBlitImage(commandBuffer,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blitRegion,
			VK_FILTER_LINEAR);
	}
	else if(srcWidth == dstWidth && srcHeight == dstHeight) {

		//
		// Copy if blit is not supported
		// Source and destiation image sizes must match
		// Formats must have the same component counts, bit depth and type
		//

		// Define copy region
		VkImageCopy copyRegion {};
		copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.srcSubresource.baseArrayLayer = 0;
		copyRegion.srcSubresource.mipLevel = 0;
		copyRegion.srcSubresource.layerCount = 1;
		copyRegion.dstSubresource = copyRegion.srcSubresource;
		copyRegion.srcOffset = { 0, 0, 0 };
		copyRegion.dstOffset = { 0, 0, 0 };
		copyRegion.extent.width  = dstWidth;
		copyRegion.extent.height = dstHeight;
		copyRegion.extent.depth = 1;
		// Copy the image
		vkCmdCopyImage(commandBuffer,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &copyRegion);
	}

	// Transition destination image back to dstLayout
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = dstLayout;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.image = dstImage;
	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	// Transition source image back to to srcLayout
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = srcLayout;
	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

}


void spoutVK::ReleaseVulkanImage(VkDevice logicaldevice)
{
	if(!logicaldevice)
		return;

	if (m_vkLinkedImage) vkDestroyImage(logicaldevice, m_vkLinkedImage, nullptr);
	if (m_vkImageMemory) vkFreeMemory(logicaldevice, m_vkImageMemory, nullptr);
	m_vkLinkedImage = nullptr;
	m_vkImageMemory = nullptr;
}

uint32_t spoutVK::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
	// No suitable memory type found
    return UINT32_MAX;
}

bool spoutVK::CheckVulkanExtensions(VkPhysicalDevice physicalDevice)
{
    // Instance extensions
    const std::vector<const char*> requiredInstanceExtensions = {
        "VK_KHR_surface",
        "VK_KHR_win32_surface",
        "VK_KHR_get_physical_device_properties2"
    };

    uint32_t instanceExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, nullptr);
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, instanceExtensions.data());

    for (const char* ext : requiredInstanceExtensions) {
        bool found = false;
        for (const auto& available : instanceExtensions) {
            if (strcmp(ext, available.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            SpoutLogError("spoutVK::CheckVulkanExtensions - missing instance extension: %s", ext);
            return false;
        }
    }

    // Device extensions
    const std::vector<const char*> requiredDeviceExtensions = {
        "VK_KHR_external_memory",
        "VK_KHR_external_memory_win32",
        "VK_KHR_dedicated_allocation",
        "VK_KHR_get_memory_requirements2"
    };

    uint32_t deviceExtCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtCount, deviceExtensions.data());

    for (const char* ext : requiredDeviceExtensions) {
        bool found = false;
        for (const auto& available : deviceExtensions) {
            if (strcmp(ext, available.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            SpoutLogError("spoutVK::CheckVulkanExtensions - missing device extension: %s", ext);
            return false;
        }
    }

	SpoutLogNotice("spoutVK::CheckVulkanExtensions - all required extensions found");

    return true;
}


//
// DXGI formats supported
//
//	DXGI_FORMAT_B8G8R8A8_UNORM
//	DXGI_FORMAT_R8G8B8A8_UNORM
//  DXGI_FORMAT_R10G10B10A2_UNORM
//	DXGI_FORMAT_R16G16B16A16_UNORM
//	DXGI_FORMAT_R16G16B16A16_FLOAT
//	DXGI_FORMAT_R32G32B32A32_FLOAT
//
DWORD spoutVK::GetD3Dformat(VkFormat vulkanFormat)
{
	DXGI_FORMAT dxFormat;
	switch (vulkanFormat) {
		case DXGI_FORMAT_R8G8B8A8_UNORM: {
			dxFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		}
		case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
			dxFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
			break;
		}
		case VK_FORMAT_R16G16B16A16_UNORM: {
			dxFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
			break;
		}
		case VK_FORMAT_R16G16B16A16_SFLOAT: {
			dxFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
			break;
		}
		case VK_FORMAT_R32G32B32A32_SFLOAT: {
			dxFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
			break;
		}
		default:
		case VK_FORMAT_B8G8R8A8_UNORM: {
			dxFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
			break;
		}
	}
	return (DWORD)dxFormat;
}

//
// Vulkan formats supported
//
//  VK_FORMAT_B8G8R8A8_UNORM            44
//	VK_FORMAT_R8G8B8A8_UNORM            37
//  VK_FORMAT_A2B10G10R10_UNORM_PACK32  64
//	VK_FORMAT_R16G16B16A16_UNORM        91
//	VK_FORMAT_R16G16B16A16_SFLOAT       97
//	VK_FORMAT_R32G32B32A32_SFLOAT      109
//
VkFormat spoutVK::GetVulkanFormat(DWORD dwFormat)
{
	VkFormat vkFormat;
	DXGI_FORMAT dxFormat = (DXGI_FORMAT)dwFormat;
	switch (dxFormat) {
		case DXGI_FORMAT_R8G8B8A8_UNORM: {
			vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
			break;
		}
		case DXGI_FORMAT_R10G10B10A2_UNORM: {
			vkFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			break;
		}
		case DXGI_FORMAT_R16G16B16A16_UNORM: {
			vkFormat = VK_FORMAT_R16G16B16A16_UNORM;
			break;
		}
		case DXGI_FORMAT_R16G16B16A16_FLOAT: {
			vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
			break;
		}
		case DXGI_FORMAT_R32G32B32A32_FLOAT: {
			vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
			break;
		}
		default:
		case DXGI_FORMAT_B8G8R8A8_UNORM: {
			vkFormat = VK_FORMAT_B8G8R8A8_UNORM;
			break;
		}
	}
	return vkFormat;
}

bool spoutVK::SendImage(VkPhysicalDevice physicaldevice, VkDevice logicaldevice,
	VkCommandBuffer commandbuffer, VkImage vulkanimage, VkImageLayout layout,
	uint32_t width, uint32_t height, VkFormat format)
{
	// 1) Check for required extensions
	// 2) If not initialized
	//      o Create a DirectX11 shared texture and share handle.
	//      o Create a Vulkan image and link with D3D11 memory
	//        using the texture share handle
	//      o Create a sender
	// 3) For a size change
	//      o Update the D3D11 texture and linked Vulkan image
	//      o Update the sender information
	//
	if (!CheckVulkanExtensions(physicaldevice)) {
		SpoutLogError("spoutVK::SendImage - required Vulkan extensions not supported");
		return false;
	}

	if(CheckSender(physicaldevice, logicaldevice,
		m_SenderName, width, height, GetD3Dformat(format))) {
		 // 3) Get access to the shared texture
		if (frame.CheckAccess()) {
			// 4) Copy the image to the linked Vulkan image
			//    to update the sender's shared texture.
			CopyVulkanImage(physicaldevice, commandbuffer,
				vulkanimage,                 // Sending image source
				layout,                      // Sending image layout
				GetVulkanFormat(m_dwFormat), // Sending image format
				m_vkLinkedImage,             // Linked image destination
				VK_IMAGE_LAYOUT_GENERAL,     // Linked image layout
				GetVulkanFormat(m_dwFormat), // Linked image format
				width, height,               // Sending image dimensions
				width, height);              // Linked image dimensions
			frame.AllowAccess();
			// 5) Signal a new frame for receivers
			frame.SetNewFrame();
			return true;
		}
	}
	return false;
}

bool spoutVK::SetSenderName(const char* sendername)
{
	// Executable name default
	if (!sendername)
		strcpy_s(m_SenderName, 256, GetExeName().c_str());
	else
		strcpy_s(m_SenderName, 256, sendername);

	// Create an incremented name if a sender with this name is already registered,
	// Although this function precedes SpoutSenderNames::RegisterSenderName,
	// a further increment is not applied when a sender with the new name is created.
	char name[256]{};
	strcpy_s(name, 256, m_SenderName);
	if (sendernames.FindSenderName(name)) {
		int i = 1;
		do {
			sprintf_s(name, 256, "%s_%d", m_SenderName, i);
			i++;
		} while (sendernames.FindSenderName(name));
		// Re-set the global sender name
		strcpy_s(m_SenderName, 256, name);
	}

	// Remove the sender from the names list if it's
	// shared memory information does not exist.
	// This can happen if the sender has crashed or if a
	// console window was closed instead of the main program.
	sendernames.CleanSenders();

	return true;
}

bool spoutVK::CreateSender(std::string sendername, uint32_t width, uint32_t height, DWORD dwFormat)
{
	if(sendername.empty())
		SetSenderName(); // Executable name
	else
		strcpy_s(m_SenderName, 256, sendername.c_str());

	// Create the sender
	if(!sendernames.CreateSender(m_SenderName, width, height, m_dxShareHandle, dwFormat)) {
		SpoutLogWarning( "spoutVK::CreateSender - could not create sender" );
		return false;
	}
	// Create a sender mutex for access to the shared texture
	frame.CreateAccessMutex(m_SenderName);

	// Enable frame counting so the receiver gets frame number and fps
	frame.EnableFrameCount(m_SenderName);

	// Update globals
	m_Width = width;
	m_Height = height;

	return true;
}

// o Create a DirectX11 shared texture and share handle.
// o Create a Vulkan image and link with the DirectX11 memory
//   using the texture share handle
// o Create a sender
// o For a size change, update images and the sender
bool spoutVK::CheckSender(VkPhysicalDevice physicaldevice, VkDevice logicaldevice,
	std::string sendername, uint32_t width, uint32_t height, DWORD dwFormat)
{
	// Ensure the GPU is not using the resources
	vkDeviceWaitIdle(logicaldevice);

	if (!m_bInitialized) {
		// Create a D3D11 shared texture with a share handle (m_dxShareHandle)
		// that will be used to link the texture with a Vulkan Image
		
		if (CreateSharedDX11texture(width, height, dwFormat)) {
			// Create a Vulkan image and link with the D3D11 texture memory
			if (LinkVulkanImage(physicaldevice, logicaldevice,
				m_dxShareHandle, width, height, dwFormat)) {
				// Create a sender using the shared texure handle
				// which is linked to the Vulkan image
				CreateSender(sendername.c_str(), width, height, dwFormat);
				m_bInitialized = true;
			}
		}
	}
	else if (width != m_Width || height != m_Height) {
		//
		// For size change, release existing resources
		// and re-create D3D11 texture and Vulkan image
		//
		// Free the sender D3D11 texture
		ReleaseSharedDX11texture();
		m_bInitialized = false;
		// Create a new texture with the new size
		if (CreateSharedDX11texture(width, height)) {
			// Recreate the linked Vulkan image with new size
			if(LinkVulkanImage(physicaldevice, logicaldevice, m_dxShareHandle, width, height)) {
				// Update the sender information
				sendernames.UpdateSender(m_SenderName, width, height, m_dxShareHandle, dwFormat);
				// Update globals
				m_Width = width;
				m_Height = height;
			}
		}
	}
	return true;
}

void spoutVK::ReleaseSender()
{
	if(!m_dxShareHandle)
		return;

	// Release the sender from the name list
	if(m_SenderName && m_SenderName[0])
		sendernames.ReleaseSenderName(m_SenderName);
	m_SenderName[0] = 0;

	// Release sender resources
	ReleaseSharedDX11texture();

}

bool spoutVK::ReceiveImage(VkPhysicalDevice physicaldevice, VkDevice logicaldevice,
	VkCommandBuffer commandbuffer, VkImage vulkanimage, VkImageLayout layout,
	VkFormat vulkanformat, uint32_t width, uint32_t height)
{
	if (!CheckVulkanExtensions(physicaldevice)) {
		SpoutLogError("spoutVK::ReceiveImage - required Vulkan extensions not supported");
		return false;
	}

	// The receiving image dimensions can be different to the sender.
	// Fit to destination if the receiving size is specified and the
	// sender and destination receiver sizes are different.
	int w = width;
	int h = height;
	if (ReceiveSenderTexture(physicaldevice, logicaldevice)) {
		if (frame.CheckAccess()) { // Get access to the shared texture
			// Copy from the linked image to the receiving image
			if(width  == 0) w = GetSenderWidth();
			if(height == 0) h = GetSenderHeight();
			CopyVulkanImage(physicaldevice, commandbuffer,
				m_vkLinkedImage,             // Linked image
				VK_IMAGE_LAYOUT_GENERAL,     // Linked image layout
				GetVulkanFormat(m_dwFormat), // Linked image format
				vulkanimage,                 // Receiving image
				layout,                      // Receiving image layout
				vulkanformat,                // Receiving image format
				GetSenderWidth(), GetSenderHeight(), // Sender dimensions
				w, h); // Receiving image dimensions
			frame.AllowAccess();
			return true;
		}
	}
	return false;
}

HANDLE spoutVK::ReceiveSenderTexture(VkPhysicalDevice physicaldevice, VkDevice device)
{
	// Set the initial width and height to current globals.
	// New width and height are returned from the sender.
	unsigned int width  = m_Width;
	unsigned int height = m_Height;
	DWORD dwFormat = m_dwFormat;

	//
	// Find if the sender exists.
	// Return the sender name, width, height, sharehandle and format.
	//
	// If not yet intialized or SelectSender has been opened, the sender
	// name will be empty and the active sender is returned if that exists.
	if (sendernames.FindSender(m_SenderName, width, height, m_dxShareHandle, dwFormat)) {

		// Set up if not initialized yet
		if (!m_bSpoutInitialized) {
			// Open a named mutex to control access to the sender's shared texture
			frame.CreateAccessMutex(m_SenderName);
			// Enable frame counting to get the sender frame number and fps
			frame.EnableFrameCount(m_SenderName);
			m_bSpoutInitialized = true;
		}

		// Check for size or format change
		if (m_Width != width || m_Height != height || m_dwFormat != dwFormat) {

			// Update globals for subsequent size checks
			m_Width = width;
			m_Height = height;
			m_dwFormat = dwFormat;
			m_bInitialized = false;

			// Create a Vulkan image and link with D3D11 texture memory
			// using the sender share handle retrieved by FindSender above
			if (!LinkVulkanImage(physicaldevice, device, m_dxShareHandle, width, height, dwFormat)) {
				SpoutLogWarning("spoutVK::ReceiveSenderTexture - could not create Vulkan image with received handle 0x%X", PtrToUint(m_dxShareHandle));
				return nullptr;
			}
			m_bInitialized = true;
			// The application can now access and copy the linked image.
		}
		return m_dxShareHandle;
	}

	return nullptr;
}

uint32_t spoutVK::GetSenderWidth()
{
	return m_Width;
}

uint32_t spoutVK::GetSenderHeight()
{
	return m_Height;
}

void spoutVK::ReleaseReceiver()
{
	if (!m_bInitialized)
		return;

	SpoutLogNotice("spoutVK::ReleaseReceiver (%s)", m_SenderName);

	// Wait 4 frames in case the same sender opens again
	Sleep(67);

	// Sender share handled texture
	ReleaseSharedDX11texture();

	// Close the named access mutex and frame counting semaphore.
	frame.CloseAccessMutex();
	frame.CleanupFrameCount();

	// Zero width and height so that they are reset when a sender is found
	m_Width = 0;
	m_Height = 0;

	// Clear the name to find the active sender
	m_SenderName[0] = 0;
	
	// Initialize again when a sender is found
	m_bInitialized = false;

	return;

}

std::string spoutVK::SelectSender(HWND hwnd)
{
	std::string senderstr;

	// Create a sender list
	std::vector<std::string> senderlist;
	int nSenders = sendernames.GetSenderCount();
	if (nSenders > 0) {
		char sendername[256]{};
		for (int i=0; i<nSenders; i++) {
			if (sendernames.GetSender(i, sendername))
				senderlist.push_back(sendername);
		}
	}

	// Get the active sender index "selected".
	// The index is passed in to SpoutMessageBox and used as the current combobox item.
	int selected = 0;
	char sendername[256]{};
	if (sendernames.GetActiveSender(sendername))
		selected = sendernames.GetSenderIndex(sendername);

	// SpoutMessageBox opens either centered on the cursor position
	// or on the application window if the handle is passed in.
	if (!hwnd) {
		POINT pt{};
		GetCursorPos(&pt);
		SpoutMessageBoxPosition(pt);
	}

	// Show the SpoutMessageBox even if the list is empty.
	// This makes it clear to the user that no senders are running.
	if (SpoutMessageBox(hwnd, NULL, "Select sender", MB_OKCANCEL, senderlist, selected) == IDOK && !senderlist.empty()) {
		// Release the receiver
		ReleaseReceiver();
		// Set the selected sender as active for the next receive
		sendernames.SetActiveSender(senderlist[selected].c_str());
		// Return the selected sender name
		// Not used in this example. the active sender is detected
		senderstr = senderlist[selected].c_str();
	} // endif messagebox query

	return senderstr;

}

void spoutVK::HoldFps(int fps)
{
	frame.HoldFps(fps);
}
