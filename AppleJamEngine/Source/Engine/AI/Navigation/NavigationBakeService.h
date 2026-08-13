#pragma once

#include "Core/Types/CoreTypes.h"

class UWorld;

// 전역에서 접근할 수 있는 Navigation Data 베이크 요청
// 현재는 에디터 콘솔 명령어로만 접근하지만 필요하다면 게임 빌드 런타임에서도 호출 가능 (e.g. 절차적 지형 생성 등)
class FNavigationBakeService
{
public:
	// 월드 내에 존재하는 모든 AVoxelNavigationVolume에 대해서 베이크 수행
	static bool Bake(UWorld* World, const FString& ScenePath);

private:
	static FString GetAssetOutputPath(const FString& ScenePath, const class AVoxelNavigationVolume& Volume);
};
