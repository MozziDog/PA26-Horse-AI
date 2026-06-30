# 텍스트 기능을 베이스 `UUIElement` 로 승격 — 진단

> 작성일: 2026-06-07 · 대상: KraftonEngine (자체 D3D11 엔진) · 사이클: **진단 단계 (구현 없음)**
> 방법: UI 런타임 트리(UIElement/Label/Button/Image/Canvas/Group) · 렌더 2경로(SimpleUIPass·RmlUi) · 직렬화(SceneSaveManager+생성 리플렉션) · 에디터(UIEditorWidget) 직접 정독. 근거는 모두 `파일:라인`.
> 표기: **[사실]** = 코드로 확인된 현황 · **[설계]** = 신규 권고(코드 근거 아님) · **[정정]** = 트리거 프롬프트 전제 오류 정정 · **[전제]** = 선행 의존성/리스크.
> 진행 순서 맥락: **Text(본 문서) → Image → 드래그 배치**의 1단계.

---

## 0. 결론 요약 (먼저 읽을 것)

### 핵심질문 1 — 기존 `UUILabel` 의 처지 → **(c) 유지하되 "Text" 팔레트 프리셋으로 재배치 + 메커니즘은 베이스로 흡수**
- **데이터(Text)와 RmlUi 마운트 메커니즘**(`OnLayoutUpdated`·`BeginDestroy`·`Widget`·`bMountAttempted`)을 **베이스 `UUIElement` 로 올린다**. 그 결과 `UUILabel` 에 남는 차별점은 생성자 `SetVisibleRect(false)`(+권고: 기본 Text) **하나뿐** → 이는 `UUIButton`/`UUIImage` 가 "생성자 기본값만 가진 trivial 프리셋"인 것과 **정확히 동형**이다.
- 즉 `UUILabel` = **"배경 없는 텍스트" 팔레트 프리셋**으로 살아남는다. 텍스트 렌더 동작은 더 이상 이 클래스의 특수기능이 아니라 *베이스 동작 + bVisibleRect=false* 일 뿐.
- **순수 폐지(a) 기각 근거**: 팔레트/팩토리가 **클래스 이름 문자열로 생성**([UIEditorWidget.cpp:279](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp), `FObjectFactory::Create`)하고, 런타임 [`CreateDebugTestCanvas` 가 `CreateObject<UUILabel>()`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp) (UICanvasManager.cpp:72) 로 타입을 직접 참조한다 → 전용 클래스가 있어야 Button/Image 와 대칭되는 깔끔한 "Text" 프리셋이 된다. *(단, 커밋된 .uasset/.Scene 중 `UUILabel` 을 직렬화한 것은 0건 — grep 무결과 — 이므로 에셋 하위호환은 약한 요인이고, 결정의 실제 동인은 코드 대칭성이다.)*
- **(b) "유지하되 자체 텍스트 동작 재정의" 기각 근거**: 본 작업의 목적 자체가 *타입별 분산 없이 텍스트를 공유*하는 것 → 텍스트 동작을 UUILabel 에 남기면 승격 취지에 반함.

### 핵심질문 2 — `UUIButton`/`UUIImage` 전제 변경 → **무회귀·순수 가산. 데이터 마이그레이션 없음. 주석만 갱신.**
- 둘은 베이스에서 텍스트 멤버+RmlUi 메커니즘을 **자동 상속**한다. 기본 Text 가 빈 문자열이고 베이스 마운트가 *비어있지 않을 때만* 동작하도록 가드하면(§B-R2) → 마운트 안 함 → **런타임 동작 동일(회귀 0)**.
- 오히려 **가산**: 이제 Button 이 텍스트를 가질 수 있다 → 단색 쿼드(SimpleUIPass) **+** 텍스트 오버레이(RmlUi) = 제대로 된 *라벨 달린 버튼*. 이것이 §B 의 "단색쿼드/RmlUi 2중 렌더 공존"이 **유리하게** 작동하는 사례.
- 데이터 마이그레이션 불필요(이전엔 텍스트가 없었음 = 베이스 기본 빈 값과 동일; 구 에셋은 키 부재 → 기본값).
- **갱신 필요한 것은 주석뿐**: [UIButton.h:7-8](../KraftonEngine/Source/Engine/UI/Canvas/UIButton.h)·[UIImage.h:7-8](../KraftonEngine/Source/Engine/UI/Canvas/UIImage.h) 의 "텍스트/타입별 특수속성 없음" 문구는 **stale** 가 된다(텍스트는 이제 상속받는 공유 속성). "trivial 서브클래스" 전제 자체는 **그대로 유효** — 둘은 여전히 생성자뿐, 베이스만 풍부해진다.

