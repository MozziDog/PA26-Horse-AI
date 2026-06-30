#include "AnimationManager.h"
#include "Object/GarbageCollection.h"

#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Montage/AnimMontage.h"
#include "Mesh/Importer/FbxImporter.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Sequence/AnimDataModel.h"
#include "Asset/AssetPackage.h"
#include "Core/Logging/Log.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

namespace
{
    static std::filesystem::path ResolveProjectPath(const FString& Path)
    {
        std::filesystem::path FullPath(FPaths::ToWide(Path));
        if (!FullPath.is_absolute())
        {
            FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
        }
        return FullPath.lexically_normal();
    }

    static bool TryGetSourceFileState(const FString& SourcePath, uint64& OutTimestamp, uint64& OutFileSize)
    {
        std::filesystem::path FullPath = ResolveProjectPath(SourcePath);

        if (!std::filesystem::exists(FullPath) || !std::filesystem::is_regular_file(FullPath))
        {
            OutTimestamp = 0;
            OutFileSize  = 0;
            return false;
        }

        OutFileSize          = static_cast<uint64>(std::filesystem::file_size(FullPath));
        const auto WriteTime = std::filesystem::last_write_time(FullPath);
        OutTimestamp         = static_cast<uint64>(WriteTime.time_since_epoch().count());
        return true;
    }

    static FAssetImportMetadata MakeImportMetadata(const FString& SourcePath)
    {
        FAssetImportMetadata Metadata;
        Metadata.SourcePath = FPaths::MakeProjectRelative(SourcePath);
        TryGetSourceFileState(SourcePath, Metadata.SourceTimestamp, Metadata.SourceFileSize);
        return Metadata;
    }

    static FString SanitizeAssetName(const FString& Name)
    {
        FString Result = Name;

        for (char& C : Result)
        {
            const bool bValid = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-';

            if (!bValid)
            {
                C = '_';
            }
        }

        if (Result.empty())
        {
            Result = "Anim";
        }

        return Result;
    }
}

FAnimationManager& FAnimationManager::Get()
{
    static FAnimationManager Instance;
    return Instance;
}

FString FAnimationManager::GetAnimationPath(const FString& SourcePath, const FString& AnimationName)
{
    std::filesystem::path ProjectRelative = std::filesystem::path(FPaths::ToWide(FPaths::MakeProjectRelative(SourcePath))).lexically_normal();

    const FString SafeAnimName = SanitizeAssetName(AnimationName);

    // SourcePath 가 이미 Content/ 하위면 그대로 — 그렇지 않으면 (구 Data/ root 호환) prefix.
    std::filesystem::path AssetPath = (!ProjectRelative.empty() && ProjectRelative.begin()->wstring() == L"Content")
        ? ProjectRelative
        : (std::filesystem::path(L"Content") / ProjectRelative);
    AssetPath.replace_filename(AssetPath.stem().wstring() + L"_" + FPaths::ToWide(SafeAnimName) + L".uasset");

    std::filesystem::path FullAssetPath = std::filesystem::path(FPaths::RootDir()) / AssetPath;
    FPaths::CreateDir(FullAssetPath.parent_path().wstring());

    return FPaths::ToUtf8(AssetPath.generic_wstring());
}

FString FAnimationManager::GetAnimationPathForSkeleton(const FString& SourcePath, const FString& AnimationName, const FString& TargetSkeletonPath)
{
    std::filesystem::path SourceRel   = std::filesystem::path(FPaths::ToWide(FPaths::MakeProjectRelative(SourcePath))).lexically_normal();
    std::filesystem::path SkeletonRel = std::filesystem::path(FPaths::ToWide(FPaths::MakeProjectRelative(TargetSkeletonPath))).lexically_normal();

    const FString SafeAnimName = SanitizeAssetName(AnimationName);

    std::filesystem::path AssetDir = SkeletonRel.parent_path();
    if (AssetDir.empty())
    {
        AssetDir = std::filesystem::path(L"Content");
    }

    const std::wstring    BaseName  = SourceRel.stem().wstring() + L"_" + FPaths::ToWide(SafeAnimName) + L".uasset";
    std::filesystem::path AssetPath = AssetDir / BaseName;

    std::filesystem::path FullAssetPath = std::filesystem::path(FPaths::RootDir()) / AssetPath;
    FPaths::CreateDir(FullAssetPath.parent_path().wstring());

    return FPaths::ToUtf8(AssetPath.generic_wstring());
}

