#include "UI/Canvas/UIImage.h"

#include "Texture/Texture2D.h"
#include "Platform/Paths.h"

#include <d3d11.h>

void UUIImage::SetTexturePath(const FString& InPath)
{
	// 스탠드얼론 이식성 — 절대경로면 프로젝트 상대(forward slash, 예: "Content/...")로 정규화해 저장한다.
	// 이미 상대경로이거나 변환 불가하면 MakeProjectRelative 가 입력을 그대로 반환. 빈 문자열은 그대로 통과.
	TexturePath.SetPath(InPath.empty() ? InPath : FString(FPaths::MakeProjectRelative(InPath)));
}

ID3D11ShaderResourceView* UUIImage::ResolveTextureSRV(ID3D11Device* Device)
{
	// 경로가 바뀐 경우(또는 최초)에만 재로드. LoadFromFile 은 path#srgb 키로 캐시되어 매 프레임 재로드 없음.
	const FString& Path = TexturePath.ToString();
	if (Path != ResolvedPath)
	{
		ResolvedPath = Path;
		LoadedTexture = (TexturePath.IsNull() || !Device)
			? nullptr
			: UTexture2D::LoadFromFile(Path, Device, ETextureColorSpace::SRGB);
	}
	return (LoadedTexture && LoadedTexture->IsLoaded()) ? LoadedTexture->GetSRV() : nullptr;
}
