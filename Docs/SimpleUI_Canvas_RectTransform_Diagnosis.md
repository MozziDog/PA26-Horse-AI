# 계층형 UI (Canvas→Group→Element + RectTransform) + 런타임 드래그 에디터 — 이식 진단

> 작성일: 2026-06-06 · 대상: KraftonEngine (자체 D3D11 엔진) · 사이클: **진단 단계 (구현 없음)**
> 방법: 10개 차원 병렬 스캔 + 적대적 교차검증(20 에이전트) → 근거는 모두 `파일:라인`.
> 표기: **[사실]** = 코드로 확인된 현황 · **[설계]** = 신규 모델 권고(코드 근거 아님) · **[정정]** = 트리거 프롬프트 전제 오류 정정 · **[결정]** = 사용자 확정 사항.

> ### 확정된 설계 결정 (2026-06-06)
> - **[결정] RmlUi 관계 = 별도 병존.** 신규 시스템은 RmlUi와 독립 신설, 단 `FUIInputCaptureState` 계약에 합류해 마우스 충돌 중재(F1). RmlUi 유지.
> - **[결정] 노드 트리 = `USceneComponent` 서브클래스.** 사이클검사·GC·dirty 전파 무료 재사용. ⚠️ 트레이드오프: 미사용 3D `FTransform`/소켓 머신이 딸려옴 → UI는 3D `RelativeTransform`을 무시하고 별도 `FUIRectTransform`으로 레이아웃, 3D world-matrix 경로는 우회(A2·C5 참조).
> - **[결정] 저장 포맷 = `.Scene` 임베드.** Element를 리플렉트 UObject로, RectTransform을 `PF_Save` 프로퍼티로 → 기존 `SceneSaveManager` 자동 영속. `SerializeVector2`만 추가(E4).

---

## 0. 결론 요약 (먼저 읽을 것)

### ⚠️ 트리거 프롬프트의 두 전제가 틀렸다 — 설계 방향이 바뀐다
- **[정정] "UI 시스템은 현재 전무" → 거짓.** 이미 **RmlUi(HTML/RCSS) 기반 런타임 UI**가 완비돼 있다: `UUIManager`(싱글턴+FGCObject) + `UUserWidget`(= `Rml::ElementDocument` 1:1 래퍼), D3D11 쿼드 렌더러(`FRmlRenderInterfaceD3D11`), 입력 캡처 계약(`FUIInputCaptureState`), Lua 바인딩까지. ([UIManager.h:96](../KraftonEngine/Source/Engine/UI/UIManager.h), [UserWidget.h:56](../KraftonEngine/Source/Engine/UI/UserWidget.h), [Engine.cpp:102](../KraftonEngine/Source/Engine/Runtime/Engine.cpp))
- **[정정] "DirectX/Vulkan" → Vulkan 없음.** 렌더러는 **D3D11 단일 백엔드**다(`D3D11CreateDeviceAndSwapChain`, `D3D_FEATURE_LEVEL_11_0`). Vulkan/D3D12/RHI 추상화 **전무** — raw `ID3D11DeviceContext` 직접 호출. ([D3DDevice.cpp:151,191](../KraftonEngine/Source/Engine/Render/Device/D3DDevice.cpp))

### 핵심 판정 (F1 선반영)
- **[설계] 신규 Canvas/Group/Element 모델은 RmlUi 위에 얹지 말고 "별도 경량 시스템"으로 신설한다.** RmlUi 모델층은 pivot/anchor/px-RectTransform 개념이 없고 레이아웃/히트테스트/드래그가 전부 Rml 내부라 삽입 seam이 없다. **모델은 신규, 플럼빙(plumbing)만 재사용**한다:
  1. **렌더**: `FRmlRenderInterfaceD3D11` 쿼드-배치 패턴 + `FUIPass` 슬롯 재사용 ([UIPass.cpp:25](../KraftonEngine/Source/Engine/Render/RenderPass/UIPass.cpp))
  2. **입력 분리**: `GameViewportClient::ProcessInput` ↔ `FUIInputCaptureState` 계약에 반드시 끼워넣기 ([GameViewportClient.cpp:109](../KraftonEngine/Source/Engine/Viewport/GameViewportClient.cpp))
  3. **트리/수명**: `USceneComponent` 부모-자식 패턴 + `UObject`/GC keepalive 그대로 모방
  4. **해상도**: `FViewport`→`FFrameContext.ViewportWidth/Height` 파이프라인 ([FrameContext.cpp:35](../KraftonEngine/Source/Engine/Render/Types/FrameContext.cpp))
  5. **직렬화**: 리플렉션 기반 `.Scene` JSON(`SerializeProperties`) 재사용
- **[사실] 좌표계가 목표 모델과 정확히 일치한다.** UI 픽셀 공간 = 좌상단 원점, Y-down, pixel→NDC를 VS에서 `(px/W)*2-1`, `1-(py/H)*2`로 변환(투영행렬 없음). 즉 pivot 좌상단=(0,0) 모델과 충돌 없음. ([RmlUi.hlsl:28-34](../KraftonEngine/Shaders/UI/RmlUi.hlsl))

