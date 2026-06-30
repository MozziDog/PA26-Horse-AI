# UI .uasset 을 월드 viewport 에 drag-drop → 화면 UI 액터 spawn — 진단

> 작성일: 2026-06-07 · 대상: KraftonEngine (자체 D3D11 엔진) · 사이클: **진단 단계 (구현 없음)**
> 방법: 콘텐츠 브라우저 드래그소스(ContentBrowserElement) · 레벨 viewport 드롭(FLevelViewportLayout) · 액터 spawn(UWorld) · UI 액터/렌더(UICanvasActor·UICanvasManager·SimpleUIPass) · .uasset 로드/복원(UIAssetManager·SceneSaveManager) · 직렬화(SceneSaveManager+생성 리플렉션) 직접 정독. 근거는 모두 `파일:라인`.
> 표기: **[사실]** = 코드로 확인된 현황 · **[설계]** = 신규 권고(코드 근거 아님) · **[정정]** = 트리거 프롬프트 전제 보정 · **[전제]** = 선행 의존성/리스크.
> 진행 순서 맥락: **Text → Image → 드래그 배치(본 문서)** 의 3단계.
> 확정 결정(재설계 금지): "월드 배치" = **의미 1 화면공간 UI**(드롭 3D 좌표 무시) · 드래그 대상 = **UI .uasset 전체** · 렌더 = **기존 화면공간 경로(SimpleUIPass 쿼드 + RmlUi 텍스트) 재사용** · spawn = **UICanvasActor 계열 활용 우선**.

---

## 0. 결론 요약 (먼저 읽을 것)

### ★ A 결과 (작업량을 가르는 1번 변수) — 기존 drag-drop 인프라 **존재함 (EXISTS)**
콘텐츠 브라우저 → 3D 월드 viewport → **액터 spawn** 전체 체인이 **이미 메시(StaticMesh)로 돌아가고 있다.** 즉 입력 배선을 새로 만들 필요가 없고, **본 작업은 "배선 추가 + UI 액터에 로드 능력 부여"** 규모다(대규모 입력 인프라 신설 아님).

- 드래그 소스(범용): [ContentBrowserElement.cpp:462-468](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp) — `BeginDragDropSource()` → `SetDragDropPayload(GetDragItemType(), &ContentItem, sizeof(FContentItem))`. payload = **`FContentItem`(에셋 파일 경로 보유)**.
- **UI .uasset 은 이미 드래그 가능** — [ContentBrowserElement.h:247-256](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h) `UIAssetElement::GetDragItemType()` = **`"UIContentItem"`**(:251).
- 드롭 수신(유일): [FLevelViewportLayout.cpp:891-907](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp) — viewport 가 `BeginDragDropTarget()` → `AcceptDragDropPayload("ObjectContentItem")`(:897) 만 받음.
- 드롭→spawn: 같은 블록(:901-903) — `FObjectFactory::Get().Create(AStaticMeshActor)` → `InitDefaultComponents(경로)` → `World->AddActor()`.

**핵심 갭 = 정확히 한 곳:** viewport 드롭 핸들러가 `"ObjectContentItem"` 만 받는다. `"UIContentItem"` 을 받는 분기는 **코드 전체에 0건**(grep 확인 — `"UIContentItem"` 은 드래그소스 선언 1곳뿐, accept 0곳).

### 핵심질문 1 (드래그 시작) — **있음. 그대로 재사용.**
메시·머티리얼·파티클 등 9종 에셋이 `ContentBrowserElement` 범용 Render 로 드래그 소스가 되고, payload 는 전부 `FContentItem`(경로). UI 도 그중 하나(`"UIContentItem"`)로 **이미 포함**. → 신규 작업 없음.

### 핵심질문 2 (드롭 수신) — **있음. 1개 분기만 추가하면 됨.**
유일한 월드 viewport 드롭 지점은 [FLevelViewportLayout.cpp:895-906](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp). 여기에 `AcceptDragDropPayload("UIContentItem")` 분기를 더하는 것이 전부. **[사실] 기존 메시 경로도 드롭 3D 좌표를 안 쓴다**(마우스 ray 계산 없이 `AddActor` 만 함, :901-903) → **의미 1(좌표 무시)과 이미 합치**.