namespace
{
    static bool RemapAnimSequenceToTargetSkeleton(
        UAnimSequence*                Sequence,
        const USkeleton*              TargetSkeleton,
        const FSkeletonBoneRemap&     Remap,
        FSkeletonCompatibilityReport* OutReport = nullptr
        )
    {
        if (!Sequence || !TargetSkeleton || !Sequence->GetDataModel())
        {
            if (OutReport)
            {
                OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
                OutReport->Reason = "null animation sequence, data model, or target skeleton";
            }
            return false;
        }

        UAnimDataModel*           DataModel = Sequence->GetDataModel();
        const FReferenceSkeleton& TargetRef = TargetSkeleton->GetReferenceSkeleton();

        for (FBoneAnimationTrack& Track : DataModel->GetMutableBoneAnimationTracks())
        {
            int32 TargetBoneIndex = -1;

            if (Track.BoneTreeIndex >= 0)
            {
                TargetBoneIndex = Remap.GetTargetBoneIndex(Track.BoneTreeIndex);
            }

            if (TargetBoneIndex < 0 && !Track.BoneName.empty())
            {
                TargetBoneIndex = TargetSkeleton->FindBoneIndex(Track.BoneName);
            }

            if (TargetBoneIndex < 0 || TargetBoneIndex >= TargetRef.GetNumBones())
            {
                if (OutReport)
                {
                    OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
                    OutReport->Reason = "animation track references an unmapped bone";
                    OutReport->MissingBones.push_back(Track.BoneName);
                }
                return false;
            }

            Track.BoneTreeIndex = TargetBoneIndex;
            Track.BoneName      = TargetRef.Bones[TargetBoneIndex].Name;
        }

        Sequence->SetSkeletonBinding(TargetSkeleton->GetSkeletonBinding());
        return true;
    }
}

UAnimSequence* FAnimationManager::LoadAnimation(const FString& PackagePath)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(PackagePath);

    auto It = AnimationCaches.find(NormalizedPath);
    if (It != AnimationCaches.end())
    {
        if (IsValid(It->second))
        {
            return It->second;
        }
        AnimationCaches.erase(It);
    }

    FWindowsBinReader Reader(NormalizedPath);
    if (!Reader.IsValid())
    {
        UE_LOG("Animation load failed: could not open file. Path=%s", NormalizedPath.c_str());
        return nullptr;
    }

    FAssetPackageHeader Header;
    FAssetImportMetadata Metadata;
    if (!FAssetPackage::ReadPackagePrelude(Reader, EAssetPackageType::AnimSequence, Header, Metadata))
    {
        UE_LOG("Animation load failed: invalid package header. Path=%s", NormalizedPath.c_str());
        return nullptr;
    }

    UAnimSequence* Sequence = UObjectManager::Get().CreateObject<UAnimSequence>();
    Sequence->Serialize(Reader);
    Sequence->SetAssetPathFileName(NormalizedPath);

    if (!Reader.IsValid())
    {
        UE_LOG("Animation load failed: corrupted package. Path=%s", NormalizedPath.c_str());
        UObjectManager::Get().DestroyObject(Sequence);
        return nullptr;
    }



    auto ListIt = std::find_if(
        AvailableAnimationFiles.begin(),
        AvailableAnimationFiles.end(),
        [&](const FAssetListItem& Item)
        {
            return Item.FullPath == NormalizedPath;
        }
    );

    if (ListIt == AvailableAnimationFiles.end())
    {
        FAssetListItem Item;
        Item.DisplayName = Sequence->GetName();
        Item.FullPath    = NormalizedPath;
        AvailableAnimationFiles.push_back(Item);
    }

    AnimationCaches[NormalizedPath] = Sequence;
    return Sequence;
}

