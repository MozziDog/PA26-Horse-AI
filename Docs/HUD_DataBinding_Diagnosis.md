# 기존 UI ↔ 월드/액터 실시간 연동 (체력바·잔탄·HUD) — 진단

> 작성일: 2026-06-08 · 대상: KraftonEngine (자체 D3D11 엔진) · 사이클: **진단 단계 (구현 없음)**
> 방법: 게임플레이 데이터 소스(Pawn/PlayerController/Boss/ProjectilePool) · UI 런타임 트리(UIElement/UITextElement/UICanvasActor/UICanvasManager) · 렌더(SimpleUIPass·SimpleUI.hlsl) · 카메라/투영(MinimalViewInfo·FrameContext) 직접 정독 + 4 병렬 스캔. 근거는 모두 `파일:라인`(하중 주장은 직접 재검증).
> 표기: **[사실]** = 코드로 확인된 현황 · **[설계]** = 신규 권고(코드 근거 아님) · **[갭]** = 백킹 데이터/기능 부재 · **[정정]** = 전제 보정 · **[전제]** = 선행 의존성/리스크 · **[결정]** = 사용자 확정 사항.

> ### 확정된 설계 결정 (2026-06-08)
> - **[결정] 텍스처 아이콘/스프라이트 바 = Option A(요소 트리 in-tree 확장).** `UUIImage` 에 텍스처 SRV(`FSoftObjectPtr`) + 텍스처 샘플 셰이더 변형 + `SimpleUIPass` SRV 바인드. B(RmlUi)·C(단색 대체) 기각 — **통일 모델/에디터/바인딩** 우선. ⚠️ 귀결: 9-slice 프레임은 공짜가 아님(B 포기) → 단순 텍스처 fill 로 시작, 9-slice 는 필요 시 SimpleUI 지오메트리 후행. 배칭 하위선택(A-1 요소별 / A-2 아틀라스)은 §D4. 잔탄·점수/웨이브/타이머는 **보류**.

---

## 0. 결론 요약 (먼저 읽을 것)

### ★ 핵심: 요청한 기능의 "씨앗"이 이미 동작 중이다 — 신규가 아니라 **일반화**다
- **[사실] 체력바 데이터 바인딩이 이미 구현·동작 중이다.** `AUICanvasActor` 가 매 프레임 틱하며 `UpdateHealthBarBinding()` 에서 소스 `APawn` 의 `GetHealthRatio()` 를 읽어 대상 UI 요소의 가로 폭을 스케일한다([UICanvasActor.cpp:113-168](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)). **이것이 현재 working tree 의 미커밋 변경**(git status: `UICanvasActor.cpp/.h`, `UIElement.h`)이다. 즉 본 요청은 **이 일회성 체력바 바인딩을 다중 채널 HUD 바인딩 체계로 확장**하는 작업이다.
- **[사실] 바인딩 패턴은 POLL(매 프레임 폴) 로 이미 확정**되어 있고, 그것이 옳다. 게임플레이 "값 변경" 이벤트(델리게이트)가 **하나도 없으므로** push 는 소스마다 델리게이트를 신설해야 해서 부적합. seam 은 `AUICanvasActor::Tick`(WorldTick 안 → LayoutAll 직전, 순서 정합).

### ⚠️ 가장 중요한 한 줄 — 무엇이 되고 무엇이 안 되는가
| HUD 타깃 | 백킹 데이터 | 판정 |
|---|---|---|
| **플레이어 체력바** (width/색) | `APawn::GetHealthRatio()` 완비 | ✅ **즉시 가능** (씨앗 확장) |
| **보스 체력바 + 페이즈** | `UBossPatternSelectorComponent` 완비 | ✅ **즉시 가능** |
| **체력/속도/시간 숫자 텍스트** | Pawn/Movement/World 완비 | ✅ **즉시 가능** |
| **월드앵커 바**(보스/적 머리 위 HP) | 카메라 VP 행렬·투영 수학 완비 | ✅ **가능**(런타임 투영 헬퍼 1개만 노출) |
| **잔탄수(Ammo)** | **무기/탄창/탄약 개념 전무, 풀 count getter 없음** | ❌ **[갭] 게임플레이 상태부터 신설** |
| **점수/웨이브/타이머** | `AGameStateBase` 비어 있음 | ❌ **[갭] GameState 필드 신설** |
| **텍스처 아이콘/스프라이트 바** | SimpleUI 는 단색 쿼드 전용 | ❌ 현재 갭 → **[결정] Option A 로 in-tree 확장**(§D4·사이클 8) |

