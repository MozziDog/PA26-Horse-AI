#pragma once

#include "UI/Canvas/UITextElement.h"
#include "Object/Ptr/SoftObjectPtr.h"

#include "Source/Engine/UI/Canvas/UIImage.generated.h"

struct ID3D11Device;
struct ID3D11ShaderResourceView;
class UTexture2D;

// 이미지 요소(팔레트). [사이클 8] Texture 지정 시 텍스처 쿼드(텍스처 × BackgroundColor),
// 비면 단색(SimpleUIPass 가 1×1 흰색 fallback → BackgroundColor 그대로). 텍스트는 UUITextElement 에서 상속.
UCLASS()
class UUIImage : public UUITextElement
{
public:
	GENERATED_BODY()
	UUIImage()
	{
		SetSize(FVector2(200.0f, 200.0f));
		SetColor(FVector4(1.0f, 1.0f, 1.0f, 1.0f));   // 흰색 → 텍스처가 변조 없이 보임(단색 시 흰 배경)
	}

	// [사이클 8] TexturePath → SRV. 경로가 바뀔 때만 재로드(UTexture2D::LoadFromFile 캐시 활용).
	// 텍스처 없으면 nullptr → SimpleUIPass 가 흰색 fallback 으로 단색 처리. Device 는 패스가 전달.
	ID3D11ShaderResourceView* ResolveTextureSRV(ID3D11Device* Device);

	// 텍스처 파일 경로(프로젝트 상대, 예: "Content/UI/Images/foo.png"). UI 에디터 Details 에서 설정.
	// 절대경로를 넘기면 SetTexturePath 가 프로젝트 상대로 정규화해 스탠드얼론 빌드 이식성을 보장한다.
	void    SetTexturePath(const FString& InPath);
	FString GetTexturePath() const { return TexturePath.ToString(); }

private:
	UPROPERTY(Edit, Save, Category="Image", DisplayName="Texture", AssetType="Texture")
	FSoftObjectPtr TexturePath;

	// 런타임 캐시(UPROPERTY 아님 → 반사/직렬화 제외). 텍스처 수명은 UTexture2D 캐시가 GC-루팅.
	UTexture2D* LoadedTexture = nullptr;
	FString     ResolvedPath;
};