### 핵심질문 3 (드롭→spawn) — **메시 경로와 동형. UI 액터에 "로드" 능력만 부여하면 됨.**
메시: `Create(AStaticMeshActor)` + `InitDefaultComponents(path)` + `AddActor`. UI 대응: `Create(AUICanvasActor)` + **(신규)** `LoadFromAsset(path)` + `AddActor`. UI 액터([AUICanvasActor](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h))는 **UCLASS 라 팩토리 생성 가능**, 복원 로직([FSceneSaveManager::DeserializeUITree](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp:466))은 **standalone 재사용 가능**. 빠진 것은 둘을 잇는 `LoadFromAsset` 한 함수뿐.

### 최대 리스크 한 줄 (자세히 §B-R1)
- **[전제] 화면 렌더 등록이 `BeginPlay` 에 묶여 있다.** UI 캔버스는 `AUICanvasActor::BeginPlay()` 에서만 `RegisterCanvas` 한다([UICanvasActor.cpp:24-32](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)). 그런데 `AddActor` 는 **`bHasBegunPlay` 일 때만 BeginPlay 를 호출**([World.cpp:368-372](../KraftonEngine/Source/Engine/GameFramework/World.cpp))하고, **에디터 월드는 `bHasBegunPlay=false`**. → **드롭한 UI 는 편집모드 화면엔 안 뜨고 PIE/런타임에서만 뜬다.** (메시는 프록시 기반이라 BeginPlay 무관하게 편집모드에서도 보이는 것과 **비대칭**.) 편집모드 즉시 미리보기를 원하면 별도 등록 훅 필요(§E 사이클 4).

---

## A. 기존 에셋 drag-drop 레퍼런스 추적 (가장 먼저)

- [x] **콘텐츠 브라우저 → 월드 viewport 끌어다 놓기 기능이 이미 존재하는가 → 존재 (메시 한정).** 위 §0 체인.
- [x] **드래그 시작 지점 + payload 구성**
  - 범용 소스: [ContentBrowserElement.cpp:462-468](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp). 모든 항목이 동일 Render 를 타고, `GetDragItemType()`(가상) 으로 payload 타입 문자열만 달라진다.
  - payload 본체: **`FContentItem`** = [ContentItem.h](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentItem.h) `{ std::filesystem::path Path; std::wstring Name; bool bIsDirectory; }`. → **에셋 식별자 = 파일 경로**(GUID 아님).
  - payload 타입 문자열(=DragID) 9종, [ContentBrowserElement.h](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h): `ObjectContentItem`(메시, :72) · `ParticleSystemContentItem` · `FloatCurveContentItem` · `AnimGraphContentItem` · `VectorFieldContentItem` · `MaterialContentItem` · `PhysicsAssetContentItem` · `LuaBlueprintContentItem` · **`UIContentItem`(:251)**.
  - **[정정]** 별개로 [EditorDragSource.cpp](../KraftonEngine/Source/Editor/UI/Util/EditorDragSource.cpp) 가 payload `DragSoruceInfo{ UObject* }` 를 쓰는 **다른(구) 드래그 경로**도 있으나, **콘텐츠 브라우저→viewport 는 위 `FContentItem` 경로**다. UI 배치는 `FContentItem` 경로만 따르면 됨(혼동 주의).
- [x] **viewport 드롭 핸들러**
  - [FLevelViewportLayout.cpp:891-907](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp). `GetDragDropPayload()` 가 있으면 viewport 영역에 `Selectable` 을 깔고 `BeginDragDropTarget()` → `AcceptDragDropPayload("ObjectContentItem")`(:897) → `FContentItem` 역해석(:899).
  - **[사실] viewport 는 다른 8종 payload 를 받지 않는다.** `"UIContentItem"` accept 는 코드 전체 0건(grep: accept 측엔 `ObjectContentItem`·`MaterialContentItem`·`ImageElement` 등이 디테일패널/머티리얼 인스펙터에만 존재, **viewport 엔 ObjectContentItem 만**).
