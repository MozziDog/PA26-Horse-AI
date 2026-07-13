#pragma once

#include "Mesh/Importer/Fbx/FbxImportTypes.h"
#include "Render/Types/VertexTypes.h"

#include <fbxsdk.h>

struct FFbxMorphVertexSource
{
	FbxMesh* Mesh                    = nullptr;
	int32    ControlPointIndex       = -1;
	FMatrix  MeshBindGlobal          = FMatrix::Identity;
	FMatrix  RigidBindCorrection     = FMatrix::Identity;
	bool     bUseRigidBindCorrection = false;
};

struct FFbxImportContext
{
	FString SourcePath;
	FString EmbeddedTextureScratchDirectory;

	TArray<FbxNode*> AllNodes;
	TArray<FbxNode*> MeshNodes;

	TArray<FFbxImportedMaterialInfo> Materials;
	TMap<FbxSurfaceMaterial*, int32> MaterialToSlotIndex;

	TArray<FBone> Bones;
	TMap<FbxNode*, int32> BoneNodeToIndex;
	FReferenceSkeleton ReferenceSkeleton;

	// When non-empty, the skeleton importer selects bone nodes by matching these names instead of
	// relying on FbxNodeAttribute::eSkeleton. Needed for animation-only FBX whose joints are
	// exported as plain transform (eNull) nodes; the names come from the target skeleton.
	TSet<FString> ReferenceBoneNames;

	TArray<FVertexPNCTBW>         SkeletalVertices;
	TArray<uint32>                SkeletalIndices;
	TArray<FSkeletalMeshSection>  SkeletalSections;
	TArray<FSkeletalMeshRange>    SkeletalMeshRanges;
	TArray<FMorphTarget>          SkeletalMorphTargets;
	TArray<FFbxMorphVertexSource> SkeletalMorphVertexSources;

	TArray<FVector> TangentSums;
	TArray<FVector> BitangentSums;

	TArray<UAnimSequence*> AnimSequences;
};
