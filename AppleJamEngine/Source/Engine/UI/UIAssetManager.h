#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"

class UUIAsset;

// UI 에셋(.uasset, EAssetPackageType::UI)의 파일 단위 저장/로드 진입점(사이클 1).
// LuaBlueprint / Material 매니저와 동일 패턴: FAssetPackage 프리루드로 헤더를 쓰고,
// 본문(컴포넌트-트리 직렬화 JSON)은 FAssetPackage::Save/LoadStringPayload 로 싣는다(진단 B).
class FUIAssetManager : public TSingleton<FUIAssetManager>, public FGCObject
{
	friend class TSingleton<FUIAssetManager>;

public:
	UUIAsset* Load(const FString& Path);
	UUIAsset* Find(const FString& Path) const;
	bool      Save(UUIAsset* Asset);

	const char* GetReferencerName() const override { return "FUIAssetManager"; }
	void        AddReferencedObjects(FReferenceCollector& Collector) override;
	void        ClearCache();

private:
	TMap<FString, UUIAsset*> LoadedAssets;
};