- [x] **spawn 경로 전체**
  - [FLevelViewportLayout.cpp:901-903](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp): `Cast<AStaticMeshActor>(FObjectFactory::Get().Create(AStaticMeshActor::StaticClass()->GetName(), Editor->GetWorld()))` → `NewActor->InitDefaultComponents(FPaths::ToUtf8(ContentItem.Path))` → `Editor->GetWorld()->AddActor(NewActor)`.
  - 액터 생성 정식 API: [World.cpp:168-178 `SpawnActorByClass(UClass*)`](../KraftonEngine/Source/Engine/GameFramework/World.cpp) 및 `UWorld::SpawnActor<T>()`(World.h inline). 둘 다 내부에서 `FObjectFactory::Create(...PersistentLevel)` + `AddActor`. (viewport 메시 경로는 SpawnActorByClass 를 안 거치고 직접 Create+AddActor 한 동치 코드.)
  - **에셋 경로→실제 에셋 로드는 spawn 시점**: `InitDefaultComponents` 내부 `FMeshManager::LoadStaticMesh(경로)` → `SetStaticMesh` ([StaticMeshActor.cpp:38-61], 에이전트 추적). **UI 도 같은 자리(=spawn 직후)에서 .uasset 로드를 끼우면 동형.**

> **결론(A):** drag-drop 인프라 **완비**. 부재한 것은 (1) viewport 의 `"UIContentItem"` accept 분기, (2) UI 액터의 .uasset 로드 메서드 — **두 곳뿐.**

---

## B. UI 액터 / 화면 렌더 연결

- [x] **기존 `AUICanvasActor` 구조** — [UICanvasActor.h](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h): `AActor` 파생, `UCLASS()`+`GENERATED_BODY()`(→**팩토리 생성 가능**), `bNeedsTick=false`, 멤버는 `TWeakObjectPtr<UUICanvas> Canvas` 하나. **에셋 경로 프로퍼티 없음.**
  - [UICanvasActor.cpp `InitCanvas()`:6-22](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp): RootComponent 가 이미 UUICanvas 면 재사용, 아니면 빈 UUICanvas 새로 만들어 RootComponent 로. **즉 현 설계는 "캔버스 트리를 .Scene 에 인라인 직렬화"**(헤더 주석 :10 "Actor + Component → .Scene 직렬화"). .uasset 참조 개념 없음.
  - 등록: [BeginPlay():24-32](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp) → `InitCanvas()` → `FUICanvasManager::Get().RegisterCanvas(C)`. 해제: `EndPlay()` → `UnregisterCanvas`.
  - **현재 사용처**: [UIEditorWidget](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp) 의 라이브 트리 소유 용도뿐. **레벨에 배치하는 경로는 아직 0건**(grep: AUICanvasActor 참조 = UIEditorWidget·자기자신·vcxproj·문서). → 본 기능이 "UICanvasActor 를 레벨에 놓는" 최초 사례.
- [x] **.uasset → 라이브 UUICanvas 복원(앞 사이클 ⓪)** — **재사용 가능.**
  - 로드: [FUIAssetManager::Load(path):9-39](../KraftonEngine/Source/Engine/UI/UIAssetManager.cpp) → 경로 정규화 + 캐시 → `FAssetPackage::LoadStringPayload(..., EAssetPackageType::UI, ...)` → `UUIAsset{ SourcePath, CanvasData(JSON) }` 반환. **로드 단계는 JSON 문자열만, 트리 미생성.**
  - 복원: [FSceneSaveManager::DeserializeUITree(Json, Owner):466-490](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) → `DeserializeSceneComponentTree`(:734-770) 재귀로 라이브 트리(루트 UUICanvas) 생성. **타입 복원 = JSON `ClassName` 태그 → `FObjectFactory::Create(ClassName, Owner)`**(:742). **월드 의존 없음, standalone.**
  - 현 호출 선례: [UIEditorWidget::BuildLiveTree:168-183](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp) — `OwnerActor = CreateObject<AUICanvasActor>()` 후 `DeserializeUITree(Asset->GetCanvasData(), OwnerActor)` → `SetRootComponent(Canvas)`. **UI 액터에 .uasset 을 채우는 패턴이 이미 에디터에 존재** → spawn 경로에 그대로 이식.
  - **[사실] .uasset 포맷 = .Scene 인라인 포맷과 동일 재귀** — [SerializeUITree:451-](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) 가 컴포넌트-트리 직렬화를 그대로 재사용해 단일 .uasset JSON 을 만든다. → 인라인 직렬화와 .uasset 이 **상호 호환 포맷**(§D 설계 선택지의 전제).