- **[정정] "기존 UI 를 그대로 연동" = 부분만 사실.** 체력/보스/숫자는 기존 인프라로 즉시 연동되지만, **잔탄은 표시할 데이터 자체가 없다** — UI 문제가 아니라 게임플레이 상태 부재다. 잔탄 HUD 는 "UI 바인딩"이 아니라 "탄약 시스템 신설 → 그 다음 바인딩" 2단계다.

### 한 줄 판정
**[설계] 씨앗(`UpdateHealthBarBinding`)을 "바인딩 디스크립터 리스트"로 일반화**하라: 각 항목 = {대상 요소명, 소스(플레이어/이름/컴포넌트), 읽을 값, 적용 채널(width/color/text/position)}. 체력·보스·숫자·월드앵커는 이 한 구조로 전부 커버되고, 잔탄/점수/텍스처는 **선행 데이터/기능 신설이 차단 요인**임을 명시한다.

---

## A. 현재 상태 — 이미 있는 것 (씨앗)

### A1. 기존 체력바 바인딩 (`UpdateHealthBarBinding`)
- [x] **틱 구동**: `AUICanvasActor()` 생성자가 `bNeedsTick = true`([UICanvasActor.h:25](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h)), `Tick()` 이 매 프레임 `UpdateHealthBarBinding()` 호출([UICanvasActor.cpp:113-117](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)). *(주: 구 진단 `UIAsset_ViewportDrop` 의 "bNeedsTick=false" 기술은 이 사이클로 갱신됨 — 현재는 true.)*
- [x] **바인딩 필드(에디터 노출·직렬화)**: `HealthBarElementName`(대상 요소) + `HealthSourceActorName`(소스 액터 Outliner 명), 둘 다 `UPROPERTY(Edit, Save)`([UICanvasActor.h:62-67](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h)).
- [x] **소스 해석**: weak 캐시 우선, 무효 시 `World->GetActors()` 선형 스캔으로 `FName` 매칭([UICanvasActor.cpp:129-149](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)).
- [x] **적용**: 최초 1회 저작 폭을 100% 기준으로 캡처(음수 sentinel), 이후 `Bar->SetSize({FullWidth * Ratio, Y})`([UICanvasActor.cpp:160-167](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)). `Ratio = Source->GetHealthRatio()`.
- [x] **PIE/런타임 한정**: 등록(`RegisterCanvas`)·틱 모두 `BeginPlay`/`HasActorBegunPlay()` 게이트라 편집 월드엔 영향 없음([UICanvasActor.cpp:88-94](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)).

### A2. 틱 seam + 프레임 순서 — **순서가 이미 정합**
- [x] **`WorldTick(액터 틱 → 바인딩 → SetSize/SetColor/SetText)` → `LayoutAll` → `Render(SimpleUIPass)`** 순서가 고정([Engine.cpp:180→188→190](../KraftonEngine/Source/Engine/Runtime/Engine.cpp)). 따라서 틱에서 바꾼 size 는 **같은 프레임** `LayoutAll` 이 `ScreenRect` 로 재계산, color/text 는 relayout 없이 draw 에서 소비. → **씨앗과 같은 자리에 채널만 추가하면 됨.**

### A3. UI 요소 가변 채널 — 런타임에 바꿀 수 있는 것
- [x] **width(바)**: `SetSize(FVector2)` → `RectTransform.Size`([UIElement.h:24](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)). `LayoutElement` 가 `Screen.Size = Size * Scale` 재계산([UICanvasManager.cpp:131-138](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)).
- [x] **color(쿼드)**: `SetColor(FVector4)` → `BackgroundColor`([UIElement.h:73](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)). draw 가 매 프레임 `GetColor()` 읽음 → **relayout 불필요**.
- [x] **text(라벨)**: `UUITextElement::SetText(FString)`([UITextElement.h:22](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.h)), 매 프레임 `OnLayoutUpdated` 가 RmlUi 위젯에 `SetText("label", Text)` 재푸시([UITextElement.cpp:77](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.cpp)). 위젯은 **1회만 마운트**([UITextElement.cpp:57-65](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.cpp)) → 매 프레임 비용은 재푸시(재마운트 아님).
- [x] **position(월드앵커)**: `SetPosition(FVector2)`([UIElement.h:23](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)) → E 장 참조.
- [x] **대상 조회**: `UICanvas::FindByName`/`UUIElement::FindByName`([UIElement.h:39](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)), 키 = `ElementName`(Save 필드, [UIElement.h:34](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h)).