bool FAnimationManager::SaveAnimation(UAnimSequence* Sequence, const FString& PackagePath, const FString& SourcePath)
{
    if (!Sequence)
    {
        return false;
    }

    const FString NormalizedPath = FPaths::MakeProjectRelative(PackagePath);
    Sequence->SetAssetPathFileName(NormalizedPath);

    FWindowsBinWriter Writer(NormalizedPath);
    if (!Writer.IsValid())
    {
        UE_LOG("Animation save failed: could not open file. Path=%s", NormalizedPath.c_str());
        return false;
    }

    FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);

    if (!FAssetPackage::WritePackagePrelude(Writer, EAssetPackageType::AnimSequence, Metadata))
    {
        UE_LOG("Animation save failed: package prelude write failed. Path=%s", NormalizedPath.c_str());
        return false;
    }
    Sequence->Serialize(Writer);

    if (!Writer.IsValid())
    {
        UE_LOG("Animation save failed: write failed. Path=%s", NormalizedPath.c_str());
        return false;
    }

    auto ListIt = std::find_if(
        AvailableAnimationFiles.begin(),
        AvailableAnimationFiles.end(),
        [&](const FAssetListItem& Item)
        {
            return Item.FullPath == NormalizedPath;
        }
    );

    if (ListIt == AvailableAnimationFiles.end())
    {
        FAssetListItem Item;
        Item.DisplayName = Sequence->GetName();
        Item.FullPath    = NormalizedPath;
        AvailableAnimationFiles.push_back(Item);
    }

    AnimationCaches[NormalizedPath] = Sequence;
    return true;
}

bool FAnimationManager::SaveAnimationPreservingMetadata(UAnimSequence* Sequence)
{
    if (!Sequence)
    {
        return false;
    }

    const FString& AssetPath = Sequence->GetAssetPathFileName();
    if (AssetPath.empty() || AssetPath == "None")
    {
        UE_LOG("Animation save failed: asset path is unknown. Anim=%s", Sequence->GetName().c_str());
        return false;
    }

    // 기존 메타데이터에서 SourcePath 추출 — 옵션 변경만으로 원본 reference 잃지 않도록.
    FAssetImportMetadata ExistingMeta;
    FAssetPackage::ReadMetadata(AssetPath, EAssetPackageType::AnimSequence, ExistingMeta);

    return SaveAnimation(Sequence, AssetPath, ExistingMeta.SourcePath);
}

bool FAnimationManager::ImportAnimationForSkeleton(const FAnimationImportRequest& Request, TArray<UAnimSequence*>* OutSequences, FString* OutError)
{
    if (OutSequences)
    {
        OutSequences->clear();
    }

    if (Request.SourceFbxPath.empty() || Request.TargetSkeletonPath.empty() || Request.TargetSkeletonPath == "None")
    {
        UE_LOG(
            "Animation import failed: source FBX and target skeleton are required. Source=%s Target=%s",
            Request.SourceFbxPath.c_str(),
            Request.TargetSkeletonPath.c_str()
        );
        if (OutError) *OutError = "Source FBX and target skeleton are required.";
        return false;
    }

    FFbxAnimationImportOptions ImportOptions;
    ImportOptions.SelectedStackIndices = Request.SelectedAnimationStackIndices;

    FFbxAnimationImportResult ImportResult;
    FString                   FbxMessage;
    if (!FFbxImporter::ImportAnimationOnly(Request.SourceFbxPath, ImportResult, &ImportOptions, &FbxMessage))
    {
        UE_LOG("Animation import failed: FBX animation-only import failed. Source=%s", Request.SourceFbxPath.c_str());
        if (OutError) *OutError = FbxMessage.empty() ? "FBX animation-only import failed." : FbxMessage;
        return false;
    }

    return SaveImportedAnimationsForSkeleton(
        Request.SourceFbxPath,
        ImportResult.SourceSkeleton,
        Request.TargetSkeletonPath,
        Request.DestinationDirectory,
        Request.bAllowTargetExtraBones,
        Request.bOverwriteExistingAssets,
        ImportResult.AnimSequences,
        OutSequences,
        OutError
    );
}

