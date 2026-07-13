#include "Mesh/Importer/Fbx/FbxSkeletonImporter.h"
#include "Mesh/Importer/Fbx/FbxSceneQuery.h"
#include "Mesh/Importer/Fbx/FbxTransformUtils.h"

namespace
{
	static void BuildReferenceSkeleton(FFbxImportContext& Context)
	{
		Context.ReferenceSkeleton.Bones.clear();
		Context.ReferenceSkeleton.Bones.reserve(Context.Bones.size());

		for (const FBone& Bone : Context.Bones)
		{
			FReferenceBone RefBone;
			RefBone.Name = Bone.Name;
			RefBone.ParentIndex = Bone.ParentIndex;
			RefBone.LocalBindPose = Bone.GetReferenceLocalPose();
			RefBone.GlobalBindPose = Bone.GetReferenceGlobalPose();
			RefBone.InverseBindPose = Bone.GetInverseBindPose();
			Context.ReferenceSkeleton.Bones.push_back(RefBone);
		}
	}
}

bool FFbxSkeletonImporter::ImportSkeleton(FbxScene* Scene, FFbxImportContext& Context, FString* OutMessage)
{
	(void)Scene;
	Context.Bones.clear();
	Context.BoneNodeToIndex.clear();
	Context.ReferenceSkeleton.Bones.clear();

	const bool bUseNameFilter = !Context.ReferenceBoneNames.empty();

	for (FbxNode* Node : Context.AllNodes)
	{
		if (bUseNameFilter)
		{
			// Animation-only FBX exports joints as eNull nodes, so match against the target
			// skeleton's bone names instead of the eSkeleton attribute type.
			if (!Node || Context.ReferenceBoneNames.find(Node->GetName()) == Context.ReferenceBoneNames.end())
			{
				continue;
			}
		}
		else if (!FFbxSceneQuery::IsSkeletonNode(Node))
		{
			continue;
		}

		FBone Bone;
		Bone.Name = Node->GetName();

		Bone.ParentIndex = FindNearestParentBoneIndex(Node, Context.BoneNodeToIndex);

		const FbxAMatrix GlobalFbxMatrix = Node->EvaluateGlobalTransform();
		const bool bAbsorbWrapperTransform = Bone.ParentIndex < 0 && FFbxSceneQuery::HasNonSkeletonWrapperParent(Node);
		const FbxAMatrix LocalFbxMatrix = bAbsorbWrapperTransform
			? GlobalFbxMatrix
			: Node->EvaluateLocalTransform();
		Bone.LocalMatrix = FFbxTransformUtils::ToEngineMatrix(LocalFbxMatrix);
		Bone.GlobalMatrix = FFbxTransformUtils::ToEngineMatrix(GlobalFbxMatrix);
		Bone.InverseBindPoseMatrix = FFbxTransformUtils::ToEngineInverseMatrix(GlobalFbxMatrix);
		Bone.SyncSeparatedPoseDataFromLegacy();

		const int32 NewBoneIndex = static_cast<int32>(Context.Bones.size());
		Context.Bones.push_back(Bone);
		Context.BoneNodeToIndex[Node] = NewBoneIndex;
	}

	if (Context.Bones.empty())
	{
		if (OutMessage)
		{
			*OutMessage = bUseNameFilter
				? "FBX skeletal import failed: no scene nodes matched the target skeleton's bone names."
				: "FBX skeletal import failed: no skeleton nodes found.";
		}
		return false;
	}

	BuildReferenceSkeleton(Context);
	return true;
}

int32 FFbxSkeletonImporter::FindNearestParentBoneIndex(FbxNode* Node, const TMap<FbxNode*, int32>& NodeToIndex)
{
	FbxNode* Parent = Node ? Node->GetParent() : nullptr;
	while (Parent)
	{
		auto It = NodeToIndex.find(Parent);
		if (It != NodeToIndex.end())
		{
			return It->second;
		}

		Parent = Parent->GetParent();
	}

	return -1;
}
