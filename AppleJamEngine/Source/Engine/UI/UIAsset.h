#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Object.h"

#include "Source/Engine/UI/UIAsset.generated.h"

// UI 계층 트리를 독립 .uasset 으로 담는 에셋(사이클 1).
// 다른 .uasset 자산(UMaterial / ULuaBlueprintAsset 등)과 동일하게 UObject 를 상속하고,
// FAssetPackage 헤더의 EAssetPackageType::UI 로 종류가 식별된다(진단 A/B).
// 트리 본문은 기존 컴포넌트-트리 직렬화(FSceneSaveManager::SerializeUITree)가 만든 JSON
// 문자열을 그대로 보관한다 — 파일 단위 저장/로드는 FUIAssetManager 가 string payload 로 담당.
// 라이브 UUICanvas 로의 재구성(인스턴스화)은 이후 사이클 범위(진단 F 리스크 #2).
UCLASS()
class UUIAsset : public UObject
{
public:
	GENERATED_BODY()
	UUIAsset()           = default;
	~UUIAsset() override = default;

	void           SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

	// 직렬화된 UI Element 트리(JSON). FSceneSaveManager::SerializeUITree 결과를 그대로 보관.
	void           SetCanvasData(const FString& InData) { CanvasData = InData; }
	const FString& GetCanvasData() const { return CanvasData; }

private:
	FString SourcePath;   // 디스크 경로(.uasset). 로드 시 매니저가 설정.
	FString CanvasData;   // 컴포넌트-트리 직렬화 JSON 본문.
};