### 최대 리스크 한 줄 (자세히는 §F)
- **[전제] 에디터가 RmlUi 마운트를 절대 돌리면 안 된다.** 에디터 라이브 트리는 `FUICanvasManager` 엔 일부러 미등록([UIEditorWidget.h:25-26](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.h))이지만, `OnLayoutUpdated` 의 마운트 대상은 **다른 싱글톤** `UUIManager`(게임 RmlUi viewport) 라 그 격리에 안 잡힌다. 에디터는 매 프레임 `LayoutCanvas`([UIEditorWidget.cpp:352](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp))→`OnLayoutUpdated`([UICanvasManager.cpp:139](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)) 를 호출하므로, 베이스가 여기서 마운트하면 **에셋 텍스트가 전체 화면 게임 viewport 에 잘못된 좌표로 새어나간다**. → 외부(RmlUi) 동기화는 런타임 `LayoutAll` 에서만 돌게 게이트하고, 에디터 뷰포트 텍스트는 **ImGui `AddText` 미러**로 그린다(기존 "에디터=ImGui 미러" 방침과 일치).

---

## A. 기존 텍스트 렌더 경로 (UUILabel)

- [x] **RmlUi 로 텍스트를 그리는 경로** — [UILabel.cpp `OnLayoutUpdated`](../KraftonEngine/Source/Engine/UI/Canvas/UILabel.cpp):
  - 최초 1회 위젯 생성: `UUIManager::Get().CreateWidget(nullptr, "Content/UI/SimpleUILabel.rml")` → `AddToViewport(1000)` (UILabel.cpp:22-30, 경로 상수 :11).
  - 매 프레임 동기화: `W->SetText("label", Text)` (:43), 위치 `W->SetProperty("label", "left"/"top", ToDp(R.Pos/Scale))` (:44-45).
  - `SetText` 는 [`Element->SetInnerRML`](../KraftonEngine/Source/Engine/UI/UserWidget.cpp) (UserWidget.cpp:116), `SetProperty` 는 [`Element->SetProperty(name, value)`](../KraftonEngine/Source/Engine/UI/UserWidget.cpp) (UserWidget.cpp:133) — **임의 RmlUi 속성을 받는 범용 통로**(= font-size/color/font-weight/text-align 도 같은 통로로 가능).
  - **폰트/정렬/색은 현재 C++ 에서 안 보냄** — 전부 `.rml` CSS 하드코딩: [SimpleUILabel.rml](../KraftonEngine/Content/UI/SimpleUILabel.rml) `div#label{font-size:20dp; color:#ffffff; white-space:nowrap}` (:11-19), `body{font-weight:bold}` (:6-8). `text-align` 미설정(기본 left).
- [x] **UUILabel 보유 텍스트 관련 멤버 전체** — [UILabel.h](../KraftonEngine/Source/Engine/UI/Canvas/UILabel.h): `FString Text`(UPROPERTY Save, :30-31), `TWeakObjectPtr<UUserWidget> Widget`(:32), `bool bMountAttempted`(:33). API: `SetText`/`GetText`(:21-22). 오버라이드: `OnLayoutUpdated`(:25), `BeginDestroy`(:27, viewport 제거 — UILabel.cpp:48-55).
- [x] **bVisibleRect/단색 rect 처리** — `UUILabel()` 생성자가 `SetVisibleRect(false)` (UILabel.h:19) → **배경 쿼드 안 그림, 텍스트만**. (Button/Image 는 베이스 기본 `bVisibleRect=true` 유지 → 쿼드 그림.)

