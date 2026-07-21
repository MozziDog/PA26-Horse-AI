#include "LuaActorBindings.internal.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particle/Distributions/DistributionFloatConstant.h"

using namespace LuaActorBindingsDetail;

namespace
{
    bool GetCameraProjectileFrame(UWorld* W, FVector& OutOrigin, FRotator& OutRotation)
    {
        if (!W) return false;

        FMinimalViewInfo POV;
        if (!W->GetActivePOV(POV)) return false;

        OutOrigin = POV.Location;
        OutRotation = POV.Rotation;
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            if (APawn* Pawn = PC->GetPossessedPawn())
            {
                OutOrigin = Pawn->GetActorLocation();
            }
        }
        return true;
    }

    FVector ComputeCameraProjectileLocation(const FVector& Origin, const FRotator& Rotation, const FVector& Offset)
    {
        return Origin
            + Rotation.GetForwardVector() * Offset.X
            + Rotation.GetRightVector() * Offset.Y
            + Rotation.GetUpVector() * Offset.Z;
    }

    APlayerCameraManager* GetFirstPlayerCameraManager()
    {
        UWorld* W = GEngine ? GEngine->GetWorld() : nullptr;
        APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
        return PC ? PC->GetPlayerCameraManager() : nullptr;
    }

    FLinearColor ToLinearColor(const FVector4& Color)
    {
        return FLinearColor(Color.X, Color.Y, Color.Z, Color.W);
    }
}

