#pragma once

#include <string>
#include <filesystem>
#include "Core/Types/CoreTypes.h"
#include "Platform/Paths.h"
#include "GameFramework/WorldContext.h"
#include "Math/Vector.h"
#include "Core/Types/PropertyTypes.h"

// Forward declarations
class UObject;
class UWorld;
class AActor;
class UActorComponent;
class USceneComponent;
struct FMinimalViewInfo;

namespace json
{
	class JSON;
}


using std::string;

// Perspective 뷰포트 카메라의 씬 스냅샷 — 씬 저장/로드 시 주고받는 순수 데이터
struct FPerspectiveCameraData
{
	FVector Location = FVector(0, 0, 0);
	FVector Rotation = FVector(0, 0, 0); // Euler (Roll, Pitch, Yaw) in degrees
	float   FOV      = 3.14159265f / 3.0f;
	float   NearClip = 0.1f;
	float   FarClip  = 1000.0f;
	bool    bValid   = false;
};

class FSceneSaveManager
{
public:
	static constexpr const wchar_t* SceneExtension = L".Scene";

	static std::wstring GetSceneDirectory() { return FPaths::SceneDir(); }

	static void SaveSceneAsJSON(const string& SceneName, FWorldContext& WorldContext, const struct FMinimalViewInfo* PerspectivePOV = nullptr);
	// OverrideWorldType: 호출자가 World 의 WorldType 을 명시 — Game 빌드처럼 scene 파일에
	// 기록된 EWorldType (보통 Editor) 을 무시하고 강제로 다른 타입으로 시작하고 싶을 때 사용.
	// nullptr 이면 scene 파일의 값을 따른다 (없으면 Editor). UWorld 의 default WorldType
	// 이 Editor 라 actor deserialize 시점에 EditorOnly 컴포넌트의 SceneProxy 가 만들어지는
	// 사고를 막기 위해, 이 값은 Actor 생성 전에 World 에 적용된다.
	static void LoadSceneFromJSON(const string& filepath, FWorldContext& OutWorldContext, FPerspectiveCameraData& OutCam, const EWorldType* OverrideWorldType = nullptr);

	static TArray<FString> GetSceneFileList();

	// 단일 컴포넌트 서브트리(예: UI Canvas)를 기존 씬 컴포넌트-트리 직렬화로 JSON 문자열로 만든다.
	// 독립 .uasset(UUIAsset) 저장이 이 진입점을 재사용한다(진단 B, 사이클 1). 월드/액터 불필요.
	static FString SerializeUITree(USceneComponent* Root);

	// SerializeUITree 의 역방향 — JSON 문자열을 라이브 컴포넌트 서브트리로 복원한다(사이클 ⓪).
	// 기존 씬 역직렬화 재귀(DeserializeSceneComponentTree) + 지연 프로퍼티 flush 를 그대로 재사용.
	// 생성된 컴포넌트는 Owner 액터에 등록된다(Owner 는 월드 없이도 가능 — 에디터 전용 소유자).
	// 반환 = 루트 컴포넌트(보통 UUICanvas) 또는 nullptr.
	static USceneComponent* DeserializeUITree(const FString& Json, AActor* Owner);

	// ─── Prefab(단일 액터 자산) ───
	// 액터 1개와 그 컴포넌트 트리를 prefab 용 JSON 으로 직렬화한다. 월드 불필요.
	// 액터/컴포넌트의 ObjectId 를 사전 수집(CollectActorObjectIds)한 뒤 직렬화하므로,
	// 컴포넌트 간/액터-컴포넌트 간 참조가 순서와 무관하게 로컬 ObjectId 로 보존된다.
	// (프리팹 밖 외부 씬 객체 참조는 ObjectId 0 → 로드 시 null. UE prefab 과 동일한 의미.)
	static FString SerializeActorToPrefab(AActor* Actor);

	// SerializeActorToPrefab 의 역방향 — JSON 을 라이브 액터로 복원해 World 에 스폰한다.
	// 씬 로드와 동일한 2-패스(객체 생성+ObjectId 등록 → 프로퍼티 flush)로 참조를 재해소.
	static AActor* InstantiateActorFromPrefab(const FString& Json, UWorld* World);

	// 액터를 Content/prefab/<name>.uasset 로 저장한다(EAssetPackageType::Prefab 문자열 페이로드).
	// PackagePath 가 비어있으면 액터 이름으로 MakePrefabPackagePath 를 통해 경로를 만든다.
	static bool SaveActorAsPrefab(AActor* Actor, const FString& PackagePath = FString());

	// prefab .uasset 를 읽어 World 에 인스턴스화. 실패 시 nullptr.
	static AActor* InstantiatePrefabFromFile(const FString& PackagePath, UWorld* World);

	// Content/prefab/ 디렉토리를 보장하고 <PrefabName>.uasset 의 프로젝트 상대 경로를 반환.
	static FString MakePrefabPackagePath(const FString& PrefabName);

	struct FSceneSaveContext
	{
		TMap<const UObject*, uint32> ObjectToId;
		uint32 NextObjectId = 1;

		uint32 RegisterSceneObject(const UObject* Object);
		uint32 FindObjectId(const UObject* Object) const;
	};

	struct FPendingPropertyLoad
	{
		UObject* Object = nullptr;
		json::JSON* Properties = nullptr;
	};

	struct FSceneLoadContext
	{
		TMap<uint32, UObject*> ObjectById;
		TArray<FPendingPropertyLoad> PendingProperties;

		void RegisterLoadedObject(json::JSON& Node, UObject* Object);
		UObject* FindObjectById(uint32 ObjectId) const;
		void QueueProperties(UObject* Object, json::JSON& Properties);
	};

private:
	// ---- Serialization ----
	static void CollectWorldObjectIds(UWorld* World, FSceneSaveContext& Context);
	static void CollectActorObjectIds(AActor* Actor, FSceneSaveContext& Context);
	static void CollectSceneComponentObjectIds(USceneComponent* Comp, FSceneSaveContext& Context);
	static json::JSON SerializeWorld(UWorld* World, const FWorldContext& Ctx, const FMinimalViewInfo* PerspectivePOV, FSceneSaveContext& Context);
	static json::JSON SerializeActor(AActor* Actor, FSceneSaveContext& Context);
	static json::JSON SerializeSceneComponentTree(USceneComponent* Comp, FSceneSaveContext& Context);
	static json::JSON SerializeProperties(UObject* Obj, FSceneSaveContext& Context);

	// ---- Camera ----
	static json::JSON SerializeCamera(const FMinimalViewInfo* POV);
	static void DeserializeCamera(json::JSON& CamJSON, FPerspectiveCameraData& OutCam);

	// ---- Deserialization helpers ----
	static USceneComponent* DeserializeSceneComponentTree(json::JSON& Node, AActor* Owner, FSceneLoadContext& Context);
	static void DeserializeProperties(UObject* Obj, json::JSON& PropsJSON, FSceneLoadContext& Context);

	static string GetCurrentTimeStamp();
};
