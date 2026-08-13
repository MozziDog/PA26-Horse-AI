#include "AI/Navigation/NavigationBakeService.h"

#include "AI/Navigation/NavigationAssetCatalog.h"
#include "AI/Navigation/VoxelNavigationBakeBuilder.h"
#include "AI/Navigation/VoxelNavigationVolume.h"
#include "Core/Logging/Log.h"
#include "Engine/Platform/Paths.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <filesystem>

bool FNavigationBakeService::Bake(UWorld* World, const FString& ScenePath)
{
	if (!World || ScenePath.empty())
	{
		UE_LOG("[VoxelNavigation] Bake rejected: a world and output scene path are required.");
		return false;
	}

	TArray<AVoxelNavigationVolume*> Volumes;
	for (AActor* Actor : World->GetActors())
	{
		if (AVoxelNavigationVolume* Volume = Cast<AVoxelNavigationVolume>(Actor)) 
			Volumes.push_back(Volume);
	}
	std::sort(Volumes.begin(), Volumes.end(), 
		[](const auto* A, const auto* B) { return A->GetName() < B->GetName(); });

	bool bSuccess = !Volumes.empty();
	for (AVoxelNavigationVolume* Volume : Volumes)
	{
		const FString OutputPath = GetAssetOutputPath(ScenePath, *Volume);
		const std::filesystem::path AssetPath(FPaths::ToWide(OutputPath));
		const FString ReferencePath = FPaths::ToUtf8((AssetPath.parent_path() /
			std::filesystem::path(AssetPath.stem().wstring() + L".reference.json")).wstring());
		FVector BoundsCenter;
		FVector BoundsExtent;
		FVoxelNavigationBuildSettings Settings;
		FVoxelNavigationBakedData BakedData;
		FVoxelNavigationBuildStats Stats;
		FVoxelNavigationBakeBuilder Builder;
		const bool bVolumeSuccess = Volume->GetNavigationBakeInput(BoundsCenter, BoundsExtent, Settings) &&
			Builder.Build(World, BoundsCenter, BoundsExtent, Settings, Volume, BakedData, Stats) &&
			FNavigationAssetCatalog::WriteReferenceJson(ReferencePath, BakedData) &&
			FNavigationAssetCatalog::WriteAsset(OutputPath, ScenePath, BakedData);
		if (bVolumeSuccess) 
			Volume->ApplyNavigationBakeResult(OutputPath, Stats);
		bSuccess = bVolumeSuccess && bSuccess;
	}
	UE_LOG("[VoxelNavigation] Bake %s. Volumes=%d", bSuccess ? "succeeded" : "failed", static_cast<int>(Volumes.size()));
	return bSuccess;
}

FString FNavigationBakeService::GetAssetOutputPath(const FString& ScenePath, const AVoxelNavigationVolume& Volume)
{
	const std::filesystem::path SourcePath(FPaths::ToWide(ScenePath));
	const std::filesystem::path OutputPath = SourcePath.parent_path() / L"Navigation" /
		std::filesystem::path(FPaths::ToWide(Volume.GetName() + ".uasset"));
	return FPaths::ToUtf8(OutputPath.wstring());
}