- [x] **런타임 화면 렌더 자동 합류 여부 + bSyncExternal 충돌**
  - 매 프레임 구동: [Engine.cpp:188 `FUICanvasManager::Get().LayoutAll()`](../KraftonEngine/Source/Engine/Runtime/Engine.cpp) (WorldTick 후, Render 전) → [Render():216-221](../KraftonEngine/Source/Engine/Runtime/Engine.cpp) → RenderPipeline → [SimpleUIPass](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp).
  - SimpleUIPass 게이트: `BeginPass()` = `Ctx.Frame.ViewportRTV && !FUICanvasManager::Get().GetCanvases().empty()`(SimpleUIPass.cpp:117-121). **월드 타입(Editor/PIE/Game) 체크 없음.** `Execute()`(:123-198) 는 등록된 캔버스의 캐시된 `ScreenRect` 를 읽어 쿼드 생성 → viewport RTV. 텍스트는 RmlUi(UIPass) 별도 경로.
  - **자동 합류 결론**: 등록(`RegisterCanvas`)만 되면 **PIE/Game 에서 자동으로 LayoutAll+SimpleUIPass+RmlUi 에 합류한다.** 매니저/패스에 월드 격리 게이트가 없다.
  - **bSyncExternal 충돌 = 없음.** `bSyncExternal` 은 **RmlUi 텍스트 외부 동기화만** 분기한다([UICanvasManager.cpp LayoutAll→bSyncExternal=true](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp), 에디터 `LayoutCanvas` 는 false). 런타임 LayoutAll 은 true → 텍스트 정상. 에디터 격리(false)는 **UI 에디터 위젯 프리뷰 전용** 경로이며, 레벨 viewport 배치와 다른 싱글톤/호출점이라 **본 기능과 교차하지 않음**.

- **R1 [전제·최대리스크] — 등록이 BeginPlay 게이트.** [World.cpp::AddActor:368-372](../KraftonEngine/Source/Engine/GameFramework/World.cpp): `if (bHasBegunPlay && !Actor->HasActorBegunPlay()) Actor->BeginPlay();`. 에디터 월드는 `BeginPlay()` 미진입(=`bHasBegunPlay=false`; [World.cpp:572-578](../KraftonEngine/Source/Engine/GameFramework/World.cpp) — Editor 월드는 GameMode 도 안 띄움). → **편집모드 드롭 시 `RegisterCanvas` 안 됨 → SimpleUIPass `GetCanvases().empty()` → UI 미렌더.** PIE 진입 시(월드 BeginPlay → Level BeginPlay → 액터 BeginPlay, 또는 PIE 복제월드의 BeginPlay)엔 등록되어 렌더됨. **메시와의 비대칭의 근본 원인.**
- **R2 [전제] — InitCanvas 빈 캔버스 선점 주의.** `LoadFromAsset` 신설 시 `InitCanvas()` 가 먼저 빈 UUICanvas 를 RootComponent 로 박아두면, 이후 `DeserializeUITree` 결과로 교체할 때 **이중 생성/루트 충돌** 가능. 로드 경로는 InitCanvas 빈-생성 분기를 타지 않게 해야 함(BuildLiveTree 처럼 Deserialize 결과를 곧장 RootComponent 로).

---

## C. 드롭 → spawn 배선

- [x] **payload 의 .uasset 경로 추출** — 메시와 동일: `*reinterpret_cast<const FContentItem*>(payload->Data)` → `.Path` → `FPaths::ToUtf8(...)` ([FLevelViewportLayout.cpp:899-902](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp) 의 메시 코드가 그대로 템플릿).
- [x] **UI 액터 spawn + .uasset 로드 + 화면 등록 흐름** (의미 1)
  - **[설계]** [FLevelViewportLayout.cpp:895-906](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp) 에 분기 추가:
    ```
    if (payload = AcceptDragDropPayload("UIContentItem")) {
        FContentItem item = ...;
        AUICanvasActor* a = Cast<AUICanvasActor>(FObjectFactory::Create(AUICanvasActor::StaticClass()->GetName(), Editor->GetWorld()));
        a->LoadFromAsset(item.Path);          // 신규: §B 복원 로직 묶음
        Editor->GetWorld()->AddActor(a);      // 메시와 동일
    }
    ```
  - **`LoadFromAsset` 신규(핵심 구현체, §B 재사용 조립)**: `UUIAsset* asset = FUIAssetManager::Get().Load(path)` → `USceneComponent* root = FSceneSaveManager::DeserializeUITree(asset->GetCanvasData(), this)` → `SetRootComponent(Cast<UUICanvas>(root))` → (R2 회피) → `FUICanvasManager::RegisterCanvas(canvas)`.