### A4. 렌더 한계 — **단색 쿼드 전용 (텍스처/클립 없음)**
- [x] **SimpleUIPass 는 per-element 단색 쿼드만**: 정점에 `GetColor()` RGBA 복사([SimpleUIPass.cpp:65-68](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)), `AlphaBlend`+`NoDepth`([SimpleUIPass.cpp:167](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)). **텍스처/샘플러 바인딩 0**(grep 무결과).
- [x] **셰이더 PS = `return Input.Color`**, Texture2D/Sampler 선언 없음([SimpleUI.hlsl:43-46](../KraftonEngine/Shaders/UI/SimpleUI.hlsl)).
- [x] **클리핑 없음**: child 를 parent rect 로 자르는 scissor/intersect 없음(scissor 는 RmlUi 경로에만, 요소엔 미사용). → **바 fill 은 width 로 줄여야지, 크게 그려 잘라내는 방식 불가.**

---

## B. 바인딩 대상 — 게임플레이 데이터 인벤토리

### B1. 플레이어 도달 — **짧고 null-safe** (이름 스캔 대체)
- [x] **`World->GetFirstPlayerController()`** = `GetGameMode() ? GameMode->GetPlayerController() : nullptr`([World.cpp:186-189](../KraftonEngine/Source/Engine/GameFramework/World.cpp)) → **`APlayerController::GetPossessedPawn()`** = `PossessedPawn.Get()`(weak)([PlayerController.h:36](../KraftonEngine/Source/Engine/GameFramework/GameMode/PlayerController.h)). 둘 다 null-safe. **싱글플레이어**(PC 1개).
- [x] **선례 동일 패턴**: `UBossPatternSelectorComponent::ResolveTargetActor` 가 `bAutoResolvePlayerTarget` 시 정확히 이 체인 사용([BossPatternSelectorComponent.cpp:189-195](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.cpp)).
- [x] **카메라까지**: 같은 PC 에서 `GetPlayerCameraManager()`([PlayerController.h:44](../KraftonEngine/Source/Engine/GameFramework/GameMode/PlayerController.h)) → 월드앵커(E)용 POV.

### B2. 체력 — **모든 Pawn 에 완비**
- [x] **`APawn` 기반 체력 블록**: `MaxHealth`/`CurrentHealth`(`UPROPERTY(Edit,Save)`, [Pawn.h:110-114](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.h)), getter `GetCurrentHealth/GetMaxHealth/GetHealthRatio/GetHealthHitCount`(`UFUNCTION(Pure)`, [Pawn.h:64-71](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.h)). `GetHealthRatio()` 는 [0,1] 클램프(MaxHealth<=0 이면 1)([Pawn.cpp:63-70](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.cpp)). 베이스 `APawn` 이라 Character/Boss 전부 상속.
- [x] **데미지 경로**: `UBulletHellDamageReceiverComponent::ApplyDamage` 가 소유 Pawn 으로 전달(컴포넌트 자체는 HP 미보유) → **HP 는 Pawn 에서 읽을 것**([BulletHellDamageReceiverComponent.h:18-33](../KraftonEngine/Source/Engine/Component/Gameplay/BulletHellDamageReceiverComponent.h)).

### B3. 보스 HP / 페이즈 — **완비 (보스바)**
- [x] **읽기 핸들**: `UBossPatternSelectorComponent::GetBossPatternDebugState()`([BossPatternSelectorComponent.h:22](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.h)) → `FBossPatternDebugState{ float BossHealthRatio, int32 BossPhase, FString ActivePatternName, ... }`([BossPatternComponentBase.h:40-56](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternComponentBase.h)).
- [x] **산출**: `ResolveBossHealthRatio` = 소유 Pawn `GetHealthRatio()`([BossPatternSelectorComponent.cpp:206-226](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.cpp)), `ComputeBossPhase` 임계 0.66/0.33 → 0/1/2([:228-244](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.cpp)). 보스 액터에서 `GetComponentByClass<UBossPatternSelectorComponent>()` 로 해석.

### B4. 기타 즉시 바인딩 가능한 값
- [x] **이동 속도**: `UCharacterMovementComponent::GetSpeed()`(=Velocity.Length, [CharacterMovementComponent.h:59](../KraftonEngine/Source/Engine/Component/Movement/CharacterMovementComponent.h)), `ACharacter::GetCharacterMovement()`([Character.h:119](../KraftonEngine/Source/Engine/GameFramework/Pawn/Character.h)).
- [x] **월드 시간(매치 타이머 대용)**: `UWorld::GetGameTimeSeconds()`([World.h:82](../KraftonEngine/Source/Engine/GameFramework/World.h)).
- [x] **피격 횟수/누적 데미지**: `APawn::GetHealthHitCount()`/`TotalDamageTaken`([Pawn.h:71,119-123](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.h)).
- [x] **활성 적탄 수**(플레이어 잔탄 아님): `UBulletHellComponent::GetBulletCount()`.