void FLuaScriptManager::RegisterActorBindings_6(sol::state& Lua)
{
    sol::table World = Lua.create_named_table("World");
    World.set_function(
        "GetFirstPlayerController",
        []() -> APlayerController*
        {
            return (GEngine && GEngine->GetWorld()) ? GEngine->GetWorld()->GetFirstPlayerController() : nullptr;
        }
    );
    World.set_function(
        "SpawnActor",
        [](const FString& ClassName, sol::optional<FVector> Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale) -> AActor*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls) return nullptr;
            AActor* Actor = W->SpawnActorByClass(Cls);
            if (IsValid(Actor))
            {
                Actor->SetActorLocation(Location.value_or(FVector(0, 0, 0)));
                Actor->SetActorRotation(Rotation.value_or(FVector(0, 0, 0)));
                Actor->SetActorScale(Scale.value_or(FVector(1, 1, 1)));
            }
            return Actor;
        }
    );
    World.set_function(
        "SpawnEmitterAtLocation",
        [](const FString& TemplatePath, const FVector& Location, sol::optional<FVector> Rotation, sol::optional<bool> bActivate) -> UParticleSystemComponent*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            return FGameplayStatics::SpawnEmitterAtLocation(
                W,
                TemplatePath,
                Location,
                FRotator(Rotation.value_or(FVector(0, 0, 0))),
                bActivate.value_or(true)
            );
        }
    );
    World.set_function(
        "GetCameraProjectileLocation",
        [](sol::optional<FVector> SpawnOffset) -> FVector
        {
            if (!GEngine) return FVector::ZeroVector;
            UWorld* W = GEngine->GetWorld();
            if (!W) return FVector::ZeroVector;

            FVector Origin;
            FRotator Rotation;
            if (!GetCameraProjectileFrame(W, Origin, Rotation)) return FVector::ZeroVector;

            return ComputeCameraProjectileLocation(
                Origin,
                Rotation,
                SpawnOffset.value_or(FVector(1.0f, 0.0f, 1.0f)));
        }
    );
    World.set_function(
        "GetCameraProjectileForward",
        []() -> FVector
        {
            if (!GEngine) return FVector::ForwardVector;
            UWorld* W = GEngine->GetWorld();
            if (!W) return FVector::ForwardVector;

            FVector Origin;
            FRotator Rotation;
            if (!GetCameraProjectileFrame(W, Origin, Rotation)) return FVector::ForwardVector;
            return Rotation.GetForwardVector();
        }
    );
    // 전역 빔 크기 — 공유 template(Beam.uasset)의 굵기(WidthDistribution)·길이(DistanceDistribution)
    // 상수를 직접 수정한다. 같은 경로를 쓰는 모든 빔에 즉시 반영(세션 한정, 영구 저장 X).
    // width/distance 가 0 이하면 해당 항목은 건드리지 않는다. Speed 는 지정되면(0 포함) 적용한다.
    World.set_function(
        "SetBeamTemplateSize",
        [](const FString& Path, float Width, float Distance, sol::optional<float> Speed) -> bool
        {
            UParticleSystem* PS = FParticleSystemManager::Get().Load(Path);
            if (!PS) return false;

            UParticleEmitter* Emitter = PS->GetEmitter(0);
            if (!Emitter) return false;
            UParticleLODLevel* LOD = Emitter->GetCurrentLODLevel(0);
            if (!LOD) return false;
            UParticleModuleTypeDataBeam* Beam = Cast<UParticleModuleTypeDataBeam>(LOD->TypeDataModule);
            if (!Beam) return false;

            bool bChanged = false;
            if (Width > 0.0f)
            {
                if (UDistributionFloatConstant* W = Cast<UDistributionFloatConstant>(Beam->WidthDistribution))
                {
                    W->Constant = Width;
                    bChanged = true;
                }
                Beam->Width = Width;   // legacy fallback (WidthDistribution 없을 때 EvaluateWidth 가 사용)
            }
            if (Distance > 0.0f)
            {
                if (UDistributionFloatConstant* D = Cast<UDistributionFloatConstant>(Beam->DistanceDistribution))
                {
                    D->Constant = Distance;
                    bChanged = true;
                }
                Beam->Distance = Distance; // legacy fallback
            }
            if (Speed.has_value())   // 0 도 유효(즉시 연결)하므로 optional 로 "지정되면 적용".
            {
                Beam->Speed = (*Speed > 0.0f) ? *Speed : 0.0f;
                bChanged = true;
            }
            return bChanged;
        }
    );
    World.set_function(
        "SetCameraVignetteColor",
        [](float Intensity, float Radius, float Softness, sol::optional<sol::object> Color) -> bool
        {
            APlayerCameraManager* Manager = GetFirstPlayerCameraManager();
            if (!Manager) return false;

            FVector4 ColorValue(0.45f, 0.02f, 0.75f, 1.0f);
            if (Color && !LuaObjectToVector4(Color.value(), ColorValue))
            {
                ColorValue = FVector4(0.45f, 0.02f, 0.75f, 1.0f);
            }
            Manager->SetCameraVignette(Intensity, Radius, Softness, ToLinearColor(ColorValue));
            return true;
        }
    );
    World.set_function(
        "ClearCameraVignette",
        []() -> bool
        {
            APlayerCameraManager* Manager = GetFirstPlayerCameraManager();
            if (!Manager) return false;
            Manager->ClearCameraVignette();
            return true;
        }
    );
    World.set_function(
        "SetCameraRadialBlur",
        [](float Intensity, float Radius, sol::optional<int32> SampleCount,
            sol::optional<float> CenterX, sol::optional<float> CenterY) -> bool
        {
            APlayerCameraManager* Manager = GetFirstPlayerCameraManager();
            if (!Manager) return false;
            Manager->SetRadialBlur(
                Intensity,
                Radius,
                SampleCount.value_or(12),
                FVector2(CenterX.value_or(0.5f), CenterY.value_or(0.5f)));
            return true;
        }
    );
    World.set_function(
        "ClearCameraRadialBlur",
        []() -> bool
        {
            APlayerCameraManager* Manager = GetFirstPlayerCameraManager();
            if (!Manager) return false;
            Manager->ClearRadialBlur();
            return true;
        }
    );
    World.set_function(
        "SetManualCameraFadeColor",
        [](float Amount, sol::optional<sol::object> Color) -> bool
        {
            APlayerCameraManager* Manager = GetFirstPlayerCameraManager();
            if (!Manager) return false;

            FVector4 ColorValue(0.35f, 0.0f, 0.55f, 1.0f);
            if (Color && !LuaObjectToVector4(Color.value(), ColorValue))
            {
                ColorValue = FVector4(0.35f, 0.0f, 0.55f, 1.0f);
            }
            Manager->SetManualCameraFade(Amount, ToLinearColor(ColorValue), false);
            return true;
        }
    );
    World.set_function(
        "ClearCameraFade",
        []() -> bool
        {
            APlayerCameraManager* Manager = GetFirstPlayerCameraManager();
            if (!Manager) return false;
            Manager->StopCameraFade();
            return true;
        }
    );
    World.set_function(
        "SpawnPawn",
        [](const FString& ClassName, sol::optional<FVector> Location, sol::optional<FVector> Rotation, sol::optional<FVector> Scale, sol::optional<bool> bPossess) -> APawn*
        {
            if (!GEngine) return nullptr;
            UWorld* W = GEngine->GetWorld();
            if (!W) return nullptr;
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls) return nullptr;
            AActor* Actor = W->SpawnActorByClass(Cls);
            APawn*  Pawn  = Cast<APawn>(Actor);
            if (!IsValid(Pawn))
            {
                if (IsValid(Actor)) W->DestroyActor(Actor);
                return nullptr;
            }
            Pawn->SetActorLocation(Location.value_or(FVector(0, 0, 0)));
            Pawn->SetActorRotation(Rotation.value_or(FVector(0, 0, 0)));
            Pawn->SetActorScale(Scale.value_or(FVector(1, 1, 1)));
            if (bPossess.value_or(false))
            {
                if (APlayerController* PC = W->GetFirstPlayerController()) PC->Possess(Pawn);
            }
            return Pawn;
        }
    );
    World.set_function(
        "FindActorByName",
        [](const FString& ActorName) -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld()) return nullptr;
            UWorld* W = GEngine->GetWorld();
            for (AActor* Actor : W->GetActors())
            {
                if (IsValid(Actor) && Actor->GetFName().ToString() == ActorName)
                {
                    return Actor;
                }
            }
            return nullptr;
        }
    );
    World.set_function(
        "FindFirstActorByClass",
        [](const FString& ClassName) -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld()) return nullptr;
            UWorld* W   = GEngine->GetWorld();
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls) return nullptr;
            for (AActor* Actor : W->GetActors())
            {
                if (IsValid(Actor) && Actor->GetClass()->IsA(Cls))
                {
                    return Actor;
                }
            }
            return nullptr;
        }
    );
    World.set_function(
        "FindFirstActorByTag",
        [](const FString& Tag) -> AActor*
        {
            return FGameplayStatics::FindFirstActorByTag(
                GEngine ? GEngine->GetWorld() : nullptr,
                FName(Tag)
            );
        }
    );
    World.set_function(
        "FindActorsByTag",
        [](const FString& Tag) -> sol::table
        {
            sol::table            Result = FLuaScriptManager::GetState().create_table();
            const TArray<AActor*> Found  = FGameplayStatics::FindActorsByTag(
                GEngine ? GEngine->GetWorld() : nullptr,
                FName(Tag)
            );
            int Idx = 1; // Lua arrays are 1-indexed
            for (AActor* Actor : Found)
            {
                Result[Idx++] = Actor;
            }
            return Result;
        }
    );
    // LuaBlueprint ForEachActorByClass 노드용 — 동일 패턴(table 반환)으로 노출.
    World.set_function(
        "FindActorsByClass",
        [](const FString& ClassName) -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            if (!GEngine || !GEngine->GetWorld()) return Result;
            UClass* Cls = UClass::FindByName(ClassName.c_str());
            if (!Cls)
            {
                static TSet<FString> WarnedUnknownClasses;
                if (WarnedUnknownClasses.find(ClassName) == WarnedUnknownClasses.end())
                {
                    WarnedUnknownClasses.insert(ClassName);
                    UE_LOG(
                        "World.FindActorsByClass: 등록되지 않은 액터 클래스 '%s' — 빈 리스트 반환 "
                        "(클래스 이름 오타/미설정 확인)",
                        ClassName.c_str()
                    );
                }
                return Result;
            }
            int Idx = 1;
            for (AActor* Actor : GEngine->GetWorld()->GetActors())
            {
                if (IsValid(Actor) && Actor->GetClass() && Actor->GetClass()->IsA(Cls))
                {
                    Result[Idx++] = Actor;
                }
            }
            return Result;
        }
    );
    World.set_function(
        "GetGameTime",
        []() -> float
        {
            UWorld* CurrentWorld = GEngine ? GEngine->GetWorld() : nullptr;
            return CurrentWorld ? CurrentWorld->GetGameTimeSeconds() : 0.0f;
        }
    );
    World.set_function(
        "LineTrace",
        [](const FVector& Start, const FVector& End, sol::optional<AActor*> IgnoreActor) -> sol::table
        {
            sol::table Result   = FLuaScriptManager::GetState().create_table();
            Result["Hit"]       = false;
            Result["Actor"]     = static_cast<AActor*>(nullptr);
            Result["Component"] = static_cast<UPrimitiveComponent*>(nullptr);
            Result["Location"]  = FVector(0, 0, 0);
            Result["Normal"]    = FVector(0, 0, 0);
            Result["Distance"]  = 0.0f;

            UWorld* CurrentWorld = GEngine ? GEngine->GetWorld() : nullptr;
            if (!CurrentWorld)
            {
                return Result;
            }

            FVector     Delta       = End - Start;
            const float MaxDistance = Delta.Length();
            if (MaxDistance <= 0.0001f)
            {
                return Result;
            }
            const FVector Direction = Delta / MaxDistance;
            FHitResult    Hit;
            if (CurrentWorld->PhysicsRaycast(Start, Direction, MaxDistance, Hit, ECollisionChannel::WorldStatic, IgnoreActor.value_or(nullptr)))
            {
                Result["Hit"]       = true;
                Result["Actor"]     = Hit.HitActor;
                Result["Component"] = Hit.HitComponent;
                Result["Location"]  = Hit.WorldHitLocation;
                Result["Normal"]    = Hit.WorldNormal;
                Result["Distance"]  = Hit.Distance;
            }
            return Result;
        }
    );
}
