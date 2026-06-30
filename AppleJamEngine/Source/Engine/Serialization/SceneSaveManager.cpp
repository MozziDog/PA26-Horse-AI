#include "SceneSaveManager.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>
#include "SimpleJSON/json.hpp"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Component/SceneComponent.h"
#include "Component/ActorComponent.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Component/Primitive/DecalComponent.h"
#include "Component/Primitive/HeightFogComponent.h"
#include "Component/Light/LightComponentBase.h"
#include "Object/Object.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Core/Types/PropertyTypes.h"
#include "Object/FName.h"
#include "Serialization/JsonArchive.h"
#include "Profiling/Time/PlatformTime.h"
#include "Core/Logging/Log.h"
#include "Asset/AssetPackage.h"

// ---- JSON vector helpers ---------------------------------------------------

static void WriteVec3(json::JSON& Obj, const char* Key, const FVector& V)
{
	json::JSON arr = json::Array();
	arr.append(static_cast<double>(V.X));
	arr.append(static_cast<double>(V.Y));
	arr.append(static_cast<double>(V.Z));
	Obj[Key] = arr;
}

static FVector ReadVec3(json::JSON& Arr)
{
	FVector out(0, 0, 0);
	int i = 0;
	for (auto& e : Arr.ArrayRange()) {
		if (i == 0) out.X = static_cast<float>(e.ToFloat());
		else if (i == 1) out.Y = static_cast<float>(e.ToFloat());
		else if (i == 2) out.Z = static_cast<float>(e.ToFloat());
		++i;
	}
	return out;
}

static bool IsSceneSerializableObject(const UObject* Object)
{
	return IsValid(Object);
}

static bool IsSceneComponentReachableFromRootTree(const USceneComponent* Root, const USceneComponent* Target)
{
	if (!IsSceneSerializableObject(Root) || !IsSceneSerializableObject(Target))
	{
		return false;
	}

	if (Root == Target)
	{
		return true;
	}

	for (USceneComponent* Child : Root->GetChildren())
	{
		if (IsSceneComponentReachableFromRootTree(Child, Target))
		{
			return true;
		}
	}

	return false;
}

// ---------------------------------------------------------------------------

namespace SceneKeys
{
	static constexpr const char* Version = "Version";
	static constexpr const char* Name = "Name";
	static constexpr const char* ClassName = "ClassName";
	static constexpr const char* WorldType = "WorldType";
	static constexpr const char* ContextName = "ContextName";
	static constexpr const char* ContextHandle = "ContextHandle";
	static constexpr const char* WorldSettings = "WorldSettings";
	static constexpr const char* GameMode = "GameMode";  // legacy / WorldSettings 내부 키
	static constexpr const char* Gravity = "Gravity";
	static constexpr const char* Actors = "Actors";
	static constexpr const char* RootComponent = "RootComponent";
	static constexpr const char* NonSceneComponents = "NonSceneComponents";
	static constexpr const char* Properties = "Properties";
	static constexpr const char* Children = "Children";
	static constexpr const char* HiddenInComponentTree = "bHiddenInComponentTree";
	static constexpr const char* ObjectId = "ObjectId";
}

class FSceneJsonSaveArchive : public FJsonArchive
{
public:
	FSceneJsonSaveArchive(json::JSON& Root, const FSceneSaveManager::FSceneSaveContext& InContext)
		: FJsonArchive(Root, /*bInIsSaving=*/true)
		, Context(InContext)
	{
	}

protected:
	uint32 ResolveJsonObjectId(const UObject* Object) const override
	{
		return Context.FindObjectId(Object);
	}

private:
	const FSceneSaveManager::FSceneSaveContext& Context;
};

class FSceneJsonLoadArchive : public FJsonArchive
{
public:
	FSceneJsonLoadArchive(json::JSON& Root, const FSceneSaveManager::FSceneLoadContext& InContext)
		: FJsonArchive(Root, /*bInIsSaving=*/false)
		, Context(InContext)
	{
	}

protected:
	UObject* ResolveJsonObjectReference(uint32 ObjectId) const override
	{
		return ObjectId != 0 ? Context.FindObjectById(ObjectId) : nullptr;
	}

private:
	const FSceneSaveManager::FSceneLoadContext& Context;
};

uint32 FSceneSaveManager::FSceneSaveContext::RegisterSceneObject(const UObject* Object)
{
	if (!IsSceneSerializableObject(Object))
	{
		return 0;
	}

	auto It = ObjectToId.find(Object);
	if (It != ObjectToId.end())
	{
		return It->second;
	}

	const uint32 ObjectId = NextObjectId++;
	ObjectToId.emplace(Object, ObjectId);
	return ObjectId;
}

uint32 FSceneSaveManager::FSceneSaveContext::FindObjectId(const UObject* Object) const
{
	if (!IsSceneSerializableObject(Object))
	{
		return 0;
	}

	auto It = ObjectToId.find(Object);
	return It != ObjectToId.end() ? It->second : 0;
}