### B5. [갭] 잔탄(Ammo) — **백킹 데이터 전무**
- [x] **무기/탄창/탄약 타입이 코드 전체에 없음**(grep `Ammo/Magazine/Weapon/Shoot/Clip/RemainingShots` = 게임플레이 상태 0건).
- [x] **발사체 풀도 count getter 없음**: `FProjectilePoolSubsystem` API = `Prewarm/Acquire/Release` 뿐, `InactivePool/ActiveLookup` 는 private, 개수 접근자 부재([ProjectilePoolSubSystem.h:28-48](../KraftonEngine/Source/Engine/GameFramework/ProjectilePoolSubSystem.h)). → **"남은 탄 = 풀 여유분"조차 공개 API 없음.**
- ⇒ **[설계] 잔탄 HUD 는 2단계**: ① 게임플레이에 탄약 상태 신설(Pawn/무기에 `Ammo` 필드 + `UFUNCTION(Pure)` getter, 체력 블록 패턴 모방) 또는 풀에 `GetFreeCount()` 추가 → ② 그 값을 텍스트 채널에 바인딩. **UI 작업 이전에 데이터부터.**

### B6. [갭] 점수/웨이브/카운트다운 — **GameState 비어 있음**
- [x] `AGameStateBase` 는 빈 베이스([GameStateBase.h](../KraftonEngine/Source/Engine/GameFramework/GameMode/GameStateBase.h)) — score/wave/remaining 필드 없음. 원시 `GetGameTimeSeconds` 만 존재. → 점수/웨이브 HUD 는 GameState 서브클래스에 필드 신설 후 바인딩.

---

## C. 바인딩 메커니즘 — 결론

### C1. POLL vs PUSH → **POLL 확정**
- [x] **델리게이트 라이브러리는 있다**: `TDelegate/TMulticastDelegate` + `Broadcast`/`AddWeakUObject`([Delegate.h:43,168,240,271](../KraftonEngine/Source/Engine/Core/Delegate.h)), 엔진 이벤트(overlap/hit/particle)에 실사용.
- [x] **그러나 게임플레이 "값 변경" 델리게이트는 0건**: `OnHealthChanged`/`On*Changed` 류 없음. `APawn::GetDamaged` 는 `CurrentHealth` 만 바꾸고 아무 이벤트도 안 쏨([Pawn.cpp:47-61](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.cpp)). → push 하려면 소스마다 델리게이트 신설 필요 = 부적합.
- ⇒ **[결정] 매 프레임 POLL.** 이미 채택된 방식이자 per-frame LayoutAll 모델과 정합.

### C2. 업데이트 seam → **`AUICanvasActor::Tick`**
- [x] **씨앗 자리 그대로 확장**([UICanvasActor.cpp:113-168](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)). WorldTick 내부(게임플레이 정산 후) + LayoutAll 직전(요소 값 소비 전) → 순서 정합. 별도 HUD 매니저/LayoutAll 내부 훅은 불필요(중복).

### C3. 소스 해석 — 3가지
- [x] **(a) 로컬 플레이어**(권고 기본): `GetFirstPlayerController()->GetPossessedPawn()`(B1). 이름 스캔보다 견고.
- [x] **(b) 이름 지정**(현행): `HealthSourceActorName` → `World->GetActors()` 스캔. 임의 액터(특정 NPC 등) 타깃 시 유지.
- [x] **(c) 컴포넌트**: 보스 등 `GetComponentByClass<T>()`(B3).

### C4. 리플렉션 bind-by-name — **가능하나 보류**
- [x] **존재**: `UClass::GetPropertyRefs`([UStruct.h:226](../KraftonEngine/Source/Engine/Object/Reflection/UStruct.h)) + `FProperty::GetValuePtrFor`([PropertyTypes.h:271](../KraftonEngine/Source/Engine/Core/Types/PropertyTypes.h)), Lua 가 동일 패턴으로 이름 프로퍼티 read([LuaScriptManager.cpp:1672-1699](../KraftonEngine/Source/Engine/Component/Script/LuaScriptManager.cpp)). 게터 `GetHealthRatio` 도 `UFUNCTION(Pure)` 라 invoke-by-name 가능([Pawn.h:68-69](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.h)).
- ⇒ **[설계] 고정 HUD엔 과설계** — 구체 게터 폴이 더 단순. 디자이너가 임의 프로퍼티를 자유 바인딩하는 단계가 오면 그때 도입.