## B. 베이스 승격 설계

- [x] **텍스트 멤버를 베이스로 올릴 때 영향 범위** — `UUIElement` 의 모든 파생(Canvas/Group/Button/Image/Label)이 5개 멤버를 상속.
  - **[설계] 추가 멤버**(UPROPERTY Save): `FString Text=""` · `float FontSize=20` · `FString FontWeight="bold"` · `FString TextAlign="left"` · `FVector4 TextColor={1,1,1,1}`. 기본값은 **반드시 `.rml` 하드코딩과 일치**(20dp/white/bold/left)시켜 기존 라벨 외형 회귀 0(§C, §F-R4).
  - **비가시 컨테이너(Canvas/Group) 무해성**: 기본 Text 빈 문자열 + 마운트 가드(아래) → RmlUi 위젯 생성 안 됨. 멤버는 약간의 메모리(weak ptr+bool+문자열)만 점유. **빈 문자열이면 무해** ✓. (직렬화 비용은 §C-R6.)
- [x] **RmlUi 텍스트 렌더를 베이스가 호출하게 옮기는 지점 + 2중 렌더 공존** —
  - **[설계] `UIElement.cpp` 신설**(현재 [UIElement.h](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h) 는 **헤더 온리**, .cpp 없음). `OnLayoutUpdated`(마운트+동기화)·`BeginDestroy` 를 UILabel.cpp 에서 베이스 .cpp 로 이전 → **RmlUi include 를 널리 포함되는 헤더 밖에 격리**(기존 "RmlUi 의존은 .cpp 에만" 방침 계승, UILabel.h:13 주석).
  - **요소당 2경로 공존 방식**: ① 단색 쿼드 = [`SimpleUIPass::CollectVisible`](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)(:47-85) 가 `bVisibleRect` 요소의 `ScreenRect+BackgroundColor` 를 쿼드로 → 화면 RTV(:179). ② 텍스트 = 베이스 `OnLayoutUpdated` 가 `UUIManager` 위젯(ZOrder 1000) 마운트/동기화 → [`UIPass` 가 `UUIManager::Render`](../KraftonEngine/Source/Engine/Render/RenderPass/UIPass.cpp)(:27). **두 경로는 독립 패스이며 공유 입력은 `ScreenRect`(위치)뿐** → 한 요소가 *쿼드만 / 텍스트만 / 둘 다* 어느 조합도 가능. 충돌 지점 없음.
  - **[전제] 마운트 가드(R2)**: 현재 UILabel.cpp:22-30 은 **무조건** 마운트하고 `bMountAttempted` 를 latch. 그대로 베이스로 옮기면 모든 Canvas/Group/Button/Image(빈 텍스트)가 빈 RmlUi 문서를 마운트 → 다수 유령 위젯. **반드시 `Text` 비어있지 않을 때만 마운트 + 빈 동안엔 latch 안 함**(에디터에서 나중에 텍스트 입력 시 마운트되도록). 텍스트 삭제 시 위젯 숨김/제거.
  - **[전제] 에디터 격리(R1, 최대 리스크)**: §0 참고 — 외부 동기화를 런타임 `LayoutAll` 한정으로 게이트. **[설계]** `LayoutElement`/`LayoutCanvas` 에 `bool bSyncExternal` 인자 추가(LayoutAll=true, 에디터 LayoutCanvas=false)해 `OnLayoutUpdated` 호출(또는 마운트)만 분기.