void FSceneSaveManager::FSceneLoadContext::RegisterLoadedObject(json::JSON& Node, UObject* Object)
{
	if (!IsSceneSerializableObject(Object) || !Node.hasKey(SceneKeys::ObjectId))
	{
		return;
	}

	const uint32 ObjectId = static_cast<uint32>(Node[SceneKeys::ObjectId].ToInt());
	if (ObjectId != 0)
	{
		ObjectById[ObjectId] = Object;
	}
}

UObject* FSceneSaveManager::FSceneLoadContext::FindObjectById(uint32 ObjectId) const
{
	auto It = ObjectById.find(ObjectId);
	return It != ObjectById.end() ? It->second : nullptr;
}

void FSceneSaveManager::FSceneLoadContext::QueueProperties(UObject* Object, json::JSON& Properties)
{
	if (!IsSceneSerializableObject(Object))
	{
		return;
	}

	PendingProperties.push_back({ Object, &Properties });
}

static void SerializeComponentEditorMetadata(json::JSON& Node, const UActorComponent* Comp)
{
	if (!Comp)
	{
		return;
	}

	if (Comp->IsHiddenInComponentTree())
	{
		Node[SceneKeys::HiddenInComponentTree] = true;
	}
}

static void DeserializeComponentEditorMetadata(UActorComponent* Comp, json::JSON& Node)
{
	if (!Comp)
	{
		return;
	}

	if (Node.hasKey(SceneKeys::HiddenInComponentTree))
	{
		Comp->SetHiddenInComponentTree(Node[SceneKeys::HiddenInComponentTree].ToBool());
	}
}

static void EnsureEditorBillboardMetadata(UActorComponent* Comp)
{
	if (ULightComponentBase* LightComponent = Cast<ULightComponentBase>(Comp))
	{
		LightComponent->EnsureEditorBillboard();
	}
	else if (UDecalComponent* DecalComponent = Cast<UDecalComponent>(Comp))
	{
		DecalComponent->EnsureEditorBillboard();
	}
	else if (UHeightFogComponent* HeightFogComponent = Cast<UHeightFogComponent>(Comp))
	{
		HeightFogComponent->EnsureEditorBillboard();
	}
}

static const char* WorldTypeToString(EWorldType Type)
{
	switch (Type) {
	case EWorldType::Game: return "Game";
	case EWorldType::PIE:  return "PIE";
	default:               return "Editor";
	}
}

static EWorldType StringToWorldType(const string& Str)
{
	if (Str == "Game") return EWorldType::Game;
	if (Str == "PIE")  return EWorldType::PIE;
	return EWorldType::Editor;
}

// ============================================================
// Save
// ============================================================

void FSceneSaveManager::SaveSceneAsJSON(const string& InSceneName, FWorldContext& WorldContext, const FMinimalViewInfo* PerspectivePOV)
{
	using namespace json;
	FScopedGarbageCollectionBlocker GCBlocker;

	if (!IsSceneSerializableObject(WorldContext.World)) return;

	string FinalName = InSceneName.empty()
		? "Save_" + GetCurrentTimeStamp()
		: InSceneName;

	std::wstring SceneDir = GetSceneDirectory();
	std::filesystem::path FileDestination = std::filesystem::path(SceneDir) / (FPaths::ToWide(FinalName) + SceneExtension);
	std::filesystem::create_directories(SceneDir);

	FSceneSaveContext SaveContext;
	CollectWorldObjectIds(WorldContext.World, SaveContext);

	JSON Root = SerializeWorld(WorldContext.World, WorldContext, PerspectivePOV, SaveContext);
	Root[SceneKeys::Version] = 2;
	Root[SceneKeys::Name] = FinalName;

	std::ofstream File(FileDestination);
	if (File.is_open()) {
		File << Root.dump();
		File.flush();
		File.close();
	}
}

void FSceneSaveManager::CollectWorldObjectIds(UWorld* World, FSceneSaveContext& Context)
{
	if (!IsSceneSerializableObject(World))
	{
		return;
	}

	Context.RegisterSceneObject(World);
	for (AActor* Actor : World->GetActors())
	{
		CollectActorObjectIds(Actor, Context);
	}
}

void FSceneSaveManager::CollectActorObjectIds(AActor* Actor, FSceneSaveContext& Context)
{
	if (!IsSceneSerializableObject(Actor))
	{
		return;
	}

	Context.RegisterSceneObject(Actor);
	if (IsSceneSerializableObject(Actor->GetRootComponent()))
	{
		CollectSceneComponentObjectIds(Actor->GetRootComponent(), Context);
	}

	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!IsSceneSerializableObject(Comp))
		{
			continue;
		}

		if (Comp->IsA<USceneComponent>())
		{
			continue;
		}

		Context.RegisterSceneObject(Comp);
	}
}

