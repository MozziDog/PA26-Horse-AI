# UI 에디터 4분할(팔레트/계층트리/캔버스 뷰포트/디테일) 재구성 — 진단

> 작성일: 2026-06-06 · 대상: KraftonEngine (자체 D3D11 엔진) · 사이클: **진단 단계 (구현 없음)**
> 방법: 병렬 탐색 에이전트 2(뷰포트 렌더 경로 / 인스펙터·선택·생성 모델) + 핵심 주장 직접 재검증. 근거는 모두 `파일:라인`.
> 표기: **[사실]** = 코드로 확인된 현황 · **[설계]** = 신규 권고(코드 근거 아님) · **[정정]** = 트리거 프롬프트 전제 오류 정정 · **[전제]** = 선행 의존성.

---

## 0. 결론 요약 (먼저 읽을 것)

### ⚠️ 트리거 프롬프트의 핵심 전제가 코드와 어긋난다 — 설계 방향이 바뀐다
- **[정정] "기존 RML UI Editor가 4분할 비슷하게 있고, 동작하는 Inspector(Pivot/Offset/W/H/Color)를 재사용·이식한다" → 이 브랜치엔 둘 다 없다.**
  - **현 UI 에디터 = 빈 창**: [UIEditorWidget.cpp](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp)은 `ImGui::Begin` 하나에 placeholder 텍스트뿐(직전 사이클의 "빈 골격"). Elements 리스트도, Inspector도 없다.
  - **범용 Details 패널은 UI 요소 속성을 못 띄운다**: UIElement 속성은 [UIElement.h:51-62](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)에서 `UPROPERTY(`**`Save`**`)` = `PF_Save`만 부여 → 그런데 Details는 [`GetEditableProperties`가 `PF_Edit`만 노출](../KraftonEngine/Source/Engine/Object/Object.cpp)(Object.cpp:164)한다. 게다가 [EditorPropertyWidget](../KraftonEngine/Source/Editor/UI/Panel/EditorPropertyWidget.cpp)에 **`Vec2` 렌더 case가 아예 없다**(grep `case EPropertyType::Vec2` = 0건). 즉 Pivot/Offset/W/H(전부 Vec2)는 *노출도 안 되고 그릴 수단도 없다*. `Color4`만 렌더 경로 존재(`ColorEdit4`, EditorPropertyWidget.cpp:~3180).
  - **UI 전용 인스펙터 코드 없음**: Editor 트리에서 UI 런타임 클래스를 참조하는 곳은 `AssetFactory`(에셋 생성) 1곳뿐. main/feature/PCH 브랜치에도 `UIEditorWidget` 본체 없음. git clean.
- **[설계] 따라서 "재사용할 인스펙터"는 *위젯*이 아니라 *리플렉션 렌더 프리미티브(`DragFloat`/`ColorEdit4`) + UIElement 리플렉션 메타데이터*다.** 디테일 패널은 폐기/재작성이 아니라(애초에 위젯이 없음) — 이 프리미티브로 **새로 조립**하되 5필드를 선택 요소에 직접 바인딩하는 게 최소·확실하다(§E).

> ❓ **이미지 2가 다른 브랜치/빌드의 UI 인스펙터라면 그 경로를 알려주세요.** 진짜 동작 위젯이 존재하면 §A·§E의 "이식 대상"을 그걸로 교체해 재진단하겠습니다. 아래 설계는 "없음"을 기준으로 작성.

### 핵심 판정
- **[사실] 재사용 가능한 진짜 자산은 런타임 UI의 *레이아웃·드로우·히트테스트 로직*이다** — 4분할의 알맹이는 거의 다 이미 있다.
  - 레이아웃: [`FUICanvasManager::LayoutAll`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)(UICanvasManager.cpp:90) / `LayoutElement`(:105), 좌표변환 `Screen = FinalPos * GlobalScale`(:123).
  - 드로우: [`FSimpleUIPass::CollectVisible`](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)(SimpleUIPass.cpp:47)가 `ScreenRect+Color`→쿼드. **단색 사각형이라 ImGui DrawList로 1:1 미러 가능**.
  - 히트테스트: [`FUICanvasManager::HitTest`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)(UICanvasManager.cpp:160). 드래그: `TickEditor`(:170).