---

## A. 인프라 스캔 — UI 트리를 어디에 얹을지

### A1. 객체 생성/수명 패턴
- [x] **생성**: `UObjectManager::Get().CreateObject<T>(Outer)` — UE의 `NewObject<>`에 해당(이 엔진엔 `NewObject<>` 없음). Outer는 "논리적 스코프, 소유 아님". ([Object.h:248-261](../KraftonEngine/Source/Engine/Object/Object.h))
- [x] **수명**: 2-phase 지연 GC. `DestroyObject()`는 `BeginDestroy()+MarkPendingKill()+RequestGarbageCollection()`만, 실제 해제는 mark/sweep. **즉시 delete/refcount 아님.** ([Object.h:266-285](../KraftonEngine/Source/Engine/Object/Object.h))
- [x] **keepalive 규율(중요)**: 루트(`RF_RootSet` UObject 또는 등록된 `FGCObject`)에서 `AddReferencedObjects()`로 도달 가능해야 sweep 생존. UI 트리는 **루트 매니저(FGCObject)가 Canvas를 보고하고, 각 노드가 자식을 보고**해야 한다. ([GarbageCollection.h:79-110](../KraftonEngine/Source/Engine/Object/GarbageCollection.h), 선례 [AActor.cpp:874](../KraftonEngine/Source/Engine/GameFramework/AActor.cpp), [SceneComponent.cpp:241](../KraftonEngine/Source/Engine/Component/SceneComponent.cpp))
- [x] **스마트포인터**: `TObjectPtr`(소유 의도, AddReferencedObjects가 추적) / `TWeakObjectPtr`(SerialNumber 검증 back-ref, PendingKill 거름) / `TSubclassOf`. **풀/Arena/freelist 없음** — UObject `new/delete`=malloc/free. → UObject로 만드는 이유는 GC+리플렉션+직렬화, 메모리 이득 아님. ([ObjectPtr.h](../KraftonEngine/Source/Engine/Object/Ptr/ObjectPtr.h), [WeakObjectPtr.h:45](../KraftonEngine/Source/Engine/Object/Ptr/WeakObjectPtr.h), [Object.h:140-157](../KraftonEngine/Source/Engine/Object/Object.h))

### A2. 재사용 가능한 부모-자식 트리 — **있음 (`USceneComponent`)**
- [x] **`USceneComponent`가 Canvas→Group→Element를 거의 1:1로 거울이다.** API: `AddChild`/`SetParent`/`GetParent`/`GetChildren`/`RemoveChild`/`IsDescendantOf`. 저장: `ChildComponents: TArray<USceneComponent*>`(부모→자식) + `ParentComponent: TWeakObjectPtr`(자식→부모). 사이클/자기부착 거부 내장. ([SceneComponent.h:26-45,145-146](../KraftonEngine/Source/Engine/Component/SceneComponent.h), [SceneComponent.cpp:249-286](../KraftonEngine/Source/Engine/Component/SceneComponent.cpp))
- [x] **레이아웃에 필요한 dirty 전파 레시피까지 이미 있음**: `MarkTransformDirty`가 자식으로 재귀, `UpdateWorldMatrix`가 `child = relative * parent`를 지연 캐시 — GlobalScale/RectTransform 재계산과 동형. ([SceneComponent.cpp:346-409,543-557](../KraftonEngine/Source/Engine/Component/SceneComponent.cpp))
- [x] **`AActor`는 부적합 모델**(평면 컴포넌트 리스트), RmlUi `UUserWidget` 트리도 부적합(Rml 내부 트리, UObject 자식 리스트 없음). ([AActor.h:179-199](../KraftonEngine/Source/Engine/GameFramework/AActor.h), [UserWidget.h:55-87](../KraftonEngine/Source/Engine/UI/UserWidget.h))
- [x] **[결정] `USceneComponent` 서브클래스 채택.** `SetParent/AddChild/GetChildren/IsDescendantOf`(사이클검사)·GC keepalive(`AddReferencedObjects`)·`MarkTransformDirty` 자식재귀를 그대로 상속. ⚠️ **트레이드오프**: 미사용 3D `FTransform RelativeTransform`/소켓이 따라옴 → UI 노드는 그 3D transform/world-matrix 경로를 **사용하지 않고**, 별도 `FUIRectTransform`만으로 레이아웃(C5). keepalive 루트는 여전히 필요(F2 참조).