void FSceneSaveManager::CollectSceneComponentObjectIds(USceneComponent* Comp, FSceneSaveContext& Context)
{
	if (!IsSceneSerializableObject(Comp))
	{
		return;
	}

	Context.RegisterSceneObject(Comp);
	for (USceneComponent* Child : Comp->GetChildren())
	{
		if (!IsSceneSerializableObject(Child))
		{
			continue;
		}
		CollectSceneComponentObjectIds(Child, Context);
	}
}

json::JSON FSceneSaveManager::SerializeWorld(UWorld* World, const FWorldContext& Ctx, const FMinimalViewInfo* PerspectivePOV, FSceneSaveContext& Context)
{
	using namespace json;
	JSON w = json::Object();
	w[SceneKeys::ClassName] = World->GetClass()->GetName();
	w[SceneKeys::ObjectId] = static_cast<int>(Context.RegisterSceneObject(World));
	w[SceneKeys::WorldType] = WorldTypeToString(Ctx.WorldType);
	w[SceneKeys::ContextName] = Ctx.ContextName;
	w[SceneKeys::ContextHandle] = Ctx.ContextHandle.ToString();

	// ---- WorldSettings (씬 단위 게임 설정) ----
	{
		const FWorldSettings& WS = World->GetWorldSettings();
		JSON WSObj = json::Object();
		WSObj[SceneKeys::GameMode] = WS.GameModeClassName;
		WriteVec3(WSObj, SceneKeys::Gravity, WS.Gravity);
		w[SceneKeys::WorldSettings] = WSObj;
	}

	// ---- Actors ----
	JSON Actors = json::Array();
	for (AActor* Actor : World->GetActors()) {
		if (!IsSceneSerializableObject(Actor)) continue;
		Actors.append(SerializeActor(Actor, Context));
	}
	w[SceneKeys::Actors] = Actors;

	// ---- Perspective camera ----
	JSON cam = SerializeCamera(PerspectivePOV);
	if (cam.size() > 0) {
		w["PerspectiveCamera"] = cam;
	}

	return w;
}

json::JSON FSceneSaveManager::SerializeActor(AActor* Actor, FSceneSaveContext& Context)
{
	using namespace json;
	JSON a = json::Object();
	a[SceneKeys::ClassName] = Actor->GetClass()->GetName();
	a[SceneKeys::ObjectId] = static_cast<int>(Context.RegisterSceneObject(Actor));
	a[SceneKeys::Name] = Actor->GetFName().ToString();
	a[SceneKeys::Properties] = SerializeProperties(Actor, Context);

	// RootComponent 트리 직렬화 — 단, 에셋 참조로 트리를 재구성하는 액터(AUICanvasActor 의 UIAssetPath
	// 등)는 인라인 트리를 기록하지 않는다(ShouldSerializeRootComponentTree=false, 진단 R4).
	if (Actor->ShouldSerializeRootComponentTree() && IsSceneSerializableObject(Actor->GetRootComponent())) {
		a[SceneKeys::RootComponent] = SerializeSceneComponentTree(Actor->GetRootComponent(), Context);
	}

	// Non-scene components
	JSON NonScene = json::Array();
	for (UActorComponent* Comp : Actor->GetComponents()) {
		if (!IsSceneSerializableObject(Comp)) continue;
		if (Comp->IsA<USceneComponent>())
		{
			USceneComponent* SceneComp = static_cast<USceneComponent*>(Comp);
			if (!IsSceneComponentReachableFromRootTree(Actor->GetRootComponent(), SceneComp))
			{
				UE_LOG("[SceneSave] Skipping detached SceneComponent not reachable from RootComponent tree. Actor=%s Component=%s Class=%s",
					Actor->GetName().c_str(),
					SceneComp->GetName().c_str(),
					SceneComp->GetClass()->GetName());
			}
			continue;
		}

		JSON c = json::Object();
		c[SceneKeys::ClassName] = Comp->GetClass()->GetName();
		c[SceneKeys::ObjectId] = static_cast<int>(Context.RegisterSceneObject(Comp));
		c[SceneKeys::Properties] = SerializeProperties(Comp, Context);
		SerializeComponentEditorMetadata(c, Comp);
		NonScene.append(c);
	}
	a[SceneKeys::NonSceneComponents] = NonScene;

	return a;
}

json::JSON FSceneSaveManager::SerializeSceneComponentTree(USceneComponent* Comp, FSceneSaveContext& Context)
{
	using namespace json;
	JSON c = json::Object();
	if (!IsSceneSerializableObject(Comp))
	{
		return c;
	}
	c[SceneKeys::ClassName] = Comp->GetClass()->GetName();
	c[SceneKeys::ObjectId] = static_cast<int>(Context.RegisterSceneObject(Comp));
	c[SceneKeys::Properties] = SerializeProperties(Comp, Context);
	SerializeComponentEditorMetadata(c, Comp);

	JSON Children = json::Array();
	for (USceneComponent* Child : Comp->GetChildren()) {
		if (!IsSceneSerializableObject(Child)) continue;
		Children.append(SerializeSceneComponentTree(Child, Context));
	}
	c[SceneKeys::Children] = Children;

	return c;
}