### C5. 값 → 채널 → setter 매핑표
| 바인딩 채널 | 소스 값(예) | setter | relayout |
|---|---|---|---|
| **width(바)** | `GetHealthRatio()`·`BossHealthRatio` | `SetSize({Full*Ratio, Y})`(좌피벗) | 필요(같은 프레임 처리) |
| **color(피드백)** | `1-Ratio` 로 green→red | `SetColor(lerp)` | 불필요 |
| **text(숫자)** | HP수치·속도·시간 | `SetText(format(v))` | 불필요(재푸시) |
| **text-color** | 페이즈/위험 | `SetTextColor` | 불필요 |
| **position(앵커)** | 보스 `GetActorLocation()`→화면 | `SetPosition(project(world))` | 필요 |

---

## D. 체력바 fill / 색 / 텍스트 — 시각 구현

### D1. fill = **width-resize + 좌피벗** (현행 확정)
- [x] **레이아웃 식**: `FinalPos = ParentOrigin + ParentSize*Anchor + Position − Size*Pivot`([UICanvasManager.cpp:131-132](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp)). 좌상단 = `−Size*Pivot` 이므로 **pivot.X=0 → 왼쪽 모서리 고정 → 폭 감소가 좌→우**(일반 체력바). pivot.X=0.5 면 중앙, 1 이면 우측 수축.
- [x] **(옵션) track**: 어두운 배경 요소 + 밝은 fill 자식. SimpleUIPass 가 자식 재귀+pre-order 덧칠([SimpleUIPass.cpp:78-84](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp))이라 구조적으로 무료. 빈 구간 배경을 보이려면 추가, 아니면 불필요.
- [x] **UV fill-amount 불가**: 셰이더 샘플 없음(A4) + 클립 없음 → **반드시 width 구동.**

### D2. 색 피드백(green→red) = **매 프레임 `SetColor`** (relayout 불필요)
- [x] `SetColor` → `BackgroundColor`, draw 가 매 프레임 live read([SimpleUIPass.cpp:57,65-68](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)). 씨앗의 `SetSize` 옆에 `Bar->SetColor(Lerp(Green, Red, 1-Ratio))` 한 줄 추가로 완성. 텍스트색은 `SetTextColor`([UITextElement.cpp:86](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.cpp)).

### D3. 숫자 텍스트 = **`SetText` 매 프레임** (재마운트 아님)
- [x] 대상 `UUITextElement` 를 `FindByName` 후 매 프레임 `SetText(format(value))`. 위젯 1회 마운트 후 재푸시뿐([UITextElement.cpp:57-77](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.cpp)) → 매 프레임 호출 안전.

### D4. 텍스처 아트(아이콘/스프라이트 바) — **[결정] Option A: 요소 트리 in-tree 확장**
- [x] **현재 = 단색 RGBA 쿼드 end-to-end**: `UUIImage` 텍스처 참조 없음([UIImage.h:7](../KraftonEngine/Source/Engine/UI/Canvas/UIImage.h)), SimpleUIPass 가 전 요소를 **단일 `DrawIndexed` 1회**로 그림([SimpleUIPass.cpp:135-193](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)), SRV/샘플러 0, PS=`return Input.Color`([SimpleUI.hlsl:43-46](../KraftonEngine/Shaders/UI/SimpleUI.hlsl)). 단 **정점이 UV 를 이미 보유**(`FSimpleUIVertex{...,U,V}` [SimpleUIPass.cpp:23-28](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp), 0/1 코너 [:65-68](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)) — 셰이더만 무시.
- [x] **재사용 가능한 텍스처 파이프라인**: RmlUi 인터페이스에 WIC 로드/SRV/샘플러/white-fallback/scissor 완비([UIManager.cpp:337-391,508-534,470-482](../KraftonEngine/Source/Engine/UI/UIManager.cpp)) → **A 의 D3D11 프리미티브는 여기서 차용.**
- [결정] **Option A 채택**(B=RmlUi / C=단색대체 기각). 근거: 아이콘/바가 RectTransform 트리 1급으로 들어와 **에디터 배치·드래그·FindByName·.Scene 직렬화·HUD 바인딩(SetSize/SetColor)** 통일. 정점 UV 가 이미 있어 셰이더 변경 최소.
- [설계] **구현 골격**: ① `UUIImage` 에 `FSoftObjectPtr TexturePath`(`UPROPERTY(Edit,Save)` — [StaticMeshComponent.h:73-74](../KraftonEngine/Source/Engine/Component/Primitive/StaticMeshComponent.h) 선례) + 로드된 SRV 캐시. ② 텍스처 샘플 셰이더 변형(`tex.Sample(samp,uv) * Input.Color`); 텍스처 없는 단색 요소는 **1×1 white SRV** 로 동일 셰이더 통과(분기 없이 곱셈 항등). ③ `SimpleUIPass::Execute` 에 SRV 바인드.
- [전제] **배칭 하위선택(A-1/A-2) = 단일 `DrawIndexed` 가 깨지는 지점**:
  - **A-1 요소별 텍스처**: draw call = 고유 텍스처 수(텍스처별 그룹핑 + `PSSetShaderResources` + `DrawIndexed`). HUD 아이콘 소수면 무해. → **시작점 권고**(아틀라스 파이프라인 없이 최소 구현).
  - **A-2 단일 UI 아틀라스 + 요소별 UV rect**: 단일 draw 보존(텍스처 1장), 정점 UV 를 sub-rect 로 교체. 아이콘 다수 시 이전. → **아이콘 수 증가가 트리거.**