- [x] **UUILabel 처리 결론** — §0 핵심질문 1 = **(c) 흡수+재배치**. 추가 구현 메모: 승격 시 **UUILabel 의 자체 `Text` 멤버는 제거**해야 함(베이스가 동일 `"Text"` 키/타입 FStringProperty 를 가지므로 둘 다 두면 **중복 등록**, R3).
- [x] **UUIButton/UUIImage 영향 결론** — §0 핵심질문 2 = 무회귀·순수 가산. 주석만 갱신.

## C. 직렬화 영향

- [x] **베이스 추가 속성의 자동 반영 여부** — **자동. 직렬화 코드 변경 0.**
  - 저장: [`SerializeProperties` → `Obj->SerializeProperties(Ar, PF_Save)`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp)(:500) 가 **모든 PF_Save 프로퍼티를 일괄** 기록.
  - 등록: UPROPERTY 매크로 → `Tools/GenerateHeaders.py` 가 [`RegisterProperties` 에 `AddProperty(...PF_Save...)`](../KraftonEngine/Intermediate/Generated/Source/Engine/UI/Canvas/UUIElement.generated.cpp)(:39-121) 자동 생성. 문자열은 [`FStringProperty`](../KraftonEngine/Intermediate/Generated/Source/Engine/UI/Canvas/UUILabel.generated.cpp)(UUILabel.generated.cpp:40-49) 선례, float 은 `EPropertyType::Float`([PropertyTypes.h:56](../KraftonEngine/Source/Engine/Core/Types/PropertyTypes.h)). → **헤더 재생성만 하면 끝**.
- [x] **구 에셋(텍스트 속성 없던 것) 로드 시 기본값** — [`DeserializeProperties`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) 가 JSON 에 **키가 없으면 skip**(:791-794, :810-813) → C++ 생성자 기본값 유지. **마이그레이션 불필요.** (단 기본값이 `.rml` 과 일치해야 외형 회귀 0 — §B, R4.)
- [x] **기존 UUILabel 자산 로드** — 구 라벨은 `Text` 를 UUILabel 의 FStringProperty 로 저장. 승격 후 동일 `"Text"` 키/displayname 이 베이스에 있으므로 **같은 키→같은 타입으로 그대로 로드**. 마이그레이션 없음. (선결: R3 — UUILabel 의 중복 Text 멤버 제거.)
- [전제] **R6(경미)**: 승격 후 모든 노드(컨테이너 포함)가 `Text:""`/`FontSize:20`/… 를 직렬화 → JSON 약간 비대. 무해. `SerializeProperties` 는 기본값 skip 을 지원하지 않음(전 PF_Save 기록).

## D. 디테일 패널 텍스트 필드 추가

- [x] **추가 지점** — [`RenderDetailsPanel` 의 Color 필드 직후](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp)(UIEditorWidget.cpp:436-440) 에 5필드 추가. 기존 바인딩 패턴(스택 배열↔멤버, 변경 시 `MarkDirty()`) 그대로 차용.
- [x] **위젯 매핑** — Text 내용 = `ImGui::InputText` · Font Size = `ImGui::DragFloat`(또는 InputFloat) · Font Weight = `ImGui::InputText` · Text Align = `ImGui::InputText`(드롭다운 아님, 결정대로) · Color = `ImGui::ColorEdit4`(**기존 BackgroundColor `ColorEdit4`(:434-440) 와 별개의 `TextColor`**).
  - **[설계] 유일한 신규 메커니즘 = `InputText`↔`FString` 마샬링**: 기존 4필드는 float/Vec4 스택배열이라 단순했음. 텍스트는 임시 char 버퍼 필요 → 편집 시 `FString` 으로 커밋. (Weight/Align/Text 3개가 이 패턴.)