FString FSceneSaveManager::SerializeUITree(USceneComponent* Root)
{
	// .Scene 임베드(사이클 8)와 동일한 컴포넌트-트리 재귀를 그대로 재사용해 한 서브트리를
	// JSON 으로 만든다. 씬 전체가 아니라 독립 .uasset(UUIAsset) 한 개를 위해 호출되며,
	// 월드/액터 없이 동작한다(저장 방향은 Owner 불필요).
	if (!IsSceneSerializableObject(Root))
	{
		return FString();
	}

	FSceneSaveContext SaveContext;
	json::JSON        Node = SerializeSceneComponentTree(Root, SaveContext);
	return Node.dump();
}

USceneComponent* FSceneSaveManager::DeserializeUITree(const FString& Json, AActor* Owner)
{
	// SerializeUITree 가 만든 JSON 을 라이브 트리로 복원. 씬 로드와 동일한 2-패스:
	//   (1) DeserializeSceneComponentTree 로 컴포넌트 생성 + 트리 부착(프로퍼티는 큐잉)
	//   (2) PendingProperties flush 로 리플렉션 프로퍼티 적용
	// Node 는 큐가 가리키는 동안 살아있어야 하므로 flush 까지 로컬로 유지한다.
	if (Json.empty() || !IsSceneSerializableObject(Owner))
	{
		return nullptr;
	}

	json::JSON         Node = json::JSON::Load(Json);
	FSceneLoadContext  LoadContext;
	USceneComponent*   Root = DeserializeSceneComponentTree(Node, Owner, LoadContext);

	for (FPendingPropertyLoad& Pending : LoadContext.PendingProperties)
	{
		if (IsSceneSerializableObject(Pending.Object) && Pending.Properties)
		{
			DeserializeProperties(Pending.Object, *Pending.Properties, LoadContext);
		}
	}

	return Root;
}

// ============================================================
// Prefab (단일 액터 자산)
// ============================================================

FString FSceneSaveManager::SerializeActorToPrefab(AActor* Actor)
{
	if (!IsSceneSerializableObject(Actor))
	{
		return FString();
	}

	// 참조 보존의 핵심 — 직렬화 전에 액터+모든 컴포넌트의 ObjectId 를 먼저 확정한다.
	// 그래야 프로퍼티 직렬화 중 만나는 컴포넌트 간/액터-컴포넌트 간 참조가 (등록 순서와
	// 무관하게) 이미 할당된 로컬 ObjectId 로 기록된다(전체 씬 저장의 Collect 패스와 동일 원리).
	// 프리팹 밖 외부 객체 참조는 FindObjectId 가 0 을 반환 → 로드 시 null(UE prefab 과 동일).
	FSceneSaveContext SaveContext;
	CollectActorObjectIds(Actor, SaveContext);

	json::JSON Node = SerializeActor(Actor, SaveContext);
	return Node.dump();
}