### A3. per-frame 루프 진입점·순서 — **단일 스레드, 완전 동기**
- [x] **체인**: [Launch.cpp:50](../KraftonEngine/Source/Engine/Runtime/Launch.cpp) → `FEngineLoop::Run` 루프(PumpMessages→Timer.Tick→`GEngine->Tick`) → `UGameEngine::Tick` → `TickFrameStart`(입력 폴) → `TickFrameBody` → `ProcessPendingTransition`. ([EngineLoop.cpp:60-76](../KraftonEngine/Source/Engine/Runtime/EngineLoop.cpp), [GameEngine.cpp:66-93](../KraftonEngine/Source/Engine/Runtime/GameEngine.cpp))
- [x] **`TickFrameBody` 순서 = Audio → `WorldTick(dt)` → `Render(dt)`** (이 순서가 레이아웃 패스 삽입의 핵심). ([Engine.cpp:174-179](../KraftonEngine/Source/Engine/Runtime/Engine.cpp))
- [x] **액터/컴포넌트 틱**: `UWorld::Tick`이 4개 TickGroup(PrePhysics→DuringPhysics→PostPhysics→PostUpdateWork)+TickPlayerCamera. ([World.cpp:635-672](../KraftonEngine/Source/Engine/GameFramework/World.cpp), [TickFunction.cpp:98-147](../KraftonEngine/Source/Engine/Core/TickFunction.cpp))
- [x] **[사실] 기존 RmlUi는 게임틱에서 틱되지 않는다.** 유일한 per-frame 구동은 렌더패스 내부 `FUIPass::Execute → UUIManager::Render`이고, 거기서 `SetDimensions/ProcessInput/Update/Render`가 한 덩어리로 실행 = **레이아웃과 렌더가 융합**돼 있음(신규 모델은 이 안티패턴을 피한다). ([UIPass.cpp:25](../KraftonEngine/Source/Engine/Render/RenderPass/UIPass.cpp), [UIManager.cpp:811-835](../KraftonEngine/Source/Engine/UI/UIManager.cpp))

### A4. 수학 타입 (Vec2 / Rect)
- [x] **`FVector2` 있음** ([Vector.h:222](../KraftonEngine/Source/Engine/Math/Vector.h)): X/Y(+U/V/Data[2] union), `Length/Normalize/Dot`, `+ -`(벡터·스칼라), `* /`(**스칼라 전용**).
- [x] **⚠️ 성분별(component-wise) 벡터곱 없음.** `ParentSize*anchor`, `size*pivot`은 `FVector2{a.X*b.X, a.Y*b.Y}` 수동 작성 필요. 선례 [ParticleVertexFactory.cpp:440](../KraftonEngine/Source/Engine/Render/Particle/ParticleVertexFactory.cpp), 3D용 헬퍼만 존재([FbxAnimationImporter.cpp:891](../KraftonEngine/Source/Engine/Mesh/Importer/Fbx/FbxAnimationImporter.cpp)). → **[설계] `FVector2` 성분곱 헬퍼 1개 추가 권장.**
- [x] **`FRect` 있음 = `{float X,Y,Width,Height}`** (pos+size, min/max 아님) → RectTransform 출력(origin=FinalPos, size)과 정확히 일치. **단 `Editor/Slate` 레이어 소속** → 런타임 UI가 쓰면 Engine→Editor 의존 발생(이미 `GameViewportClient.h`가 끌어씀). → **[설계] Engine측 소형 rect 신설 또는 pos+size를 2×FVector2로 보관 권장.** ([SWindow.h:11](../KraftonEngine/Source/Editor/Slate/SWindow.h), [GameViewportClient.h:5,44](../KraftonEngine/Source/Engine/Viewport/GameViewportClient.h))
- [x] **`FIntPoint` 없음.** 해상도는 엔진 전반 float 보관(`Frame.ViewportWidth/Height`) → GlobalScale 나눗셈에 문제없음.

### A5. 마우스 입력 경로 — 히트테스트의 토대
- [x] **폴링 방식**(메시지 디스패치 아님): `InputSystem::Tick()`이 매 프레임 `GetCursorPos`+`GetAsyncKeyState(0..255)` 폴. WndProc은 `WM_MOUSEWHEEL`/`WM_INPUT`(raw)만 처리, `WM_MOUSEMOVE/LBUTTONDOWN` **미처리**. ([InputSystem.cpp:16-33](../KraftonEngine/Source/Engine/Input/InputSystem.cpp), [WindowsApplication.cpp:31-99](../KraftonEngine/Source/Engine/Platform/WindowsApplication.cpp), 폴 지점 [Engine.cpp:171](../KraftonEngine/Source/Engine/Runtime/Engine.cpp))
- [x] **히트테스트 좌표 단일 진입점**: `InputSystem::GetMouseClientPos()` = **클라이언트 픽셀, 좌상단 원점**(`ScreenToClient`). 기존 RmlUi도 이걸로 히트테스트(폴백) — 단 우선은 `Frame.CursorViewportX/Y`(이것도 `GetMouseClientPos`에서 파생). 둘 다 client/viewport px. ([InputSystem.h:85-94](../KraftonEngine/Source/Engine/Input/InputSystem.h), [UIManager.cpp:847-859](../KraftonEngine/Source/Engine/UI/UIManager.cpp), [GameRenderPipeline.cpp:203-215](../KraftonEngine/Source/Engine/Runtime/GameRenderPipeline.cpp))
- [x] **드래그 프리미티브 기성품**: `GetLeftDragging()/GetLeftDragVector()` + 5px threshold, 스냅샷에도 `bLeftDragging/LeftDragVector`. `MouseDeltaX()/Y()`는 **스크린 px 델타**. ([InputSystem.h:95-113](../KraftonEngine/Source/Engine/Input/InputSystem.h), [InputSystem.cpp:232-257](../KraftonEngine/Source/Engine/Input/InputSystem.cpp))