- [x] **선택 요소 직접 read/write** — `Selected`(베이스 포인터)의 새 텍스트 멤버에 직접 접근(기존 `Selected->GetColor()/SetColor`·`GetRectTransform()` 와 동형, :411/:434).
- [x] **편집 → 즉시 갱신 경로(Refresh 트리거)** — **별도 트리거 불필요(pull 기반).**
  - 에디터: 뷰포트가 매 프레임 `LayoutCanvas`(:352) + (신설) ImGui 텍스트 미러로 **라이브 멤버를 매 프레임 재독** → 편집 즉시 다음 프레임 반영.
  - 런타임: 베이스 `OnLayoutUpdated` 가 매 프레임 `SetText`/`SetProperty` 재푸시(기존 UILabel.cpp:43 와 동일한 매프레임 푸시 방식) → 트리거 배선 없음.
- [정정] **"5필드" 명칭 불일치(무해)**: 현재 디테일 패널 실제 위젯은 **4개**(Size/Offset/Pivot/Color). 코드 주석(:409)·트리거 프롬프트는 "5필드"로 칭하나 `Anchor` 는 RectTransform 멤버이면서 **미노출** — 이 누락이 "5" 호칭의 원인으로 보임. 설계엔 영향 없음.

## E. 팔레트 "Text" 요소

- [x] **팔레트 항목 추가** — [`RenderPalettePanel`](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp) 의 Canvas/Button/Image 버튼 블록(:263-265) 에 `if (ImGui::Button("Text")) SpawnElement(UUILabel::StaticClass());` 추가. (핵심질문 1 결론대로 **UUILabel** 사용.)
  - **[설계] include 누락**: UIEditorWidget.cpp 는 UIButton.h/UIImage.h 만 포함(:13-14), **UILabel.h 미포함** → 추가 필요.
  - **[설계] 기본 Text**: 스폰 직후 보이도록 `UUILabel` 생성자에 기본 `Text="Text"` 권고(현재 생성자는 `SetVisibleRect(false)` 만 — UILabel.h:19).
- [x] **생성 흐름 동일성** — [`SpawnElement`](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp)(:271-300) 은 클래스 이름 범용(`FObjectFactory::Create`→`RegisterComponent`→`AttachToComponent`). **Canvas/Button/Image 와 완전 동일 경로** ✓.
- [전제] **에디터 미러가 비가시 텍스트를 그려야 함**: [`DrawUIElementRect`](../KraftonEngine/Source/Editor/UI/Asset/UI/UIEditorWidget.cpp)(:23-45) 는 `IsVisibleRect()` 일 때만 그림 → bVisibleRect=false 인 Text 프리셋은 에디터에서 **안 보임**. `Text` 비어있지 않으면 `bVisibleRect` 무관하게 `AddText` 하도록 확장 필요(§B 의 ImGui 텍스트 미러와 동일 작업).
- [전제→드래그 차례] **선택성**: [`HitTestRecursive`](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)(:158) 도 `IsVisibleRect` 만 히트 → Text 프리셋은 **뷰포트 클릭 선택 불가**, 계층트리 클릭으로만 선택(DrawHierarchyNode 는 가시성 무관, :73-76). 드래그 사이클에서 텍스트 바운드 히트테스트 확장 필요(아래 의존성 메모).

## F. 구현 사이클 분할 + 리스크

### [설계] 사이클 분할 (각 사이클: msbuild 0-error → 1커밋 → 런타임 점검)
- **① 텍스트 멤버 베이스 승격 + UUILabel 정리(데이터만)** — UIElement.h 에 5멤버 UPROPERTY(Save) 추가 · UUILabel 자체 `Text` 멤버 제거(R3) · UUILabel 을 생성자 전용 프리셋(bVisibleRect=false + 기본 Text)으로 축소 · 헤더 재생성. *렌더 변화 없음(데이터 골격).*
- **② 베이스 텍스트 RmlUi 렌더** — `UIElement.cpp` 신설, `OnLayoutUpdated`(마운트+동기화)·`BeginDestroy` 를 UILabel.cpp→베이스로 이전 · **비빈-텍스트 가드(R2)** · font-size/weight/align/color 도 SetProperty 푸시 · **외부 동기화 런타임 한정 게이트(R1)** · UILabel.cpp 본문 비움. 런타임 점검: CreateDebugTestCanvas 라벨 정상 + Button+텍스트 오버레이 확인.
- **③ 디테일 5필드 + 에디터 텍스트 미러** — InputText/DragFloat/InputText/InputText/ColorEdit4 추가(§D) · `DrawUIElementRect` 에 텍스트 `AddText` 미러(§E). 에디터 점검.
- **④ 팔레트 "Text" 항목** — 버튼 + UILabel.h include + 기본 Text(§E). 점검: Text 스폰→편집→저장/재로드.

