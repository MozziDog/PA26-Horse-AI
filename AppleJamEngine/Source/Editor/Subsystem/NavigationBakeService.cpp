#include "Editor/Subsystem/NavigationBakeService.h"

#include "AI/Navigation/VoxelNavigationVolume.h"
#include "Core/Logging/Log.h"
#include "Engine/Platform/Paths.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <filesystem>

bool FNavigationBakeService::Bake(UWorld* EditorWorld, const FString& ScenePath)
{
	if (!EditorWorld || ScenePath.empty())
	{
		UE_LOG("[VoxelNavigation] Bake rejected: save the current scene first.");
		return false;
	}
	if (EditorWorld->GetWorldType() != EWorldType::Editor)
	{
		UE_LOG("[VoxelNavigation] Bake rejected: it must run against an editor world.");
		return false;
	}

	TArray<AVoxelNavigationVolume*> Volumes;
	for (AActor* Actor : EditorWorld->GetActors())
	{
		if (AVoxelNavigationVolume* Volume = Cast<AVoxelNavigationVolume>(Actor)) Volumes.push_back(Volume);
	}
	std::sort(Volumes.begin(), Volumes.end(), [](const auto* A, const auto* B)
	{
		return A->GetName() < B->GetName();
	});

	bool bSuccess = !Volumes.empty();
	for (AVoxelNavigationVolume* Volume : Volumes)
	{
		bSuccess = Volume->BakeNavigationReference(GetReferenceOutputPath(ScenePath, *Volume)) && bSuccess;
	}
	UE_LOG("[VoxelNavigation] Bake %s. Volumes=%d", bSuccess ? "succeeded" : "failed", static_cast<int>(Volumes.size()));
	return bSuccess;
}

FString FNavigationBakeService::GetReferenceOutputPath(const FString& ScenePath, const AVoxelNavigationVolume& Volume)
{
	const std::filesystem::path SourcePath(FPaths::ToWide(ScenePath));
	const std::filesystem::path OutputPath = SourcePath.parent_path() / L"Navigation" /
		std::filesystem::path(FPaths::ToWide(Volume.GetName() + ".reference.json"));
	return FPaths::ToUtf8(OutputPath.wstring());
}