---

## B. 렌더 경로

### B1. 2D/스크린스페이스 쿼드 드로우 경로 — **이미 존재(생산 코드)**
- [x] **최근접 아날로그 = `FRmlRenderInterfaceD3D11`**: `CompileGeometry`가 interleaved `POSITION(float2)+COLOR(float4)+TEXCOORD(float2)` VB + R32 IB 빌드, `RenderGeometry`가 `Shaders/UI/RmlUi.hlsl` 바인드 + NoDepth/AlphaBlend/SolidNoCull + `OMSetRenderTargets(ViewportRTV)` + `DrawIndexed`. **완전 범용 텍스처드 쿼드 서브밋.** ([UIManager.cpp:200-315](../KraftonEngine/Source/Engine/UI/UIManager.cpp))
- [x] **더 단순한 아날로그 = `FFontGeometry::AddScreenText`**(글자당 4v/6i, 픽셀→클립 직접). RmlUi 안 끌어오고 모방 가능. ([FontGeometry.cpp:159-231](../KraftonEngine/Source/Engine/Render/Geometry/FontGeometry.cpp))

### B2. 좌표계 규약 — **목표 모델과 일치**
- [x] **좌상단 원점 + Y-down 픽셀.** pixel→NDC를 VS에서 `x=(px/W)*2-1; y=1-(py/H)*2` (투영행렬 없음, CB엔 ViewportSize+Translation+기본단위 Transform만). 동일 공식이 FontGeometry/FullscreenTriangleVS에도. D3D 뷰포트 TopLeft(0,0). ([RmlUi.hlsl:28-34](../KraftonEngine/Shaders/UI/RmlUi.hlsl), [FontGeometry.cpp:187-195](../KraftonEngine/Source/Engine/Render/Geometry/FontGeometry.cpp), [D3DDevice.cpp:228](../KraftonEngine/Source/Engine/Render/Device/D3DDevice.cpp))
- ⇒ **pivot 좌상단=(0,0), anchor (0,0)=부모 좌상단** 모델 그대로 성립. Y축 뒤집기 불필요.

### B3. 텍스트 라벨 렌더 수단 (예: HP바 value text)
- [x] **월드 텍스트 경로**(완비·활성): `UTextRenderComponent→FTextRenderSceneProxy→AddWorldText`(카메라 빌보드, 3D). **스크린 라벨 아님.** ([FontGeometry.cpp:98-157](../KraftonEngine/Source/Engine/Render/Geometry/FontGeometry.cpp))
- [x] **스크린 텍스트 경로 존재하나 사실상 死코드**: `AddScreenText`는 픽셀→클립 변환까지 되지만 유일 피더 `FScene::AddOverlayText` **호출처 0개**, 셰이더가 **색 cyan 하드코딩**+알파=R채널, 23px 고정 그리드 비트맵 아틀라스(비례/커닝 없음). → 재사용하려면 (a)호출 훅, (b)라벨별 color/size 파라미터, (c)아틀라스 개선 필요. ([FScene.cpp:383](../KraftonEngine/Source/Engine/Render/Scene/FScene.cpp), [OverlayFont.hlsl:14-22](../KraftonEngine/Shaders/UI/OverlayFont.hlsl), [ResourceTypes.h:12-26](../KraftonEngine/Source/Engine/Core/Types/ResourceTypes.h))
- [x] **RmlUi는 자체 FreeType 폰트로 TTF 렌더**(Maplestory Bold.ttf), 엔진엔 글리프/아틀라스 노출 안 됨 → 직접 재사용 불가. **신규 모델이 RmlUi 문서를 마운트하면 고품질 TTF 라벨은 공짜.** ([UIManager.cpp:574-579](../KraftonEngine/Source/Engine/UI/UIManager.cpp))
- ⇒ **[설계] 별도 렌더러면 `AddScreenText` 확장이 재사용 타깃**(색/크기 파라미터화 + 에디터 게이트 제거).