- [x] **의미 1: 드롭 3D 좌표 무시 — 이미 합치.** 기존 메시 경로가 드롭 좌표/ray 를 안 쓰고 기본 transform 으로 `AddActor` 한다([FLevelViewportLayout.cpp:901-903](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp)). UI 액터 transform 은 화면공간이라 무의미하므로 **기본값/원점 그대로** 두면 됨. 추가 작업 없음.
- **R3 [전제] 편집모드 즉시 렌더(선택)**: 위 `LoadFromAsset` 가 `RegisterCanvas` 까지 직접 호출하면 **편집모드에서도 등록**되어 R1 우회 가능. 단 그 경우 (a) 편집모드 SimpleUIPass 가 레벨 viewport RTV 에 그려야 하고(에디터 렌더 파이프라인이 SimpleUIPass 를 도는지 별도 확인 필요), (b) RmlUi 텍스트가 게임 viewport 로 새지 않도록 `bSyncExternal` 동등 격리 필요(§E 사이클 4의 본질). **최소 구현은 R1 을 수용(PIE 에서만 렌더)** 하고 편집모드 프리뷰는 후행 사이클로 분리 권장.

---

## D. 직렬화 / 레벨 저장

현재 두 모델이 충돌 가능 — **설계 선택이 필요한 유일 지점.**

- [x] **(현행) 인라인 트리 모델** — [SerializeActor:394-397](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) 이 모든 액터의 `RootComponent` 트리를 `SerializeSceneComponentTree`(:428-449) 로 **통째 직렬화**. AUICanvasActor 의 RootComponent=UUICanvas 이므로, **레벨에 놓으면 UI 트리 전체가 .Scene 에 인라인 저장**되고 로드 시 그 인라인 트리로 복원([DeserializeSceneComponentTree:734-770](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp), InitCanvas 가 복원된 RootComponent 재사용). **그러나 "어느 .uasset 에서 왔는지" 정보는 보존 안 됨**(=스냅샷; 원본 .uasset 수정해도 배치 인스턴스 불변).
- [x] **(권고) 에셋 참조 모델 — 메시 선례 동형** — 에셋 참조 보존의 엔진 표준 패턴 존재:
  - [StaticMeshComponent.h:73-74](../KraftonEngine/Source/Engine/Component/Primitive/StaticMeshComponent.h) `UPROPERTY(Edit, Save, AssetType="StaticMesh") FSoftObjectPtr StaticMeshPath;` → **경로 문자열을 자동 직렬화**. 로드 후 `PostEditChangeProperty` 에서 `FMeshManager::LoadStaticMesh(path)` 로 재로드(StaticMeshComponent.cpp:321-339, 에이전트 추적).
  - 리플렉션 자동 직렬화: [SerializeProperties → Obj->SerializeProperties(Ar, PF_Save):499-500](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) 가 PF_Save 프로퍼티 일괄 기록(생성 리플렉션이 `FSoftObjectProperty` 등록). → **`FSoftObjectPtr UIAssetPath` 한 줄 추가 = 저장/복원 자동.**
  - 디테일 패널 드롭 선례: [EditorPropertyWidget.cpp:2225 `TryAcceptAssetPathDrop(...)`](../KraftonEngine/Source/Editor/UI/Panel/EditorPropertyWidget.cpp) — `FSoftObjectPtr` 류 프로퍼티에 콘텐츠 항목 경로 드롭을 받는 패턴이 이미 있음(향후 디테일에서 UI 에셋 교체 시 재사용 가능).