AActor* FSceneSaveManager::InstantiateActorFromPrefab(const FString& Json, UWorld* World)
{
	using json::JSON;
	if (Json.empty() || !World)
	{
		return nullptr;
	}

	FScopedGarbageCollectionBlocker GCBlocker;

	JSON ActorJSON = JSON::Load(Json);

	const string ActorClass = ActorJSON[SceneKeys::ClassName].ToString();
	UObject*     ActorObj   = FObjectFactory::Get().Create(ActorClass, World);
	if (!ActorObj || !ActorObj->IsA<AActor>())
	{
		return nullptr;
	}
	AActor* Actor = static_cast<AActor*>(ActorObj);

	FSceneLoadContext LoadContext;
	LoadContext.RegisterLoadedObject(ActorJSON, Actor);
	World->AddActor(Actor);

	if (ActorJSON.hasKey(SceneKeys::Name))
	{
		Actor->SetFName(FName(ActorJSON[SceneKeys::Name].ToString()));
	}

	// RootComponent 트리 복원 (Actor 프로퍼티보다 먼저여야 SetActorLocation 등이 적용됨).
	if (ActorJSON.hasKey(SceneKeys::RootComponent))
	{
		JSON&            RootJSON = ActorJSON[SceneKeys::RootComponent];
		USceneComponent* Root     = DeserializeSceneComponentTree(RootJSON, Actor, LoadContext);
		if (Root) Actor->SetRootComponent(Root);
	}

	if (ActorJSON.hasKey(SceneKeys::Properties))
	{
		LoadContext.QueueProperties(Actor, ActorJSON[SceneKeys::Properties]);
	}

	// Non-scene 컴포넌트 복원
	if (ActorJSON.hasKey(SceneKeys::NonSceneComponents))
	{
		for (auto& CompJSON : ActorJSON[SceneKeys::NonSceneComponents].ArrayRange())
		{
			const string CompClass = CompJSON[SceneKeys::ClassName].ToString();
			UObject*     CompObj   = FObjectFactory::Get().Create(CompClass, Actor);
			if (!CompObj || !CompObj->IsA<UActorComponent>()) continue;

			UActorComponent* Comp = static_cast<UActorComponent*>(CompObj);
			LoadContext.RegisterLoadedObject(CompJSON, Comp);
			Actor->RegisterComponent(Comp);

			if (CompJSON.hasKey(SceneKeys::Properties))
			{
				LoadContext.QueueProperties(Comp, CompJSON[SceneKeys::Properties]);
			}
			DeserializeComponentEditorMetadata(Comp, CompJSON);
		}
	}

	// 2-패스: 모든 객체 생성/ObjectId 등록 후 프로퍼티(객체 참조 포함)를 flush 해 참조 재해소.
	for (FPendingPropertyLoad& Pending : LoadContext.PendingProperties)
	{
		if (IsSceneSerializableObject(Pending.Object) && Pending.Properties)
		{
			DeserializeProperties(Pending.Object, *Pending.Properties, LoadContext);
		}
	}

	// 타입별 캐시 컴포넌트 포인터 재연결 + 렌더 상태 재생성 (씬 로드 경로와 동일).
	Actor->PostDuplicate();
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!IsValid(Component)) continue;
		Component->DestroyRenderState();
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			SceneComponent->MarkTransformDirty();
		}
		Component->CreateRenderState();
	}

	World->RemoveActorToOctree(Actor);
	World->InsertActorToOctree(Actor);

	return Actor;
}

FString FSceneSaveManager::MakePrefabPackagePath(const FString& PrefabName)
{
	// 파일명 정제 — 경로/확장자에 부적합한 문자를 '_' 로 치환.
	FString SafeName;
	SafeName.reserve(PrefabName.size());
	for (char C : PrefabName)
	{
		const bool bValid = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
			(C >= '0' && C <= '9') || C == '_' || C == '-';
		SafeName.push_back(bValid ? C : '_');
	}
	if (SafeName.empty()) SafeName = "Prefab";

	// Content/prefab/ 디렉토리 보장.
	const std::filesystem::path Dir = std::filesystem::path(FPaths::RootDir()) / L"Content" / L"prefab";
	FPaths::CreateDir(Dir.wstring());

	const std::filesystem::path Full = Dir / (FPaths::ToWide(SafeName) + L".uasset");
	return FPaths::MakeProjectRelative(FPaths::ToUtf8(Full.generic_wstring()));
}

bool FSceneSaveManager::SaveActorAsPrefab(AActor* Actor, const FString& PackagePath)
{
	if (!IsSceneSerializableObject(Actor))
	{
		return false;
	}

	const FString Json = SerializeActorToPrefab(Actor);
	if (Json.empty())
	{
		return false;
	}

	FString TargetPath = PackagePath;
	if (TargetPath.empty())
	{
		const FString Name = Actor->GetFName().ToString();
		TargetPath = MakePrefabPackagePath(Name.empty() ? FString(Actor->GetClass()->GetName()) : Name);
	}

	const FString        NormalizedPath = FPaths::MakeProjectRelative(TargetPath);
	FAssetImportMetadata Metadata;  // prefab 은 외부 소스 파일이 없음.
	if (!FAssetPackage::SaveStringPayload(NormalizedPath, EAssetPackageType::Prefab, Metadata, Json))
	{
		UE_LOG("[Prefab] Save failed. Path=%s", NormalizedPath.c_str());
		return false;
	}

	UE_LOG("[Prefab] Saved actor '%s' -> %s", Actor->GetName().c_str(), NormalizedPath.c_str());
	return true;
}