### B4. UI 드로우 콜 제출 지점
- [x] **전용 렌더패스 `FUIPass`(`ERenderPass::UI`)**, `REGISTER_RENDER_PASS`로 등록, enum 순서 = 실행 순서. UI는 **OverlayFont 다음, GammaCorrection 직전**(3D·포스트·블룸·FXAA 이후, Present 전). `BeginPass`가 `ViewportRTV && HasViewportWidgets()`로 게이팅. ([UIPass.cpp:8-28](../KraftonEngine/Source/Engine/Render/RenderPass/UIPass.cpp), [RenderTypes.h:62-72,141](../KraftonEngine/Source/Engine/Render/Types/RenderTypes.h), [RenderPassRegistry.cpp:27-31](../KraftonEngine/Source/Engine/Render/RenderPass/RenderPassRegistry.cpp))
- ⇒ **[설계] 신규 UI는 (a) `FUIPass`에 인접한 새 `ERenderPass` 슬롯**으로 draw-only 등록 또는 (b) `FUIPass` 확장. 단 **레이아웃은 여기서 하지 말 것**(C 참조).
- [ ] **미결**: UI는 오프스크린 `FViewport` RTV에 그린 뒤 백버퍼로 stretch-blit. UI가 월드와 함께 스케일될지 vs 백버퍼 네이티브 해상도로 그릴지 확인 필요(GlobalScale 산식에 영향). 단 standalone에선 viewport size == client size라 1:1.

---

## C. 레이아웃 계산 패스 설계

### C1. Layout Pass 진입점 — **별도 패스 권장, 후보 명시**
- [x] **1순위(권장) = `UEngine::TickFrameBody`의 `WorldTick(dt)`와 `Render(dt)` 사이**([Engine.cpp:177↔178](../KraftonEngine/Source/Engine/Runtime/Engine.cpp)). 게임스레드, 게임플레이 정산 후·렌더 제출 전. `TickFrameBody`는 `UEngine`/`UGameEngine`/`UObjViewerEngine` 공유 → 단일 훅으로 커버. **드래그(position 변경)도 같은 패스로 흐름.**
- [x] **2순위 = `UWorld::Tick` 말미**([World.cpp:672](../KraftonEngine/Source/Engine/GameFramework/World.cpp)) — 비권장: per-world 다중 호출 + `bPaused` 시 조기반환으로 일시정지 메뉴 레이아웃이 멈춤.
- [x] **3순위 = 렌더패스 내부**(RmlUi식) — 비권장: 레이아웃과 GPU 제출 융합.
- ⇒ **결론: 레이아웃은 1순위(게임스레드 LayoutTick), 그리기는 B4의 draw-only 패스. 융합 금지.**

### C2. 계산식 검증 — 엔진 규약과 충돌 없음
- [x] **[설계] `AnchorPx = ParentSize * anchor` → `FinalPos = ParentOrigin + AnchorPx + position − (size * pivot)`** 그대로 사용 가능. 근거: 좌표계가 좌상단/Y-down(B2)이라 부호·축 보정 불필요. **단 두 `*`는 성분별이라 수동 곱 또는 헬퍼 필요(A4).**
- [x] **[정정] 이 식의 RectTransform/anchor/pivot/FinalPos/GlobalScale은 현재 코드에 없음** — 전부 신규 설계물(grep 0건). 진단은 "엔진이 이 식을 *수용 가능*"임을 확인한 것.

### C3. 트리 순회 방향
- [x] **Top-down 확정**: 부모 origin→자식으로 내려가며 누적(`UpdateWorldMatrix`의 `child = relative * parent`와 동형). 루트 Canvas origin=(0,0). ([SceneComponent.cpp:346-409](../KraftonEngine/Source/Engine/Component/SceneComponent.cpp))

### C4. 더티 플래그 vs 매 프레임 전체 재계산
- [x] **[설계] MVP는 "매 프레임 전체 top-down 재계산" 권장**(트리 수십~수백 노드 규모, 단순·버그 적음). 더티 전파가 필요해지면 **이미 있는 `MarkTransformDirty` 자식-재귀 패턴**을 그대로 채택(조기 도입은 오버엔지니어링). ([SceneComponent.cpp:543-557](../KraftonEngine/Source/Engine/Component/SceneComponent.cpp))

### C5. RectTransform 분리 여부 (Element=논리 vs Slot=좌표)
- [x] **결론: RectTransform을 Element 노드의 값-구조체 멤버로 둔다(분리 안 함).** 엔진 선례가 좌표를 노드에 융합: `USceneComponent`가 `FTransform RelativeTransform`을 직접 멤버로 보유(별도 Slot 객체 없음). 별도 Slot/Transform 객체는 UE식 과설계 — 이 코드베이스 결을 거스름. ([SceneComponent.h:152-154](../KraftonEngine/Source/Engine/Component/SceneComponent.h))
- [x] **[결정] 서브클래스 채택과 정합**: Element는 `USceneComponent`를 상속하므로 부모의 3D `RelativeTransform`을 그대로 들고 오지만 **UI는 이를 쓰지 않고** 새 멤버 `FUIRectTransform{pivot,anchor,position,size}`만으로 레이아웃한다. 즉 좌표 융합 패턴은 유지하되, 3D 필드는 dead-weight로 무시(레이아웃/드로우/히트테스트는 전부 `FUIRectTransform`→스크린 rect 경로).

---

## D. Reference Resolution / GlobalScale 통합 지점