### [전제] 리스크 (우선순위順)
- **R1 (최고) 에디터 RmlUi 오염** — 게이트 없으면 에셋 텍스트가 게임 viewport 에 잘못된 좌표로 누출 + 런타임 viewport 오염 + 위젯 누수. 완화: `bSyncExternal` 분기 + 에디터는 ImGui `AddText` 미러. *(현재는 잠복 — 팔레트에 Text 없고 커밋된 라벨 에셋 0건. 텍스트가 베이스에 오르고 팔레트에 Text 가 생기는 순간 발현.)*
- **R2 무조건 마운트 회귀** — 빈 텍스트도 마운트하면 컨테이너마다 유령 위젯. 가드: 비빈 텍스트만 마운트 + 빈 동안 latch 금지.
- **R3 UUILabel Text 이중 선언** — 베이스 Text 추가 시 UUILabel 의 Text 멤버 미제거하면 프로퍼티 중복 등록/`"Text"` 키 충돌.
- **R4 기존 라벨 외형 회귀** — 베이스 텍스트 기본값을 `.rml`(size20/white/bold/left)과 불일치시키면 매프레임 SetProperty 푸시가 외형을 바꿈. 기본값 일치로 차단.
- **R5 Text 프리셋 선택 불가** — bVisibleRect=false + 히트테스트가 가시 한정. 당장은 계층트리 선택으로 충분, 뷰포트 클릭/드래그는 드래그 사이클에서 텍스트 바운드 히트 확장.
- **R6 (경미) JSON 비대** — 전 노드가 빈 텍스트 속성 직렬화. 무해.
- **[caveat] RmlUi 폰트/정렬 충실도** — `font-weight` 는 해당 weight 폰트 페이스가 로드돼 있어야 적용(현재 body 에 bold 만). Align/Weight 가 자유 입력(결정대로)이라 미등록 값은 무시될 수 있음 — 기능 결함 아닌 폰트 자원 한계.

---

## 다음 단계(Image / 드래그) 의존성 메모
- **Image UI**: 향후 UIImage 텍스처 참조는 *새 멤버*(베이스 또는 Image)로 추가되며, 본 텍스트 작업과 충돌 없음. 단 **같은 디테일 패널 + 같은 직렬화 경로**를 건드리므로 §C/§D 패턴(UPROPERTY Save 자동 직렬화 · Selected 직접 바인딩) 재사용.
- **드래그 배치**: **R5 가 드래그 사이클의 직접 선결**이다 — bVisibleRect=false 인 Text 요소를 뷰포트에서 드래그하려면 `HitTestRecursive`(UICanvasManager.cpp:158)/`DrawUIElementRect`(UIEditorWidget.cpp:29) 의 "가시 한정"을 텍스트 바운드까지 확장해야 함. 본 사이클에선 계층트리 선택으로 우회하고, 확장은 드래그 차례로 이관.
- **2중 렌더 정합**: 텍스트가 베이스에 오르면 "쿼드+텍스트" 요소의 *위치 동기화*는 단일 `ScreenRect` 로 통일돼 있어(SimpleUIPass·RmlUi·에디터 미러 모두 `GetScreenRect()` 사용) 드래그가 셋을 동시에 끌고 간다 — 추가 배선 불필요.