AActor* FSceneSaveManager::InstantiatePrefabFromFile(const FString& PackagePath, UWorld* World)
{
	if (!World) return nullptr;

	const FString        NormalizedPath = FPaths::MakeProjectRelative(PackagePath);
	FAssetImportMetadata Metadata;
	FString              Json;
	if (!FAssetPackage::LoadStringPayload(NormalizedPath, EAssetPackageType::Prefab, Metadata, Json))
	{
		UE_LOG("[Prefab] Load failed. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}
	return InstantiateActorFromPrefab(Json, World);
}

json::JSON FSceneSaveManager::SerializeProperties(UObject* Obj, FSceneSaveContext& Context)
{
	using namespace json;
	JSON Props = json::Object();
	if (!IsSceneSerializableObject(Obj)) return Props;

	FSceneJsonSaveArchive Ar(Props, Context);
	Obj->PreSaveForArchive(Ar);
	Obj->SerializeProperties(Ar, PF_Save);
	return Props;
}

// ---- Camera helpers ----

json::JSON FSceneSaveManager::SerializeCamera(const FMinimalViewInfo* POV)
{
	using namespace json;
	JSON cam = json::Object();
	if (!POV) return cam;

	WriteVec3(cam, "Location", POV->Location);
	// FRotator(Pitch, Yaw, Roll) → 직렬화 컨벤션 FVector(Roll, Pitch, Yaw)
	WriteVec3(cam, "Rotation", FVector(POV->Rotation.Roll, POV->Rotation.Pitch, POV->Rotation.Yaw));

	cam["FOV"] = static_cast<double>(POV->FOV);
	cam["NearClip"] = static_cast<double>(POV->NearClip);
	cam["FarClip"] = static_cast<double>(POV->FarClip);

	return cam;
}

void FSceneSaveManager::DeserializeCamera(json::JSON& CameraJSON, FPerspectiveCameraData& OutCam)
{
	using namespace json;
	if (CameraJSON.JSONType() == JSON::Class::Null) return;

	if (CameraJSON.hasKey("Location")) OutCam.Location = ReadVec3(CameraJSON["Location"]);
	if (CameraJSON.hasKey("Rotation")) OutCam.Rotation = ReadVec3(CameraJSON["Rotation"]);
	if (CameraJSON.hasKey("FOV")) {
		auto& Val = CameraJSON["FOV"];
		float fov = static_cast<float>(Val.JSONType() == JSON::Class::Array ? Val[0].ToFloat() : Val.ToFloat());
		// 엔진 내부는 라디안 — π(~3.14)를 넘으면 degree로 간주하고 변환
		if (fov > 3.14159265f) fov *= (3.14159265f / 180.0f);
		OutCam.FOV = fov;
	}
	if (CameraJSON.hasKey("NearClip")) {
		auto& Val = CameraJSON["NearClip"];
		OutCam.NearClip = static_cast<float>(Val.JSONType() == JSON::Class::Array ? Val[0].ToFloat() : Val.ToFloat());
	}
	if (CameraJSON.hasKey("FarClip")) {
		auto& Val = CameraJSON["FarClip"];
		OutCam.FarClip = static_cast<float>(Val.JSONType() == JSON::Class::Array ? Val[0].ToFloat() : Val.ToFloat());
	}
	OutCam.bValid = true;
}

// ============================================================
// Load
// ============================================================

void FSceneSaveManager::LoadSceneFromJSON(const string& filepath, FWorldContext& OutWorldContext, FPerspectiveCameraData& OutCam, const EWorldType* OverrideWorldType)
{
	using json::JSON;
	FScopedGarbageCollectionBlocker GCBlocker;
	std::ifstream File(std::filesystem::path(FPaths::ToWide(filepath)));
	if (!File.is_open()) {
		std::cerr << "Failed to open file at target destination" << std::endl;
		return;
	}

	string FileContent((std::istreambuf_iterator<char>(File)),
		std::istreambuf_iterator<char>());

	JSON root = JSON::Load(FileContent);

	string ClassName = root[SceneKeys::ClassName].ToString();
	ClassName = ClassName.empty() ? "UWorld" : ClassName; // Default to "World" if ClassName is missing
	UObject* WorldObj = FObjectFactory::Get().Create(ClassName);
	if (!WorldObj || !WorldObj->IsA<UWorld>()) return;

	UWorld* World = static_cast<UWorld*>(WorldObj);
	FSceneLoadContext LoadContextState;
	LoadContextState.RegisterLoadedObject(root, World);

	EWorldType WorldType = OverrideWorldType
		? *OverrideWorldType
		: (root.hasKey(SceneKeys::WorldType)
			? StringToWorldType(root[SceneKeys::WorldType].ToString())
			: EWorldType::Editor);

	// World 의 WorldType 을 actor deserialize 전에 적용. Default 가 Editor 라 actor 추가
	// 시 CreateRenderState 의 "EditorOnly && WorldType != Editor" 체크가 잘못 통과돼 Game
	// 빌드에서도 editor billboard SceneProxy 가 만들어지는 버그를 막기 위해.
	World->SetWorldType(WorldType);
	FString ContextName = root.hasKey(SceneKeys::ContextName)
		? root[SceneKeys::ContextName].ToString()
		: "Loaded Scene";
	FString ContextHandle = root.hasKey(SceneKeys::ContextHandle)
		? root[SceneKeys::ContextHandle].ToString()
		: ContextName;

	// WorldSettings — scene 단위 게임 설정. 신규 포맷은 root["WorldSettings"] 객체.
	// 구버전 호환: root["GameMode"] (top-level) 도 fallback 으로 읽음.
	FWorldSettings WorldSettings;
	if (root.hasKey(SceneKeys::WorldSettings))
	{
		JSON& WSObj = root[SceneKeys::WorldSettings];
		if (WSObj.hasKey(SceneKeys::GameMode))
		{
			WorldSettings.GameModeClassName = WSObj[SceneKeys::GameMode].ToString();
		}
		if (WSObj.hasKey(SceneKeys::Gravity) &&
			WSObj[SceneKeys::Gravity].JSONType() == JSON::Class::Array)
		{
			WorldSettings.Gravity = ReadVec3(WSObj[SceneKeys::Gravity]);
		}
	}
	else if (root.hasKey(SceneKeys::GameMode))
	{
		WorldSettings.GameModeClassName = root[SceneKeys::GameMode].ToString();
	}
	World->GetWorldSettings() = WorldSettings;

	World->InitWorld();

	// "PerspectiveCamera" 우선, 구버전 "Camera" 키도 지원
	const char* CamKey = root.hasKey("PerspectiveCamera") ? "PerspectiveCamera"
		: root.hasKey("Camera") ? "Camera"
		: nullptr;
	if (CamKey) {
		JSON& Cam = root[CamKey];
		DeserializeCamera(Cam, OutCam);
	}

	// Deserialize Actors
	if (root.hasKey(SceneKeys::Actors))
	{
		for (auto& ActorJSON : root[SceneKeys::Actors].ArrayRange()) {
			string ActorClass = ActorJSON[SceneKeys::ClassName].ToString();

			UObject* ActorObj = FObjectFactory::Get().Create(ActorClass, World);
			if (!ActorObj || !ActorObj->IsA<AActor>()) continue;
			AActor* Actor = static_cast<AActor*>(ActorObj);
			LoadContextState.RegisterLoadedObject(ActorJSON, Actor);
			World->AddActor(Actor);

			if (ActorJSON.hasKey(SceneKeys::Name)) {
				Actor->SetFName(FName(ActorJSON[SceneKeys::Name].ToString()));
			}

			// RootComponent 트리 복원
			if (ActorJSON.hasKey(SceneKeys::RootComponent)) {
				JSON& RootJSON = ActorJSON[SceneKeys::RootComponent];
				USceneComponent* Root = DeserializeSceneComponentTree(RootJSON, Actor, LoadContextState);
				if (Root) Actor->SetRootComponent(Root);
			}

			// Actor 프로퍼티(Location/Rotation/Scale/Visible 및 서브클래스 추가 항목)
			// 복원 — RootComponent 복원 뒤여야 SetActorLocation 등이 적용됨.
			if (ActorJSON.hasKey(SceneKeys::Properties)) {
				LoadContextState.QueueProperties(Actor, ActorJSON[SceneKeys::Properties]);
			}

			// Non-scene components 복원
			if (ActorJSON.hasKey(SceneKeys::NonSceneComponents)) {
				for (auto& CompJSON : ActorJSON[SceneKeys::NonSceneComponents].ArrayRange()) {
					string CompClass = CompJSON[SceneKeys::ClassName].ToString();
					UObject* CompObj = FObjectFactory::Get().Create(CompClass, Actor);
					if (!CompObj || !CompObj->IsA<UActorComponent>()) continue;

					UActorComponent* Comp = static_cast<UActorComponent*>(CompObj);
					LoadContextState.RegisterLoadedObject(CompJSON, Comp);
					Actor->RegisterComponent(Comp);

					if (CompJSON.hasKey(SceneKeys::Properties)) {
						JSON& PropsJSON = CompJSON[SceneKeys::Properties];
						LoadContextState.QueueProperties(Comp, PropsJSON);
					}
					DeserializeComponentEditorMetadata(Comp, CompJSON);
				}
			}
		}
	}

	for (FPendingPropertyLoad& Pending : LoadContextState.PendingProperties)
	{
		if (IsSceneSerializableObject(Pending.Object) && Pending.Properties)
		{
			DeserializeProperties(Pending.Object, *Pending.Properties, LoadContextState);
		}
	}

	// Components are registered while the object graph is being created so object-id
	// references can be resolved. Some render resources, however, depend on properties
	// that are only applied in the deferred pass above. Rebuild render state once after
	// all reflected properties/PostLoad fixups are complete.
	for (AActor* Actor : World->GetActors())
	{
		if (!IsValid(Actor))
		{
			continue;
		}
		// 캐시 컴포넌트 포인터 재연결 — load 경로는 InitDefaultComponents/PostDuplicate 를
		// 거치지 않으므로, deserialize 로 복원된 컴포넌트 트리에서 액터의 타입별 캐시
		// (StaticMeshComponent / Mesh / CapsuleComponent 등) 를 다시 잡아준다. duplicate 경로
		// (AActor::Duplicate)와 동일한 PostDuplicate 를 호출. 이 시점엔 RootComponent 와 모든
		// 컴포넌트가 복원 완료라 GetRootComponent()/GetComponentByClass() 기반 재발견이 안전하다.
		Actor->PostDuplicate();

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!IsValid(Component))
			{
				continue;
			}

			Component->DestroyRenderState();
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				SceneComponent->MarkTransformDirty();
			}
			Component->CreateRenderState();
		}
	}

	for (AActor* Actor : World->GetActors())
	{
		if (!IsSceneSerializableObject(Actor))
		{
			continue;
		}

		World->RemoveActorToOctree(Actor);
		World->InsertActorToOctree(Actor);
	}

	OutWorldContext.WorldType = WorldType;
	OutWorldContext.World = World;
	OutWorldContext.ContextName = ContextName;
	OutWorldContext.ContextHandle = FName(ContextHandle);
}