- [전제] **스프라이트 바 fill**: (a) 폭 스케일 시 텍스처 **가로 찌그러짐** 감수, 또는 (b) fill 쿼드 우측 UV=`ratio`(폭=`ratio*Full`) 저작으로 찌그러짐 없이 L→R 노출 → **요소별 UV 노출**(현재 0/1 하드코딩 [:65-68](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp))이 선결.
- [전제·결정 귀결] **9-slice 프레임은 공짜가 아님**: B(RmlUi `tiled-box` decorator)를 포기했으므로, 테두리 비뚤어짐 없는 스프라이트 바 프레임이 필요해지면 **SimpleUI 에 9-slice 지오메트리(4×4 정점)를 직접** 추가. MVP 는 단순 텍스처 fill 로 시작, 9-slice 는 필요 시 후행.

---

## E. 월드앵커 바 (보스/적 머리 위 HP) — **가능, 신규 수학 없음**

### E1. world→screen 재료가 이미 다 있다
- [x] **카메라 POV**: `UWorld::GetActivePOV(POV)`([World.cpp:214](../KraftonEngine/Source/Engine/GameFramework/World.cpp)) → `FMinimalViewInfo::CalculateViewProjectionMatrix()`(row-major LH, View*Proj). 렌더가 쓰는 바로 그 POV(셰이크/블렌드 반영).
- [x] **뷰포트 크기**: `FFrameContext::ViewportWidth/Height`([FrameContext.h:61-62](../KraftonEngine/Source/Engine/Render/Types/FrameContext.h)).
- [x] **정방향 투영 선례(에디터)**: 마퀴 선택이 `VP.TransformPositionWithW(WorldPos)` → NDC → 픽셀 `x=(X*0.5+0.5)*W`, `y=(1-(Y*0.5+0.5))*H`([EditorViewportClient.cpp:666-684](../KraftonEngine/Source/Editor/Viewport/EditorViewportClient.cpp)) — **좌상단/Y-down, UI 쿼드 좌표와 동일**. 역방향 `DeprojectScreenToWorld` 는 런타임 POV 메서드로 이미 존재(NDC 규약 동일).

### E2. [설계] 런타임 노출 1개만 추가
- [x] **`FMinimalViewInfo::ProjectWorldToScreen(WorldPos, W, H)` 신설**(기존 `DeprojectScreenToWorld` 의 역, 동일 규약) — 에디터 정방향 코드는 Editor TU 라 런타임에서 못 부름. **⚠️ behind-camera reject 필수**(`TransformPositionWithW` 가 w 부호를 감춤 → 카메라 뒤 점이 유효 NDC 로 위장). w<=eps 면 숨김.
- [x] **구동**: 매 프레임 보스 `GetActorLocation()`(+Z 오프셋) → `ProjectWorldToScreen` → `Bar->SetPosition(pixel)`. 화면 밖/뒤면 요소 숨김.
- [전제] **틱 시점 뷰포트 크기**: `FFrameContext` 는 렌더 파이프라인 소유라 액터 Tick 에서 직접 접근 불가 → `GetActivePOV` + 게임 뷰포트 W/H 로 재현(렌더가 하는 것과 동일).

---

## F. HUD 소유 / 수명