### D1. RefRes(1920×1080) 전역 설정 위치
- [x] **`FProjectSettings`에 `FUIOption{RefResX=1920, RefResY=1080}` 추가 권장.** 기존 싱글턴, 중첩 옵션 구조체(`FShadowOption` 등) + JSON 직렬화(`Settings/ProjectSettings.ini`는 실제 JSON), 시작 시 1회 로드. 16:9 하드상수면 신규 UI 헤더 constexpr도 허용. ([ProjectSettings.h:12-72](../KraftonEngine/Source/Engine/Core/ProjectSettings.h), 로드 [GameEngine.cpp:26](../KraftonEngine/Source/Engine/Runtime/GameEngine.cpp))

### D2. GlobalScale 산출 패스 — **리사이즈당 1회**
- [x] **`GlobalScale = ClientHeight / 1080.0f`를 `UEngine::OnWindowResized(W,H)`에서 1회 계산**([Engine.cpp:218-227](../KraftonEngine/Source/Engine/Runtime/Engine.cpp)). 모든 리사이즈가 통과하는 단일 깔때기: `WM_SIZE`→`OnResizedCallback`→`GEngine->OnWindowResized`→스왑체인 리사이즈(+게임은 `StandaloneViewport->RequestResize`). 매 프레임 해상도는 `FFrameContext.ViewportWidth/Height`로 읽힘. ([WindowsApplication.cpp:66-77](../KraftonEngine/Source/Engine/Platform/WindowsApplication.cpp), [EngineLoop.cpp:32-38](../KraftonEngine/Source/Engine/Runtime/EngineLoop.cpp), [GameEngine.cpp:95-106](../KraftonEngine/Source/Engine/Runtime/GameEngine.cpp), [FrameContext.cpp:35-38](../KraftonEngine/Source/Engine/Render/Types/FrameContext.cpp))
- [ ] **미결**: GlobalScale 저장처 = `UUIManager`(SetDimensions 이미 소유) vs 신규 UI 루트. → 별도 시스템이면 신규 루트.

### D3. 스케일 적용 단일 지점 — **레이아웃 픽셀 결과에 곱한다**
- [x] **결론: 렌더 투영이 아니라 레이아웃 단계에서 곱한다.** 근거: (1) 2D 경로엔 ortho 행렬이 없어 "투영 스케일"=셰이더 수정이 됨, (2) 기존 단순 경로도 픽셀 공간에서 스케일(`CharW=23*Scale` 후 픽셀→클립), (3) 픽셀→NDC가 라이브 ViewportSize로 나누므로 스케일된 픽셀이 자동 정합, (4) **드래그/히트테스트가 스케일된 픽셀 rect를 쓰므로 렌더·입력 일관성 유지.** 3D 카메라 투영(AspectRatio만 소비)엔 절대 GlobalScale 넣지 말 것. ([FontGeometry.cpp:167-168](../KraftonEngine/Source/Engine/Render/Geometry/FontGeometry.cpp), [UIManager.cpp:819-822,292-300](../KraftonEngine/Source/Engine/UI/UIManager.cpp), [CameraComponent.cpp:60-63](../KraftonEngine/Source/Engine/Component/Camera/CameraComponent.cpp))

### D4. 16:9 외 비율 처리 방침
- [x] **결론: (a) 무시/균일스케일** 채택(프롬프트의 "단일 GlobalScale 균일 확대" + "오버엔지니어링 금지"에 정합). `GlobalScale=H/1080`만 계산, 비-16:9는 UI 레이어 overflow 허용. **현재 비율 강제·레터박스 전무**(WM_GETMINMAXINFO 없음, `BlitToBackBuffer`는 무보정 stretch). 레터박스 원하면 기존 카메라 `ApplyLetterboxAspect` 선례 재사용 가능(더 무거움). ([WindowsApplication.cpp:66-89](../KraftonEngine/Source/Engine/Platform/WindowsApplication.cpp), [Renderer.cpp:154-173](../KraftonEngine/Source/Engine/Render/Pipeline/Renderer.cpp), [GameRenderPipeline.cpp:17-32](../KraftonEngine/Source/Engine/Runtime/GameRenderPipeline.cpp))

> 참고: RmlUi엔 `SetDensityIndependentPixelRatio(dp_ratio)`가 이미 있고(=균일 UI 스케일) **현재 호출 안 함**. RmlUi를 쓴다면 `dp_ratio = H/1080`이 곧 GlobalScale. 별도 시스템이면 자체 산식. ([Context.h:42-54](../KraftonEngine/ThirdParty/RmlUi/Include/RmlUi/Core/Context.h))

---

## E. 런타임 드래그 에디터