bool FAnimationManager::SaveImportedAnimationsForSkeleton(
    const FString&            SourceFbxPath,
    const FReferenceSkeleton& SourceSkeleton,
    const FString&            TargetSkeletonPath,
    const FString&            DestinationDirectory,
    bool                      bAllowTargetExtraBones,
    bool                      bOverwriteExistingAssets,
    TArray<UAnimSequence*>&   ImportedSequences,
    TArray<UAnimSequence*>*   OutSequences,
    FString*                  OutError
    )
{
    if (OutSequences)
    {
        OutSequences->clear();
    }

    if (SourceFbxPath.empty() || TargetSkeletonPath.empty() || TargetSkeletonPath == "None")
    {
        UE_LOG(
            "Animation save failed: source FBX and target skeleton are required. Source=%s Target=%s",
            SourceFbxPath.c_str(),
            TargetSkeletonPath.c_str()
        );
        if (OutError) *OutError = "Source FBX and target skeleton are required.";
        return false;
    }

    USkeleton* TargetSkeleton = FSkeletonManager::Get().LoadSkeleton(TargetSkeletonPath);
    if (!TargetSkeleton)
    {
        UE_LOG("Animation save failed: target skeleton not found. Path=%s", TargetSkeletonPath.c_str());
        if (OutError) *OutError = "Target skeleton not found: " + TargetSkeletonPath;
        return false;
    }

    FSkeletonBoneRemap           Remap;
    FSkeletonCompatibilityReport Report;
    if (!FSkeletonManager::BuildBoneRemapByName(
        SourceSkeleton,
        TargetSkeleton->GetReferenceSkeleton(),
        Remap,
        &Report,
        !bAllowTargetExtraBones
    ))
    {
        UE_LOG(
            "Animation save failed: skeleton remap failed. Source=%s Target=%s Reason=%s",
            SourceFbxPath.c_str(),
            TargetSkeletonPath.c_str(),
            Report.Reason.c_str()
        );
        if (OutError)
        {
            *OutError = "Skeleton mismatch — " + Report.Reason + ".";
            if (!Report.MissingBones.empty())
            {
                *OutError += " Missing in target: " + Report.MissingBones[0];
                if (Report.MissingBones.size() > 1)
                {
                    *OutError += " (+" + std::to_string(Report.MissingBones.size() - 1) + " more)";
                }
            }
            else if (!Report.ParentMismatchBones.empty())
            {
                *OutError += " Bone: " + Report.ParentMismatchBones[0];
            }
            else if (!Report.ExtraBones.empty())
            {
                *OutError += " Extra/uncovered: " + Report.ExtraBones[0];
            }
            if (!bAllowTargetExtraBones)
            {
                *OutError += "  (Tip: enable \"Allow target skeleton extra bones\".)";
            }
        }
        return false;
    }

    bool bSavedAny = false;
    for (UAnimSequence* Sequence : ImportedSequences)
    {
        if (!Sequence)
        {
            continue;
        }

        if (!RemapAnimSequenceToTargetSkeleton(Sequence, TargetSkeleton, Remap, &Report))
        {
            UE_LOG("Animation save failed: sequence remap failed. Anim=%s Reason=%s", Sequence->GetName().c_str(), Report.Reason.c_str());
            if (OutError) *OutError = "Sequence remap failed (" + Sequence->GetName() + ") — " + Report.Reason + ".";
            return false;
        }

        FString AnimPath;
        if (!DestinationDirectory.empty())
        {
            std::filesystem::path Dir(FPaths::ToWide(FPaths::MakeProjectRelative(DestinationDirectory)));
            std::filesystem::path SourceRel(FPaths::ToWide(FPaths::MakeProjectRelative(SourceFbxPath)));
            FString               SafeAnimName  = SanitizeAssetName(Sequence->GetName());
            std::filesystem::path AssetPath     = Dir / (SourceRel.stem().wstring() + L"_" + FPaths::ToWide(SafeAnimName) + L".uasset");
            std::filesystem::path FullAssetPath = std::filesystem::path(FPaths::RootDir()) / AssetPath;
            FPaths::CreateDir(FullAssetPath.parent_path().wstring());
            AnimPath = FPaths::ToUtf8(AssetPath.generic_wstring());
        }
        else
        {
            AnimPath = GetAnimationPathForSkeleton(SourceFbxPath, Sequence->GetName(), TargetSkeletonPath);
        }

        if (!bOverwriteExistingAssets && std::filesystem::exists(ResolveProjectPath(AnimPath)))
        {
            UE_LOG("Animation import skipped: destination exists. Path=%s", AnimPath.c_str());
            continue;
        }

        if (!SaveAnimation(Sequence, AnimPath, SourceFbxPath))
        {
            UE_LOG("Animation save failed: save failed. Path=%s", AnimPath.c_str());
            if (OutError) *OutError = "Failed to write animation asset: " + AnimPath;
            return false;
        }

        bSavedAny = true;
        if (OutSequences)
        {
            OutSequences->push_back(Sequence);
        }
    }

    if (!bSavedAny && OutError && OutError->empty())
    {
        // 본 호환은 통과했지만 모든 시퀀스가 "이미 존재"로 스킵된 경우.
        *OutError = "No animation imported — all targets already exist. Enable \"Overwrite existing animation assets\".";
    }

    return bSavedAny;
}