USceneComponent* FSceneSaveManager::DeserializeSceneComponentTree(json::JSON& Node, AActor* Owner, FSceneLoadContext& Context)
{
	if (!IsSceneSerializableObject(Owner))
	{
		return nullptr;
	}

	string ClassName = Node[SceneKeys::ClassName].ToString();
	UObject* Obj = FObjectFactory::Get().Create(ClassName, Owner);
	if (!IsSceneSerializableObject(Obj) || !Obj->IsA<USceneComponent>()) return nullptr;

	USceneComponent* Comp = static_cast<USceneComponent*>(Obj);
	Context.RegisterLoadedObject(Node, Comp);
	Owner->RegisterComponent(Comp);

	// Restore properties
	if (Node.hasKey(SceneKeys::Properties)) {
		json::JSON& PropsJSON = Node[SceneKeys::Properties];
		Context.QueueProperties(Comp, PropsJSON);
	}
	DeserializeComponentEditorMetadata(Comp, Node);
	Comp->MarkTransformDirty();

	// Restore children recursively
	if (Node.hasKey(SceneKeys::Children)) {
		for (auto& ChildJSON : Node[SceneKeys::Children].ArrayRange()) {
			USceneComponent* Child = DeserializeSceneComponentTree(ChildJSON, Owner, Context);
			if (Child) {
				Child->AttachToComponent(Comp);
			}
		}
	}

	EnsureEditorBillboardMetadata(Comp);

	return Comp;
}