- [x] **레벨 재로드 시 재로드 경로** — 에셋참조 모델 채택 시: 로드 → `UIAssetPath` 문자열 복원 → `PostEditChangeProperty`(또는 BeginPlay)에서 `LoadFromAsset(UIAssetPath)` 호출 → 트리 재생성 → 등록. (메시의 PostEditChangeProperty 재로드와 동형.)
- **R4 [설계·결정필요] 인라인 vs 참조 택일** — 둘 다 켜면 **로드 시 인라인 트리와 .uasset 재로드가 충돌/중복**. 의미 1(=특정 .uasset 을 화면에 띄우는 액터, 원본 추종)에는 **에셋 참조 모델 권고**: (1) `AUICanvasActor` 에 `FSoftObjectPtr UIAssetPath` 추가, (2) UI 트리는 **.Scene 에 인라인 직렬화하지 않도록 억제**(예: 로드 경로의 캔버스 서브트리를 `IsSceneSerializableObject` 에서 제외하거나 transient 표식), (3) 로드 시 경로로 재구성. *주의: (2) 억제 지점은 본 진단에서 미구현 — `IsSceneSerializableObject`/RootComponent 직렬화 제외 훅을 구현 사이클에서 확정.*

---

## E. 구현 사이클 분할 + 리스크

**[정정] 프롬프트의 사이클 ②(콘텐츠 브라우저 드래그 소스)는 이미 존재(§A)** → 신규 사이클에서 제거. 재구성:

- **사이클 1 — UI 액터에 .uasset 로드+등록 능력 부여 (B 선검증, drag-drop 무관하게 독립 테스트).**
  `AUICanvasActor::LoadFromAsset(path)` 신설 = `FUIAssetManager::Load` + `FSceneSaveManager::DeserializeUITree` + `SetRootComponent` + `RegisterCanvas`(R2 회피). 임시로 디버그 커맨드/하드코딩 spawn 으로 **PIE 에서 화면 표시 확인**. → R1/R2 를 가장 먼저 깸.
- **사이클 2 — 영속 모델 결정/구현 (D).** `FSoftObjectPtr UIAssetPath` UPROPERTY(Save) 추가 + 로드 시 재구성(PostEditChangeProperty 또는 BeginPlay) + 인라인 트리 직렬화 억제(R4). 저장→로드 왕복에서 동일 .uasset 복원 확인.
- **사이클 3 — viewport 드롭 배선 (C).** [FLevelViewportLayout.cpp:895-906](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp) 에 `AcceptDragDropPayload("UIContentItem")` 분기 → `Create(AUICanvasActor)` + `UIAssetPath` 세팅(→로드 트리거) + `AddActor`. **드롭→PIE 에서 표시**까지 E2E. (드래그 소스는 §A 로 무료.)
- **사이클 4 (선택) — 편집모드 즉시 프리뷰 (R1/R3 해소).** spawn/Construction 시점 등록 + 에디터 렌더 파이프라인의 SimpleUIPass 합류 확인 + RmlUi 텍스트 격리(`bSyncExternal` 동등) 또는 ImGui 미러. **난도·리스크 최상** → 분리.

### 막힐 가능성 (리스크 등록부)
| ID | 리스크 | 근거 | 영향/완화 |
|----|--------|------|-----------|
| **R1** | 편집모드 미렌더(등록이 BeginPlay 게이트) | [World.cpp:368-372](../KraftonEngine/Source/Engine/GameFramework/World.cpp)·[UICanvasActor.cpp:24-32](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp) | 최소구현=PIE 에서만 렌더 수용 / 프리뷰는 사이클4 |
| **R2** | InitCanvas 빈 캔버스 선점→이중루트 | [UICanvasActor.cpp:6-22](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp) | 로드경로는 InitCanvas 빈생성 분기 우회(BuildLiveTree 방식) |
| **R3** | 편집모드 프리뷰 시 RmlUi 텍스트가 게임 viewport 로 누수 | bSyncExternal 격리는 LayoutAll 한정([UICanvasManager.cpp](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)); 텍스트 마운트 대상이 별 싱글톤(UIText 진단 R1 선례) | 프리뷰 시 외부동기화 게이트/ImGui 미러 |
| **R4** | 인라인 트리 ↔ .uasset 참조 직렬화 충돌/중복 | [SerializeActor:394-397](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) 가 RootComponent 트리 인라인 저장 | 참조모델 택1 + 캔버스 서브트리 직렬화 억제 |
| R5 | 편집모드 SimpleUIPass 미구동 가능성 | SimpleUIPass 는 `ViewportRTV+캔버스`만 보지만 에디터 렌더 파이프라인이 패스를 도는지 미확인 | 사이클4 진입 전 [EditorRenderPipeline](../KraftonEngine/Source/Editor/EditorRenderPipeline.h) 확인 |