void FAnimationManager::RefreshAvailableAnimations()
{
    const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
    if (!std::filesystem::exists(ContentRoot))
    {
        return;
    }

    const std::filesystem::path ProjectRoot(FPaths::RootDir());

    AvailableAnimationFiles.clear();

    for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentRoot))
    {
        if (!Entry.is_regular_file()) continue;

        std::wstring Ext = Entry.path().extension().wstring();
        std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
        if (Ext != L".uasset") continue;

        const FString RelPath =
        FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

        FAssetImportMetadata Metadata;
        if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::AnimSequence, Metadata))
        {
            continue;
        }

        FAssetListItem Item;
        Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
        Item.FullPath    = RelPath;
        AvailableAnimationFiles.push_back(std::move(Item));
    }
}

bool FAnimationManager::DeleteAnimation(const FString& PackagePath, FString* OutError)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(PackagePath);

    // 1) 디스크 파일 제거.
    std::error_code Ec;
    const std::filesystem::path FullPath = ResolveProjectPath(NormalizedPath);
    std::filesystem::remove(FullPath, Ec);
    if (Ec)
    {
        if (OutError) *OutError = Ec.message().c_str();
        UE_LOG("Animation delete failed: %s Path=%s", Ec.message().c_str(), NormalizedPath.c_str());
        return false;
    }

    // 2) 인메모리 캐시에서 제거 — UObject 자체는 GC 가 수거. (선택 해제는 caller 책임)
    auto CacheIt = AnimationCaches.find(NormalizedPath);
    if (CacheIt != AnimationCaches.end())
    {
        AnimationCaches.erase(CacheIt);
    }

    // 3) 사용 가능 목록에서 제거.
    AvailableAnimationFiles.erase(
        std::remove_if(
            AvailableAnimationFiles.begin(),
            AvailableAnimationFiles.end(),
            [&](const FAssetListItem& Item) { return Item.FullPath == NormalizedPath; }),
        AvailableAnimationFiles.end());

    UE_LOG("Animation deleted: Path=%s", NormalizedPath.c_str());
    return true;
}

// ─────────────────────────────────────────────────────────────
// Montage
// ─────────────────────────────────────────────────────────────

