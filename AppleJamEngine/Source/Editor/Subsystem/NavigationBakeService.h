#pragma once

#include "Core/Types/CoreTypes.h"

class UWorld;

// Editor-only orchestration for authoring and persisting voxel navigation data.
// It deliberately receives only the editor world and its saved scene path so
// UEditorEngine remains unaware of navigation implementation details.
class FNavigationBakeService
{
public:
	static bool Bake(UWorld* EditorWorld, const FString& ScenePath);

private:
	static FString GetAssetOutputPath(const FString& ScenePath, const class AVoxelNavigationVolume& Volume);
};