### E1. 마우스 → UI 트리 히트테스트 위치
- [x] **신규 코드 필요(재사용 가능한 2D point-in-rect 없음).** 기존 `Contains`는 3D `FBoundingBox::IsContains`뿐, RmlUi는 히트테스트를 `RmlContext->ProcessMouseMove` 내부에 은닉. **알고리즘[설계]**: 레이아웃 패스가 만든 각 Element 스크린 rect(`FinalPos`+`size*GlobalScale`)로 트리 순회, 마우스 포함 + 최상위(최후-그림/최심 자식) Element 반환. ([UIManager.cpp:861-863](../KraftonEngine/Source/Engine/UI/UIManager.cpp), [EngineTypes.h:105-117](../KraftonEngine/Source/Engine/Core/Types/EngineTypes.h))
- [x] **순회/최근접 선택 구조는 기즈모 피커 모방**, 선택 소유는 `FSelectionManager` 패턴 모방. ([GizmoComponent.cpp:353-410](../KraftonEngine/Source/Engine/Component/Debug/GizmoComponent.cpp), [EditorViewportClient.cpp:707-758](../KraftonEngine/Source/Editor/Viewport/EditorViewportClient.cpp), [SelectionManager.h:20-55](../KraftonEngine/Source/Editor/Selection/SelectionManager.h))

### E2. 드래그 시 position 갱신 지점 (anchor/pivot 고정)
- [x] **`position`만 변경.** 패턴은 `UGizmoComponent::UpdateLinearDrag`(첫 프레임 grab 기준 저장→이후 프레임 델타 적용) 모방. 소유=신규 UI 서브시스템의 per-frame update, 입력 진입=`UGameViewportClient::ProcessInput`. ([GizmoComponent.cpp:475-502,318-328](../KraftonEngine/Source/Engine/Component/Debug/GizmoComponent.cpp), [GameViewportClient.cpp:92](../KraftonEngine/Source/Engine/Viewport/GameViewportClient.cpp))
- [x] **⚠️ 핵심: 마우스 델타는 스크린 px**(`MouseDeltaX/Y`, 선언 [InputSystem.h:19-20](../KraftonEngine/Source/Engine/Input/InputSystem.h)). FinalPos가 나중에 `*GlobalScale` 되므로 **델타를 GlobalScale로 나눠서** position에 더해야 함: `position += FVector2(dx,dy) / GlobalScale`.

### E3. 에디터 모드 ON/OFF + 게임 입력 분리 — **기존 계약에 끼워넣기**
- [x] **입력 분리 이미 구현됨**: `UUIManager::GetViewportInputCaptureState()`가 위젯별 플래그(`bWantsMouse/bBlocksGameInput/...`)를 OR합산 → `UGameViewportClient::ProcessInput`이 매 프레임 소비해 `UIOnly||bBlocksGameInput`이면 게임 스냅샷 차단, `bWantsMouse`면 마우스 키 제거 후에야 `PlayerController`로 전달. **신규 UI-에디터모드는 이 계약에 동일하게 플러그인**(자체 capture-state를 합산에 기여 또는 `EGameInputMode::UIOnly` 설정). ([GameViewportClient.cpp:92-131](../KraftonEngine/Source/Engine/Viewport/GameViewportClient.cpp), [UIManager.cpp:642-660](../KraftonEngine/Source/Engine/UI/UIManager.cpp), enum [GameViewportClient.h:19-24](../KraftonEngine/Source/Engine/Viewport/GameViewportClient.h))
- [x] **모드 토글 선례 = PIE Possessed/Ejected(F8)**: 단일 enum+핫키, enter/exit 시 possess 토글·raw마우스 해제·커서 표시. UI-에디터모드를 같은 모양으로. ([EditorEngine.cpp:525-566,216-218](../KraftonEngine/Source/Editor/EditorEngine.cpp))
- [x] **주의**: ImGui `WantCaptureMouse`(에디터 툴)와 `FUIInputCaptureState`(인게임 UI)는 **별개 두 계층**. 신규 모델은 후자에 속함. ImGui GuiState는 게임 스냅샷을 게이팅하지 않음. ([EditorMainPanel.cpp:1152-1157](../KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp))

### E4. 배치 저장/로드 포맷 — 필요, 기존 직렬화 재사용
- [x] **[결정] `.Scene` 임베드 확정.** Element를 리플렉트 UObject(=`USceneComponent` 서브클래스)로 만들고 `FUIRectTransform` 필드를 `PF_Save` 프로퍼티로 노출 → 기존 `FSceneSaveManager`(`Obj->SerializeProperties(Ar, PF_Save)` 저장 / FProperty 순회 로드)가 **자동 영속**. 신규 직렬화 메커니즘 금지. ⚠️ 단 `USceneComponent`는 OnPostLoad에서 부모/자식 토폴로지를 명시적으로 재구축하므로, UI 트리 토폴로지도 동일하게 명시 재구축 필요([SceneComponent.cpp:163-170](../KraftonEngine/Source/Engine/Component/SceneComponent.cpp)). ([SceneSaveManager.cpp:451-461,761-790](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp), [JsonArchive.h:138-265](../KraftonEngine/Source/Engine/Serialization/JsonArchive.h))
- [x] **⚠️ 갭**: 직렬화기에 `FVector`/`FVector4`는 있으나 **2성분 `SerializeVector2` 없음** → ~3줄 추가 필요. 경량 독립 파일을 원하면 `NodeEditor.json`(id→{x,y} 맵) 형식이 선례. ([NodeEditor.json](../KraftonEngine/NodeEditor.json))