UAnimMontage* FAnimationManager::LoadMontage(const FString& PackagePath)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(PackagePath);

    auto It = MontageCaches.find(NormalizedPath);
    if (It != MontageCaches.end())
    {
        if (IsValid(It->second))
        {
            return It->second;
        }
        MontageCaches.erase(It);
    }

    FWindowsBinReader Reader(NormalizedPath);
    if (!Reader.IsValid())
    {
        UE_LOG("Montage load failed: could not open file. Path=%s", NormalizedPath.c_str());
        return nullptr;
    }

    FAssetPackageHeader Header;
    FAssetImportMetadata Metadata;
    if (!FAssetPackage::ReadPackagePrelude(Reader, EAssetPackageType::AnimMontage, Header, Metadata))
    {
        UE_LOG("Montage load failed: invalid package header. Path=%s", NormalizedPath.c_str());
        return nullptr;
    }

    UAnimMontage* Montage = UObjectManager::Get().CreateObject<UAnimMontage>();
    Montage->Serialize(Reader);
    Montage->SetAssetPathFileName(NormalizedPath);

    if (!Reader.IsValid())
    {
        UE_LOG("Montage load failed: corrupted package. Path=%s", NormalizedPath.c_str());
        UObjectManager::Get().DestroyObject(Montage);
        return nullptr;
    }

    // SourceSequence wire — path 가 None 이 아니면 LoadAnimation 으로 resolve.
    {
        const FString& SrcPath = Montage->GetSourceSequencePath();
        if (!SrcPath.empty() && SrcPath != "None")
        {
            if (UAnimSequence* SrcSeq = LoadAnimation(SrcPath))
            {
                // SetSourceSequence 는 PlayLength/FrameRate 동기화 + EnsureDefaultSection.
                // 단, EnsureDefaultSection 은 Sections 이미 있으면 no-op 이라 기존 정보 보존됨.
                Montage->SetSourceSequence(SrcSeq);
            }
            else
            {
                UE_LOG("Montage load: source sequence resolve failed. Path=%s", SrcPath.c_str());
            }
        }
    }

    auto ListIt = std::find_if(
        AvailableMontageFiles.begin(),
        AvailableMontageFiles.end(),
        [&](const FAssetListItem& Item) { return Item.FullPath == NormalizedPath; });
    if (ListIt == AvailableMontageFiles.end())
    {
        FAssetListItem Item;
        Item.DisplayName = Montage->GetName();
        Item.FullPath    = NormalizedPath;
        AvailableMontageFiles.push_back(std::move(Item));
    }

    MontageCaches[NormalizedPath] = Montage;
    return Montage;
}

bool FAnimationManager::SaveMontage(UAnimMontage* Montage, const FString& PackagePath)
{
    if (!Montage)
    {
        return false;
    }

    const FString NormalizedPath = FPaths::MakeProjectRelative(PackagePath);
    Montage->SetAssetPathFileName(NormalizedPath);

    // 대상 디렉토리 자동 생성 — AnimSequence save 와 동일 패턴.
    {
        std::filesystem::path FullAssetPath = ResolveProjectPath(NormalizedPath);
        FPaths::CreateDir(FullAssetPath.parent_path().wstring());
    }

    FWindowsBinWriter Writer(NormalizedPath);
    if (!Writer.IsValid())
    {
        UE_LOG("Montage save failed: could not open file. Path=%s", NormalizedPath.c_str());
        return false;
    }

    FAssetImportMetadata Metadata;
    // Montage 는 FBX 같은 source 가 없음 — SourcePath 비워둠.

    if (!FAssetPackage::WritePackagePrelude(Writer, EAssetPackageType::AnimMontage, Metadata))
    {
        UE_LOG("Montage save failed: package prelude write failed. Path=%s", NormalizedPath.c_str());
        return false;
    }
    Montage->Serialize(Writer);

    if (!Writer.IsValid())
    {
        UE_LOG("Montage save failed: write failed. Path=%s", NormalizedPath.c_str());
        return false;
    }

    auto ListIt = std::find_if(
        AvailableMontageFiles.begin(),
        AvailableMontageFiles.end(),
        [&](const FAssetListItem& Item) { return Item.FullPath == NormalizedPath; });
    if (ListIt == AvailableMontageFiles.end())
    {
        FAssetListItem Item;
        Item.DisplayName = Montage->GetName();
        Item.FullPath    = NormalizedPath;
        AvailableMontageFiles.push_back(std::move(Item));
    }

    MontageCaches[NormalizedPath] = Montage;
    return true;
}

