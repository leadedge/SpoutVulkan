#pragma once
#ifndef __spoutVK__
#define __spoutVK__

// Include the vulkan hpp header from the vulkan sdk.
// This requires the vulkan sdk's include path to be present
// in the include search directories.
#define VULKAN_HPP_NO_EXCEPTIONS
#ifndef VK_USE_PLATFORM_WIN32_KHR
#	define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#	define _WINSOCKAPI_
#include <vulkan/vulkan.hpp>

#include "SpoutDX\SpoutCommon.h"
#include "SpoutDX\SpoutDirectX.h"
#include "SpoutDX\SpoutSenderNames.h"
#include "SpoutDX\SpoutSharedMemory.h"
#include "SpoutDX\SpoutFrameCount.h"
#include "SpoutDX\SpoutUtils.h"

class spoutVK {

public:
	spoutVK();
	~spoutVK();

	bool OpenDirectX11();
	void CloseDirectX11();

	bool CreateSharedDX11texture(uint32_t width, uint32_t height, DWORD dwFormat = DXGI_FORMAT_B8G8R8A8_UNORM);
	void ReleaseSharedDX11texture();

	bool LinkVulkanImage(VkPhysicalDevice physicalDevice, VkDevice device,
		HANDLE dxShareHandle, uint32_t width, uint32_t height, DWORD D3D11format = DXGI_FORMAT_B8G8R8A8_UNORM);
	void CopyVulkanImage(VkPhysicalDevice physicaldevice, VkCommandBuffer commandBuffer,
		VkImage srcImage, VkImageLayout srcLayout, VkFormat srcFormat,
		VkImage dstImage, VkImageLayout dstLayout, VkFormat dstFormat,
		uint32_t srcWidth, uint32_t srcHeight,
		uint32_t dstWidth, uint32_t dstHeight);
	void ReleaseVulkanImage(VkDevice logicaldevice);
	uint32_t findMemoryType(VkPhysicalDevice physicaldevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
	bool CheckVulkanExtensions(VkPhysicalDevice physicalDevice);

	// Sender
	bool SendImage(VkPhysicalDevice physicaldevice, VkDevice logicaldevice,
		VkCommandBuffer commandbuffer, VkImage vulkanimage, VkImageLayout layout,
		uint32_t width, uint32_t height, VkFormat format);
	bool SetSenderName(const char * sendername = nullptr);
	bool CreateSender(std::string senderName, uint32_t width, uint32_t height, DWORD dwFormat = DXGI_FORMAT_B8G8R8A8_UNORM);
	bool CheckSender(VkPhysicalDevice physicaldevice, VkDevice logicaldevice,
		std::string sendername, uint32_t width, uint32_t height,
		DWORD dwFormat = DXGI_FORMAT_B8G8R8A8_UNORM);
	void ReleaseSender();
	DWORD GetD3Dformat(VkFormat vulkanFormat);
	VkFormat GetVulkanFormat(DWORD dwD3Dformat);

	// Receiver
	bool ReceiveImage(VkPhysicalDevice physicaldevice, VkDevice logicaldevice,
		VkCommandBuffer commandbuffer, VkImage vulkanimage, VkImageLayout layout,
		VkFormat vulkanformat, uint32_t width = 0, uint32_t height = 0);
	HANDLE ReceiveSenderTexture(VkPhysicalDevice physicaldevice, VkDevice logicaldevice);
	uint32_t GetSenderWidth();
	uint32_t GetSenderHeight();
	void ReleaseReceiver();
	std::string SelectSender(HWND hwnd = nullptr);
	void HoldFps(int fps);

private:
	// Vulkan import image
	VkImage m_vkLinkedImage = nullptr;
	VkDeviceMemory m_vkImageMemory = nullptr;
	bool m_bBlitSupported = false;

	// DirectX 11
	ID3D11Device * m_pD3D11Device = nullptr;
	ID3D11DeviceContext * m_pImmediateContext = nullptr;
	bool m_bSpoutInitialized = false;

	// Sender / Receiver
	char m_SenderName[256] {};
	unsigned int m_Width = 0;
	unsigned int m_Height = 0;
	DWORD m_dwFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	ID3D11Texture2D * m_pSharedTexture = nullptr;
	HANDLE m_dxShareHandle = nullptr;
	bool m_bInitialized = false;

	spoutFrameCount frame;
	spoutSenderNames sendernames;
	spoutDirectX spoutdx;

};

#endif