---

## F. 리스크 & 구현 사이클 분할

### F1. 가장 결합도 높은 통합 지점 (1~2곳)
1. **입력 캡처 계약 `GameViewportClient::ProcessInput ↔ FUIInputCaptureState`** — 신규 UI가 마우스를 게임에서 뺏으려면 **반드시** 이 계약에 합류해야 함. 안 그러면 RmlUi/게임/신규UI가 단일 마우스 스트림을 두고 충돌. ([GameViewportClient.cpp:109-119](../KraftonEngine/Source/Engine/Viewport/GameViewportClient.cpp))
2. **RmlUi와의 공존(병존 확정)** — 둘 다 `ERenderPass::UI` 인근 슬롯·단일 마우스 스트림 공유. **[결정] 병존**이므로 동시 표시 시 (a) 입력 소유권: 두 시스템의 capture-state를 `FUIInputCaptureState` 합산에 함께 기여, (b) Z순서: draw 패스 순서로 신규 UI를 RmlUi 위/아래 어디에 둘지 명시 필요.

### F2. 미해결 의문점
- [x] ~~신규 UI vs RmlUi 병존/은퇴~~ → **[결정] 별도 병존** (RmlUi 유지, 신규는 `FUIInputCaptureState` 계약 합류).
- [x] ~~노드 트리 베이스~~ → **[결정] `USceneComponent` 서브클래스** (3D 필드 무시, A2·C5).
- [x] ~~직렬화 포맷~~ → **[결정] `.Scene` 임베드** (E4).
- [ ] **남은 의문 1 — Canvas 루트 소유/루팅 주체**: 신규 싱글턴 FGCObject 매니저 vs (서브클래스이므로) **소유 Actor의 컴포넌트로 부착해 `.Scene`과 함께 직렬화**. 후자가 `.Scene` 임베드 결정과 정합적 → **소유 Actor(예: HUD/Canvas Actor)에 RootComponent로 부착** 검토 권장.
- [ ] **남은 의문 2 — 렌더 타깃 표면**: 인게임 에디터가 오프스크린 `FViewport`(월드와 함께 blit) vs 백버퍼 직접 — GlobalScale 기준 높이(viewport vs window)·비-16:9 정책에 영향. standalone은 1:1이라 무관, 에디터-인-패널 시 차이.
- [ ] **남은 의문 3 — 라벨 렌더**: 별도 경량 렌더러(→`AddScreenText` 확장) vs RmlUi 문서 마운트(→TTF 공짜). 병존 결정상 후자도 옵션이나, 결합도 최소화엔 전자.

### F3. 구현 사이클 제안 (컴포넌트/이슈 1개 단위, 순서)
| # | 사이클 | 산출물 | 의존 |
|---|---|---|---|
| 1 | **수학 토대** | `FVector2` 성분곱 헬퍼 + Engine측 `FUIRect{pos,size}` (또는 2×FVector2 컨벤션) | — |
| 2 | **노드 트리 + RectTransform** | `UUICanvas/UUIGroup/UUIElement : USceneComponent`(서브클래스), `FUIRectTransform{pivot,anchor,position,size}` 멤버(3D RelativeTransform 무시), 소유 Actor/매니저로 루팅 | 1 |
| 3 | **레이아웃 패스** | `TickFrameBody`(WorldTick↔Render 사이) LayoutTick, top-down 전체 재계산, FinalPos 식 | 2 |
| 4 | **RefRes/GlobalScale** | `FProjectSettings.FUIOption` + `OnWindowResized`에서 `GlobalScale=H/1080` 1회 산출, 레이아웃 픽셀에 곱 | 3 |
| 5 | **드로우 패스** | 신규 `ERenderPass::SimpleUI` 슬롯, `FRmlRenderInterfaceD3D11` 패턴 모방한 쿼드 배처(draw-only) | 3,4 |
| 6 | **텍스트 라벨** | `AddScreenText` 확장(색/크기 파라미터화 + 에디터게이트 제거) 또는 RmlUi 라벨 마운트 | 5 |
| 7 | **히트테스트 + 드래그 에디터** | 트리 point-in-rect 히트테스트, `position`만 갱신(델타/GlobalScale), 에디터모드 토글을 `FUIInputCaptureState` 계약에 합류 | 4,5 |
| 8 | **저장/로드** | `SerializeVector2` 추가 + Element `FUIRectTransform`을 `PF_Save` 프로퍼티로 → `.Scene` 임베드 영속 + 로드 후 토폴로지 명시 재구축 | 2,7 |

> **오버엔지니어링 경계 재확인**: 더티플래그(C4)·Slot 분리(C5)·영역앵커(Min≠Max)·레터박스(D4)·RmlUi 레이어링(F1)은 **MVP 범위 밖**. 위 8 사이클이 프롬프트 목표 모델의 최소 충분 집합.