bool FAnimationManager::SaveMontagePreservingMetadata(UAnimMontage* Montage)
{
    if (!Montage) return false;
    const FString& AssetPath = Montage->GetAssetPathFileName();
    if (AssetPath.empty() || AssetPath == "None")
    {
        UE_LOG("Montage save failed: asset path is unknown. Montage=%s", Montage->GetName().c_str());
        return false;
    }
    return SaveMontage(Montage, AssetPath);
}

UAnimMontage* FAnimationManager::CreateMontage(UAnimSequence* SourceSequence, const FString& MontageName)
{
    UAnimMontage* Montage = UObjectManager::Get().CreateObject<UAnimMontage>();
    Montage->SetFName(FName(MontageName));
    Montage->SetSourceSequence(SourceSequence);
    return Montage;
}

void FAnimationManager::RefreshAvailableMontages()
{
    const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
    if (!std::filesystem::exists(ContentRoot)) return;
    const std::filesystem::path ProjectRoot(FPaths::RootDir());

    AvailableMontageFiles.clear();

    for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentRoot))
    {
        if (!Entry.is_regular_file()) continue;

        std::wstring Ext = Entry.path().extension().wstring();
        std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
        if (Ext != L".uasset") continue;

        const FString RelPath =
            FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

        FAssetImportMetadata Metadata;
        if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::AnimMontage, Metadata))
        {
            continue;
        }

        FAssetListItem Item;
        Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
        Item.FullPath    = RelPath;
        AvailableMontageFiles.push_back(std::move(Item));
    }
}

bool FAnimationManager::DeleteMontage(const FString& PackagePath, FString* OutError)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(PackagePath);

    // 1) 디스크 파일 제거. (SourceSequence 는 별개 자산이므로 건드리지 않음)
    std::error_code Ec;
    const std::filesystem::path FullPath = ResolveProjectPath(NormalizedPath);
    std::filesystem::remove(FullPath, Ec);
    if (Ec)
    {
        if (OutError) *OutError = Ec.message().c_str();
        UE_LOG("Montage delete failed: %s Path=%s", Ec.message().c_str(), NormalizedPath.c_str());
        return false;
    }

    // 2) 인메모리 캐시에서 제거 — UObject 자체는 GC 가 수거.
    auto CacheIt = MontageCaches.find(NormalizedPath);
    if (CacheIt != MontageCaches.end())
    {
        MontageCaches.erase(CacheIt);
    }

    // 3) 사용 가능 목록에서 제거.
    AvailableMontageFiles.erase(
        std::remove_if(
            AvailableMontageFiles.begin(),
            AvailableMontageFiles.end(),
            [&](const FAssetListItem& Item) { return Item.FullPath == NormalizedPath; }),
        AvailableMontageFiles.end());

    UE_LOG("Montage deleted: Path=%s", NormalizedPath.c_str());
    return true;
}


void FAnimationManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : AnimationCaches)
    {
        Collector.AddReferencedObject(Pair.second, "FAnimationManager.AnimationCaches");
    }
    for (auto& Pair : MontageCaches)
    {
        Collector.AddReferencedObject(Pair.second, "FAnimationManager.MontageCaches");
    }
}

void FAnimationManager::ClearCache()
{
    AnimationCaches.clear();
    MontageCaches.clear();
    AvailableAnimationFiles.clear();
    AvailableMontageFiles.clear();
}