void FSceneSaveManager::DeserializeProperties(UObject* Obj, json::JSON& PropsJSON, FSceneLoadContext& Context)
{
	if (!IsSceneSerializableObject(Obj)) return;

	TArray<const FProperty*> Properties;
	Obj->GetClass()->GetPropertyRefs(Properties);
	for (const FProperty* Property : Properties)
	{
		if(!Property || (Property->Flags & PF_Save) == 0)
		{
			continue;
		}

		const char* PropertyKey = Property->Name;
		if (!PropsJSON.hasKey(PropertyKey) && Property->DisplayName && PropsJSON.hasKey(Property->DisplayName))
		{
			PropertyKey = Property->DisplayName;
		}

		if (!PropsJSON.hasKey(PropertyKey))
		{
			continue;
		}

		if (PropertyKey != Property->Name)
		{
			PropsJSON[Property->Name] = PropsJSON[PropertyKey];
		}
	}

	for (const FProperty* Property : Properties)
	{
		if(!Property || (Property->Flags & PF_Save) == 0)
		{
			continue;
		}

		const char* PropertyKey = Property->Name;
		if (!PropsJSON.hasKey(PropertyKey))
		{
			continue;
		}

		if(!Property->GetValuePtrFor(Obj))
		{
			continue;
		}

		FSceneJsonLoadArchive Ar(PropsJSON[PropertyKey], Context);
		Property->Serialize(Obj, Ar);

		FPropertyChangedEvent Event;
		Event.Object = Obj;
		Event.Property = Property;
		Event.PropertyName = Property->Name;
		Event.DisplayName = Property->DisplayName ? Property->DisplayName : Property->Name;
		Event.PropertyPath = Property->Name;
		Event.Type = Property->GetType();
		Event.ChangeType = EPropertyChangeType::Load;
		Obj->PostEditChangeProperty(Event);
	}

	FSceneJsonLoadArchive PostLoadArchive(PropsJSON, Context);
	Obj->PostLoadFromArchive(PostLoadArchive);
}

// ============================================================
// Utility
// ============================================================

string FSceneSaveManager::GetCurrentTimeStamp()
{
	std::time_t t = std::time(nullptr);
	std::tm tm{};
	localtime_s(&tm, &t);

	char buf[20];
	std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
	return buf;
}

TArray<FString> FSceneSaveManager::GetSceneFileList()
{
	TArray<FString> Result;
	std::wstring SceneDir = GetSceneDirectory();
	if (!std::filesystem::exists(SceneDir))
	{
		return Result;
	}

	for (auto& Entry : std::filesystem::directory_iterator(SceneDir))
	{
		if (Entry.is_regular_file() && Entry.path().extension() == SceneExtension)
		{
			Result.push_back(FPaths::ToUtf8(Entry.path().stem().wstring()));
		}
	}
	return Result;
}