---

## F. 파일 인덱스 (빠른 참조)

| 역할 | 위치 | 비고 |
|------|------|------|
| 드래그 소스(범용) | [ContentBrowserElement.cpp:462-468](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp) | `SetDragDropPayload(GetDragItemType(), &FContentItem)` |
| UI 드래그 타입 | [ContentBrowserElement.h:251](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h) | `"UIContentItem"` (이미 존재) |
| payload 본체 | [ContentItem.h](../KraftonEngine/Source/Editor/UI/ContentBrowser/ContentItem.h) | `FContentItem{Path,Name,bIsDirectory}` |
| **viewport 드롭(갭 위치)** | [FLevelViewportLayout.cpp:891-907](../KraftonEngine/Source/Editor/Viewport/Level/FLevelViewportLayout.cpp) | `ObjectContentItem` 만 accept; 여기에 분기 추가 |
| 액터 spawn API | [World.cpp:168-178](../KraftonEngine/Source/Engine/GameFramework/World.cpp) `SpawnActorByClass` / `AddActor:356-395` | `Create(...PersistentLevel)+AddActor` |
| UI 액터 | [UICanvasActor.h](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h)·[.cpp](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp) | UCLASS; BeginPlay 에서 RegisterCanvas |
| .uasset 로드 | [UIAssetManager.cpp:9-39](../KraftonEngine/Source/Engine/UI/UIAssetManager.cpp) | `Load(path)→UUIAsset{SourcePath,CanvasData}` |
| 트리 복원(⓪) | [SceneSaveManager.cpp:466-490 `DeserializeUITree`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) | standalone 재사용 가능 |
| 복원 선례 | [UIEditorWidget.cpp:168-183 `BuildLiveTree`](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp) | Create(AUICanvasActor)+DeserializeUITree+SetRootComponent |
| 화면 렌더 구동 | [Engine.cpp:188 LayoutAll / :216-221 Render](../KraftonEngine/Source/Engine/Runtime/Engine.cpp) | WorldTick→LayoutAll→Render |
| 쿼드 패스 | [SimpleUIPass.cpp:117-198](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp) | BeginPass=ViewportRTV&&!Canvases.empty(); 월드게이트 없음 |
| 등록/레이아웃 | [UICanvasManager.cpp](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp) | RegisterCanvas / LayoutAll(bSyncExternal=true) |
| 인라인 직렬화 | [SceneSaveManager.cpp:385-449](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) | RootComponent 트리 통째 저장 |
| 에셋참조 선례 | [StaticMeshComponent.h:73-74](../KraftonEngine/Source/Engine/Component/Primitive/StaticMeshComponent.h) | `FSoftObjectPtr ...Path` UPROPERTY(Save) |
| 에셋경로 드롭 선례 | [EditorPropertyWidget.cpp:2225](../KraftonEngine/Source/Editor/UI/Panel/EditorPropertyWidget.cpp) | `TryAcceptAssetPathDrop(...)` |

---

### 한 줄 결론
**입력 인프라(콘텐츠 브라우저 드래그 + viewport 드롭 + 액터 spawn)는 메시로 완비되어 재사용 가능하고, UI .uasset 드래그도 이미 가능하다. 신규 작업은 (1) viewport 드롭 핸들러에 `"UIContentItem"` 분기, (2) `AUICanvasActor::LoadFromAsset`(=기존 Load+DeserializeUITree 조립), (3) 영속을 위한 `FSoftObjectPtr UIAssetPath` 와 인라인 직렬화 억제 — 세 갈래뿐.** 최대 변수는 **편집모드 렌더(R1, BeginPlay 게이트)**: 최소구현은 PIE 렌더로 수용하고 편집모드 프리뷰는 후행 사이클로 분리.
