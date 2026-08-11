#pragma once
#include "Core/Types/CoreTypes.h"

struct ID3D11Resource;

class MemoryStats
{
public:
	static void OnAllocated(uint64 Size)
	{
		TotalAllocationBytes += Size;
		TotalAllocationCount++;
	}

	static void OnDeallocated(uint64 Size)
	{
		TotalAllocationBytes -= Size;
		TotalAllocationCount--;
	}

	static void AddPixelShaderMemory(uint64 Size) { PixelShaderMemory += Size; }
	static void SubPixelShaderMemory(uint64 Size) { PixelShaderMemory -= Size; }

	static void AddVertexShaderMemory(uint64 Size) { VertexShaderMemory += Size; }
	static void SubVertexShaderMemory(uint64 Size) { VertexShaderMemory -= Size; }

	// Texture
	static void AddTextureMemory(uint64 Size) { TextureMemory += Size; }
	static void SubTextureMemory(uint64 Size) { TextureMemory -= Size; }

	// GPU Buffer
	static void AddVertexBufferMemory(uint64 Size) { VertexBufferMemory += Size; }
	static void SubVertexBufferMemory(uint64 Size) { VertexBufferMemory -= Size; }

	static void AddIndexBufferMemory(uint64 Size) { IndexBufferMemory += Size; }
	static void SubIndexBufferMemory(uint64 Size) { IndexBufferMemory -= Size; }

	// StaticMesh CPU
	static void AddStaticMeshCPUMemory(uint64 Size) { StaticMeshCPUMemory += Size; }
	static void SubStaticMeshCPUMemory(uint64 Size) { StaticMeshCPUMemory -= Size; }

	// SkeletalMesh CPU
	static void AddSkeletalMeshCPUMemory(uint64 Size) { SkeletalMeshCPUMemory += Size; }
	static void SubSkeletalMeshCPUMemory(uint64 Size) { SkeletalMeshCPUMemory -= Size; }

	// Voxel Navigation CPU
	static void AddVoxelNavigationMemory(uint64 Size) { VoxelNavigationMemory += Size; }
	static void SubVoxelNavigationMemory(uint64 Size) { VoxelNavigationMemory -= Size; }
	// 복셀은 VoxelGrid에서 중앙집중식으로 관리하는 점을 고려해서 예외적으로 Set 형태 허용
	static void SetVoxelNavigationMemory(uint64 Size) { VoxelNavigationMemory = Size; }

	static uint64 GetTotalAllocationBytes() { return TotalAllocationBytes; }
	static uint32 GetTotalAllocationCount() { return TotalAllocationCount; }
	static uint64 GetPixelShaderMemory() { return PixelShaderMemory; }
	static uint64 GetVertexShaderMemory() { return VertexShaderMemory; }
	static uint64 GetTextureMemory() { return TextureMemory; }
	static uint64 GetVertexBufferMemory() { return VertexBufferMemory; }
	static uint64 GetIndexBufferMemory() { return IndexBufferMemory; }
	static uint64 GetStaticMeshCPUMemory() { return StaticMeshCPUMemory; }
	static uint64 GetSkeletalMeshCPUMemory() { return SkeletalMeshCPUMemory; }
	static uint64 GetVoxelNavigationMemory() { return VoxelNavigationMemory; }

	static uint64 CalculateTextureMemory(ID3D11Resource* Resource);

private:
	static uint64 TotalAllocationBytes;
	static uint32 TotalAllocationCount;
	static uint64 PixelShaderMemory;
	static uint64 VertexShaderMemory;
	static uint64 TextureMemory;
	static uint64 VertexBufferMemory;
	static uint64 IndexBufferMemory;
	static uint64 StaticMeshCPUMemory;
	static uint64 SkeletalMeshCPUMemory;
	static uint64 VoxelNavigationMemory;
};