### F1. [설계] 스폰 — GameMode 훅, 플레이어 존재 보장
- [x] **`AGameModeBase::StartMatch()`**([GameModeBase.h:36](../KraftonEngine/Source/Engine/GameFramework/GameMode/GameModeBase.h))이 `UWorld::BeginPlay` 에서 **모든 액터 BeginPlay 후 + AutoPossess 후** 호출됨([World.cpp:598](../KraftonEngine/Source/Engine/GameFramework/World.cpp)). 여기서 `SpawnActor<AUICanvasActor>()` + `SetUIAssetPath` + 바인딩 필드 세팅 → **possess 된 폰이 이미 있어 로컬 플레이어 즉시 바인딩**. (GameMode 자체가 런타임 전용 스폰이라 수명이 HUD 의도와 정합.)
- [x] **persistence**: PersistentLevel 소유 + `AActor::AddReferencedObjects` 가 RootComponent 로 트리 keepalive([UICanvasActor.h:15-16](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h)).

### F2. [사실] 등록 게이트 = PIE/Game 한정
- [x] `RegisterCanvas` 는 `BeginPlay` 에서만([UICanvasActor.cpp:91-94](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp)) → 편집 월드 미렌더(정상). HUD 수명이 매치에 정확히 스코프됨.

---

## G. 리스크 & 구현 사이클 분할

### 리스크 등록부
| ID | 리스크 | 근거 | 완화 |
|----|--------|------|------|
| **R1** | **잔탄 데이터 부재** — UI 만 만들면 표시할 값이 없음 | 무기/탄약/풀 count 전무([ProjectilePoolSubSystem.h:28-48](../KraftonEngine/Source/Engine/GameFramework/ProjectilePoolSubSystem.h)) | 게임플레이 탄약 상태 **선신설**(사이클 7), HUD 와 분리 |
| **R2** | 다중 바인딩 일반화 시 단일 필드(HealthBar*) 구조 한계 | 현재 1쌍 필드뿐([UICanvasActor.h:62-70](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h)) | 바인딩 디스크립터 배열로 치환(사이클 4) |
| **R3** | 월드앵커: 카메라 뒤 점이 유효 화면좌표로 위장 | `TransformPositionWithW` w 부호 소실 | behind-camera reject(E2) |
| **R4** | 텍스처 아이콘 불가(단색 전용) → **[결정] A 로 in-tree 확장** | [SimpleUI.hlsl:43-46](../KraftonEngine/Shaders/UI/SimpleUI.hlsl) | 단일 `DrawIndexed` 배칭 깨짐 주의(A-1 시작 / A-2 아틀라스), 9-slice 는 후행(사이클 8) |
| **R5** | 보스 셀렉터와 HUD 틱 순서 → 1프레임 지연 가능 | 둘 다 액터/컴포넌트 틱 | 미관상 무시 가능 |
| **R6** | 텍스트 채널이 비가시(bVisibleRect=false) 요소면 에디터 클릭선택 불가 | UIText 진단 R5 | 계층트리 선택 우회(기존 방침) |

### [설계] 구현 사이클 분할 (각 사이클: msbuild 0-error → 1커밋 → 런타임 점검)
| # | 사이클 | 산출물 | 의존 |
|---|---|---|---|
| 1 | **로컬 플레이어 타게팅** | 씨앗 소스 해석에 `GetFirstPlayerController()->GetPossessedPawn()` 추가(이름은 override 로 유지). 플레이어 체력바 자동 타깃 | 씨앗 |
| 2 | **색 피드백** | `SetColor` green→red lerp(`1-Ratio`)를 틱에 추가 | 1 |
| 3 | **숫자 텍스트 바인딩** | `UUITextElement` 대상에 `SetText(format(value))` — **존재하는 값**(HP수치/속도/`GetGameTimeSeconds`)로 검증 | 1 |
| 4 | **다중 바인딩 일반화(핵심)** | `HealthBar*` 단일 필드 → 바인딩 디스크립터 배열 `{요소명, 소스, 값, 채널}`. width/color/text 채널 디스패치 | 1,2,3 |
| 5 | **보스바** | `GetComponentByClass<UBossPatternSelectorComponent>()` → `BossHealthRatio`/`BossPhase` width+페이즈색 | 4 |
| 6 | **월드앵커 바** | `FMinimalViewInfo::ProjectWorldToScreen` 신설(+behind-camera reject) + position 채널 → 보스/적 머리 위 HP | 4,5 |
| 7 | **[갭선결] 잔탄 데이터 + HUD** | ① 게임플레이 탄약 상태 신설(Pawn/무기 `Ammo` 필드+getter, 또는 풀 `GetFreeCount`) → ② 텍스트 바인딩 | 4 + **게임플레이 신설** |
| 8 | **텍스처 HUD 아트 [결정 A]** | `UUIImage` `FSoftObjectPtr TexturePath`+SRV · 텍스처 샘플 셰이더 변형+1×1 white fallback · SimpleUIPass SRV 바인드(**A-1 요소별 시작**) · (스프라이트 바 시)요소별 UV 노출 | 4 |