- **[전제] 단일 최대 의존성 — 라이브 트리 복원.** 직전 사이클의 .uasset 저장은 트리를 **JSON 블롭(`UUIAsset::CanvasData`)으로만** 보관하고 **라이브 `UUIElement` 트리를 복원하지 않는다**([UIAsset.h](../KraftonEngine/Source/Engine/UI/UIAsset.h)). 그런데 4분할의 **뷰포트·트리·팔레트·디테일이 전부 살아있는 트리를 전제**한다. → **JSON→라이브 트리 복원이 이 기능 전체의 선행 사이클(⓪)**이다(§F 리스크 #1).

---

## A. 현재 UI 에디터 해부
- [x] **창 레이아웃 구성 위치**: [UIEditorWidget.cpp `Render`](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp) — 단일 `ImGui::Begin("…###UIEditor")` + placeholder. **Elements 리스트/Inspector 패널 부재.** (도킹 패널 `[Window][Details]`/`[Window][Outliner]`는 *레벨 에디터*용, UI 에디터 소속 아님)
- [x] **Inspector 필드 바인딩 경로(범용 패널 기준 — 현재 UI엔 미작동)**: [`GetEditableProperties`](../KraftonEngine/Source/Engine/Object/Object.cpp)(PF_Edit 필터, Object.cpp:164) → `RenderPropertyWidget` switch(타입별 `DragFloat3/4`, `ColorEdit4`; EditorPropertyWidget.cpp:~3147–3181) → 포인터 in-place 수정 → `DispatchPostEditChange`→`PostEditChangeProperty`. **UI 속성은 PF_Save라 이 경로에 안 올라옴.**
- [x] **"Add Text" 생성 경로**: 그 버튼 없음. 요소 생성 **모델**은 [`AddComponentToActor`](../KraftonEngine/Source/Editor/UI/Panel/EditorPropertyWidget.cpp)(EditorPropertyWidget.cpp:1874) = `Actor->AddComponentByClass` + `AttachToComponent`. 런타임 캔버스 구성 선례 = [`UICanvasActor::InitCanvas`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)(UICanvasActor.cpp:6).
- [x] **에디터 렌더 수단**: **ImGui 즉시모드**(FUIEditorWidget·EditorPropertyWidget 모두). 런타임 UI = `SimpleUIPass`(D3D 쿼드 배처) + 텍스트만 RmlUi.

## B. 4분할 레이아웃 골격
- [x] **4 영역 만드는 법(ImGui)**: FUIEditorWidget 단일 창 내부에서 `ImGui::BeginChild` + `SameLine` 분할. 선례 = [MeshEditorWidget의 좌트리/우디테일 child 분할](../KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp)(MeshEditorWidget.cpp:1456). 에셋 에디터는 단일 창이라 *내부 child 분할*이 도킹노드보다 단순·확실. (재배치까지 원하면 창 내부 `DockSpace` — 옵션)
- [x] **재배치 방안**: "기존 Elements 리스트/Inspector 이동"이 아니라(둘 다 없음) — 4 child에 **신규 구축**: 좌상=팔레트, 좌하=계층트리(`ImGui::TreeNode`로 캔버스 `GetChildren` 재귀), 중앙=뷰포트, 우=디테일.

## C. ★ 중앙 캔버스 뷰포트 (최대 리스크)
- [x] **에디터에 요소 렌더 경로 존재? → 없음.** 런타임만 존재: [`CollectVisible`](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)(SimpleUIPass.cpp:47)가 가시 요소 `GetScreenRect()+GetColor()`→쿼드, `Execute`(:123)가 **화면 RTV(`Ctx.Cache.RTV`, :179)** 에 그림. `FPassContext`(D3D Device/Frame/Shader) 필수, `BeginPass`는 ViewportRTV+캔버스 있을 때만(:120).
- [x] **재사용 결론 = Option B (ImGui DrawList 미러)**: D3D 패스는 화면 RTV에 묶여 에디터 패널로 retarget 비용이 큼. 대신 뷰포트 child에서 `ImGui::GetWindowDrawList()->AddRectFilled(min,max,col)`로 **CollectVisible 로직을 그대로 미러**(가시 요소 ScreenRect→사각형). 텍스처/텍스트 없는 단색 범위라 1:1로 옮겨짐. (대안 A=오프스크린 RT로 SimpleUIPass 렌더 후 `ImGui::Image`는 RT 파라미터화 리팩터 필요 → 과함)
- [x] **그리드/줌/RefRes(1920×1080)**: 뷰포트 child의 DrawList로 직접 그림. 줌 = `GlobalScale = 뷰포트높이/1080` 조절.
- [x] **좌표 변환 지점**: [`Screen.Pos = FinalPos * Scale`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)(`LayoutElement`, UICanvasManager.cpp:123). 역변환(뷰포트→UI ref): `UIref = (마우스 - 캔버스화면원점) / GlobalScale`.
- [x] **히트테스트→동기화**: [`HitTest`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)(UICanvasManager.cpp:160)에 리맵 마우스 전달 → 선택 요소 → 트리/디테일 갱신. 재사용 가능.
- [x] **(선택) 드래그 배치**: [`TickEditor`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)(UICanvasManager.cpp:170)의 로직(`HitTest` + `SetPosition += delta/Scale`) 재사용. 단 F9 런타임 경로가 아니라 에디터가 자체 호출.
- **[전제] 의존성**: layout/draw/hittest 전부 **살아있는 `UUIElement` 트리**(`FUICanvasManager`에 등록된 캔버스)를 대상으로 동작. 직전 사이클은 .uasset을 JSON 블롭으로만 저장 → **복원 선행 필수**. 또 `LayoutAll`(public)은 *전체 캔버스* 대상이고 `LayoutElement`는 private → 에디터 캔버스를 매니저에 등록해 LayoutAll 하거나 layout을 미러해야 함(싱글톤 오염 seam).

## D. 팔레트 3종 (Canvas/Button/Image)
- [x] **타입 개념 = UCLASS**. 기존: [UUICanvas](../KraftonEngine/Source/Engine/UI/Canvas/UICanvas.h)(루트, 1920×1080, `SetVisibleRect(false)`), [UUIGroup](../KraftonEngine/Source/Engine/UI/Canvas/UIGroup.h)(비가시 컨테이너), [UUILabel](../KraftonEngine/Source/Engine/UI/Canvas/UILabel.h)(RmlUi 텍스트, 비가시), 베이스 [UUIElement](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)(기본 `bVisibleRect=true` → 단색 쿼드 그림).
- [x] **추가로 정의할 것**: Canvas=기존. **Button/Image = "단색 사각형"이라 가시 `UUIElement` 그 자체로 충분**. 팔레트/트리 라벨용 타입 식별을 위해 trivial 서브클래스 `UUIButton`/`UUIImage`(UIGroup처럼 빈 UCLASS, 단 가시 rect 유지)만 추가. 텍스처 참조·특수속성 없음.
- [x] **팔레트→생성 흐름**: `AddComponentToActor` 모델 재사용 — `UObjectManager::CreateObject<UUIButton>()` + 선택 노드(없으면 캔버스)에 `AttachToComponent`.
- [x] **공통 속성만 확인**: W/H(`Size`)·Offset(`Position`)·Pivot·Color(`BackgroundColor`)는 전부 [UUIElement 베이스](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)(UIElement.h:51) 보유. 타입별 특수속성 0 — 확인됨.

## E. 디테일 편집 이식
- [x] **이식 가능성**: 위젯 통째 이식은 불가(존재 안 함). **렌더 프리미티브 재사용** = `ColorEdit4`(EditorPropertyWidget.cpp:~3180), `DragFloat`류. **단순·확실안**: 새 디테일 패널이 5필드를 직접 렌더 — `DragFloat2`×3(W/H, Offset, Pivot) + `ColorEdit4`(RGBA) — 선택 `UUIElement`의 `RectTransform`/`BackgroundColor`에 직접 read/write. (엔진 인스펙터와 *동일 ImGui 프리미티브* → property 시스템 재작성 아님)
- [x] **선택→바인딩**: FUIEditorWidget이 `UUIElement* Selected` 로컬 보관(트리 클릭/뷰포트 HitTest가 set) → 디테일이 그걸 편집. (액터 기반 `SelectionManager`보다 분리형 캔버스엔 로컬 포인터가 단순)
- [x] **텍스트 전용 필드 숨김**: 팔레트 3종(Canvas/Button/Image)엔 Text 없음 → 숨길 것 없음. (UILabel 선택 시 `Text`만 제외 — 직접 렌더면 자연 제외, 리플렉션 기반이면 `ShouldExposeProperty`/카테고리 필터)
- **[설계] 리플렉션 기반으로 갈 경우 선행 작업 2가지**: ① EditorPropertyWidget에 `case Vec2`(→`DragFloat2`) 추가, ② UI 속성에 `PF_Edit`(=`UPROPERTY(EditAnywhere, Save)`) 부여. → **직접 렌더를 택하면 둘 다 불필요**(권장).

## F. 구현 사이클 분할 + 리스크
제안 — 선행 전제를 ⓪로 명시:
- **⓪ 선행: .uasset JSON → 라이브 `UUICanvas` 트리 복원.** [`DeserializeSceneComponentTree`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp)(SceneSaveManager.cpp:693) + owner 액터/인스턴스화 컨텍스트. ※ 이것 없이 ②~⑤ 전부 불가.
- **① 4분할 골격**: FUIEditorWidget 안 `BeginChild` 4분할(팔레트/트리/뷰포트/디테일 빈 박스).
- **② 중앙 뷰포트 렌더**: 복원 트리 layout(GlobalScale 세팅) → ImGui DrawList로 단색 쿼드 + 그리드 미러.
- **③ 팔레트 3종 타입+생성**: `UUIButton`/`UUIImage` 추가 + 팔레트 클릭→CreateObject+Attach.
- **④ 뷰포트 선택/히트테스트**: `HitTest`(좌표 리맵) → Selected 동기화(트리 하이라이트).
- **⑤ 디테일 패널**: 5필드 직접 ImGui 바인딩(Selected의 RectTransform/Color).

**막힐 가능성 높은 2곳**:
1. **★ ⓪+② (뷰포트의 라이브 트리)**: 직전 사이클이 JSON 블롭만 저장 → 복원 경로(owner 액터 필요)와, `LayoutAll` 싱글톤 전체-캔버스 동작 vs 에디터 단일 캔버스 layout seam. **이 기능 전체의 단일 최대 리스크.**
2. **디테일/인스펙터 전제**: "동작 인스펙터 재사용"은 사실상 신규 조립(Vec2 렌더러 부재 + UI 속성 PF_Save). 직접 렌더로 우회 권장.

---

## 경계
- 이번 산출물 = 본 진단 문서(코드 변경 없음).
- **재확인**: 출발 전제("기존 동작 인스펙터/4분할 에디터 재사용")가 이 브랜치 코드와 어긋난다 — 재사용 대상은 *위젯이 아니라 리플렉션/레이아웃/히트테스트/드로우 프리미티브*다. 이미지 2가 다른 브랜치라면 알려주면 §A·§E를 그 위젯 기준으로 재진단.
