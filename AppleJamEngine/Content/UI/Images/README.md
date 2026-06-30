# UI Images

UI `Image` 요소에 띄울 이미지 파일(PNG / JPG / JPEG)을 이 폴더에 둡니다.

## 경로 규약 (스탠드얼론 빌드 필수)

- UI 에디터의 Image 요소 → Details → **Texture Path** 또는 **Browse...** 로 지정합니다.
- 저장되는 경로는 **프로젝트 상대 경로(forward slash)** 여야 합니다. 예:
  ```
  Content/UI/Images/title_logo.png
  ```
- Browse 다이얼로그는 선택한 파일을 자동으로 위 형식의 상대 경로로 반환합니다.
  (절대 경로를 직접 입력해도 `SetTexturePath` 가 프로젝트 상대로 정규화합니다.)

## 동작 원리

- 런타임: `UUIImage::ResolveTextureSRV` → `UTexture2D::LoadFromFile` → `SimpleUIPass` 가
  텍스처 × `Background Color`(틴트) 로 렌더링.
- 경로는 `FPaths::RootDir()` 기준으로 해석되므로, 빌드 스크립트가 `Content/` 를 빌드 루트로
  복사하기만 하면(현재 `GameBuild.bat` / `ReleaseBuild.bat` 가 그렇게 함) 그대로 동작합니다.
- 별도 임포트/쿠킹 불필요 — 원본 이미지 파일을 그대로 둡니다.

## 지원 포맷

PNG, JPG, JPEG (WIC). 그 외 BMP/DDS 도 로더가 지원하며, TGA 는 stb_image 경로로 처리됩니다.