> **MVP 경계**: 사이클 1~5 가 "기존 데이터로 즉시 되는 HUD"(플레이어/보스 체력바·색·숫자). 사이클 6 은 수학 노출 1개. **사이클 7(잔탄)·8(텍스처)·B6(점수/웨이브)는 UI 가 아니라 선행 데이터/기능 신설이 본체** — "기존 UI 연동"으로 묶지 말 것.

---

## 파일 인덱스 (빠른 참조)
| 역할 | 위치 | 비고 |
|------|------|------|
| **씨앗 바인딩** | [UICanvasActor.cpp:113-168](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.cpp) | Tick→UpdateHealthBarBinding, SetSize 스케일 |
| 바인딩 필드 | [UICanvasActor.h:62-70](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasActor.h) | HealthBarElementName/HealthSourceActorName |
| 틱 seam/순서 | [Engine.cpp:180,188,190](../KraftonEngine/Source/Engine/Runtime/Engine.cpp) | WorldTick→LayoutAll→Render |
| 요소 setter | [UIElement.h:23-24,39,73](../KraftonEngine/Source/Engine/UI/Canvas/UIElement.h) | SetPosition/SetSize/FindByName/SetColor |
| 텍스트 setter | [UITextElement.h:22,34](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.h)·[.cpp:57-86](../KraftonEngine/Source/Engine/UI/Canvas/UITextElement.cpp) | SetText/SetTextColor, 1회 마운트 |
| 레이아웃 식 | [UICanvasManager.cpp:131-138](../KraftonEngine/Source/Engine/UI/Canvas/UICanvasManager.cpp) | FinalPos = Origin+Size*Anchor+Pos−Size*Pivot |
| 드로우(단색) | [SimpleUIPass.cpp:47-85,167](../KraftonEngine/Source/Engine/Render/RenderPass/SimpleUIPass.cpp)·[SimpleUI.hlsl:43-46](../KraftonEngine/Shaders/UI/SimpleUI.hlsl) | per-element color, 텍스처/클립 없음 |
| 플레이어 체력 | [Pawn.h:64-71,110-114](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.h)·[Pawn.cpp:63-70](../KraftonEngine/Source/Engine/GameFramework/Pawn/Pawn.cpp) | GetHealthRatio 등 |
| 플레이어 도달 | [World.cpp:186](../KraftonEngine/Source/Engine/GameFramework/World.cpp)·[PlayerController.h:36,44](../KraftonEngine/Source/Engine/GameFramework/GameMode/PlayerController.h) | GetFirstPlayerController→GetPossessedPawn |
| 보스 HP/페이즈 | [BossPatternSelectorComponent.cpp:206-244](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.cpp)·[BossPatternComponentBase.h:40-56](../KraftonEngine/Source/Engine/Component/Gameplay/BossPatternComponentBase.h) | GetBossPatternDebugState |
| world→screen | [EditorViewportClient.cpp:666-684](../KraftonEngine/Source/Editor/Viewport/EditorViewportClient.cpp)·[World.cpp:214](../KraftonEngine/Source/Engine/GameFramework/World.cpp)·[FrameContext.h:61-62](../KraftonEngine/Source/Engine/Render/Types/FrameContext.h) | VP·POV·뷰포트, 정방향 선례 |
| **[갭] 잔탄** | [ProjectilePoolSubSystem.h:28-48](../KraftonEngine/Source/Engine/GameFramework/ProjectilePoolSubSystem.h) | count getter 없음 |
| **[갭] 점수/웨이브** | [GameStateBase.h](../KraftonEngine/Source/Engine/GameFramework/GameMode/GameStateBase.h) | 빈 베이스 |
| HUD 스폰 훅 | [GameModeBase.h:36](../KraftonEngine/Source/Engine/GameFramework/GameMode/GameModeBase.h)·[World.cpp:598](../KraftonEngine/Source/Engine/GameFramework/World.cpp) | StartMatch(possess 후) |

---

### 한 줄 결론
**플레이어/보스 체력바·색·숫자 HUD 는 기존 인프라(씨앗 `UpdateHealthBarBinding` + `APawn` 체력 + POLL 틱)로 즉시 되고, 월드앵커 바는 투영 헬퍼 1개 노출이면 된다. 진짜 작업은 (1) 씨앗을 바인딩 디스크립터 리스트로 일반화, (2) 로컬 플레이어 타게팅 전환 — 두 가지다. 반면 잔탄·점수·텍스처 아이콘은 "UI 연동"이 아니라 선행 게임플레이/렌더 기능 신설이 본체이므로 별도 트랙으로 분리하라.**
