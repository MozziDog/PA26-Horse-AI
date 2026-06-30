# FBX Importer — PreRotation / RotationOrder 버그 분석 및 수정 기록

> **상태:** ✅ 수정 완료 (옵션 C) · 엔진 빌드 통과 · 시각 검증은 사용자 재임포트로 확정
> **수정 파일:** `Source/Engine/Mesh/Importer/Fbx/FbxAnimationImporter.cpp` (6줄)
> **대상:** Mixamo auto-rigging FBX (회전이 PreRotation 에 저장된 파일)
> **재현 데이터:** `Content/Data/Haru/problem/{engine_problem.fbx, engine_ok.fbx}`

---

## 1. TL;DR

- **증상:** Mixamo 원본 FBX(`engine_problem`)를 임포트하면 **애니메이션 재생 시 어깨/팔꿈치가 ~180° 꺾여** 보임. Blender에서 z-forward로 재익스포트한 파일(`engine_ok`)은 정상.
- **근본 원인:** `DeepConvertScene`(Y-up→Z-up 축 변환)이 본의 `RotationOrder`를 **XYZ → YZX**로 바꾼다. 그런데 엔진의 per-frame 애니메이션 경로는 **PreRotation/PostRotation**을 그 바뀐 노드 순서(YZX)로 합성했다. FBX 규약상 pre/post rotation은 **노드 순서와 무관하게 항상 고정 XYZ**로 합성해야 한다.
- **수정:** per-frame 경로의 pre/post rotation 합성 인자를 `RotationOrder` → `eEulerXYZ` 고정으로 변경 (6곳).
- **검증:** standalone FBX SDK 하니스로, 수정 전 어깨 오차 **1.86**(≈180° 플립) → 수정 동작(XYZ) 시 SDK 대비 오차 **0.000000** (전 본·전 프레임).

---

## 2. 증상

| 파일 | 출처 | 결과 |
|---|---|---|
| `engine_problem.fbx` | Mixamo 원본을 엔진에 직접 사용 | 애니메이션 재생 시 **어깨/팔꿈치 180° 꺾임** |
| `engine_ok.fbx` | Blender에서 z-forward export | 동일 엔진에서 **정상** |

핵심 단서: rest(정지) 포즈는 멀쩡한데 **애니메이션 재생 시에만** 특정 본이 꺾였다. 그리고 같은 캐릭터라도 "회전을 어디에 저장했는지"가 두 파일에서 달랐다.

---

## 3. 두 파일의 구조 차이 (바이너리 파싱으로 확인)

| 속성 | engine_problem (v7700) | engine_ok (v7400, Blender) |
|---|---|---|
| 회전 저장 위치 | **PreRotation** (per-bone) | **Lcl Rotation** (베이크됨) |
| `RotationActive` | true | false |
| `RotationOrder` (소스) | XYZ | XYZ |
| `UpAxis` | +Y | +Y |
| `FrontAxisSign` / `CoordAxisSign` | +1 / +1 | −1 / −1 |
| Armature 래퍼 노드 | 없음 | 있음 (`Lcl Rot=(0,−180,0)`) |
| `LeftShoulder` 회전값 | `PreRot=(−36.4, 81.8, −179.9)` | `LclRot=(126.1, 113.5, −23.1)` |

> FrontAxis·CoordAxis 부호가 둘 다 반전 = handedness 동일 + **Up축 기준 180° yaw 차이**(정면 방향). 이는 플립의 직접 원인이 아니라 두 파일이 다른 경로로 변환되게 만드는 배경 조건이다.

---

## 4. 진단 여정 (가설의 변천 — 복습 포인트)

이 버그는 **세 번 가설이 바뀌었다.** 성급한 단정이 위험했던 사례로 남길 가치가 있다.

1. **가설 A (초기):** "임포터가 `Lcl*.Get()`만 읽어 PreRotation을 통째로 누락한다."
   → **반증.** 코드를 읽으니 per-frame 경로 `EvaluateLocalFbxMatrixFromCurves`는 전체 pivot 공식(`T·Roff·Rp·Rpre·R·Rpost⁻¹·Rp⁻¹·Soff·Sp·S·Sp⁻¹`)으로 PreRotation을 **이미 포함**하고 있었다. rest 경로는 SDK `EvaluateLocalTransform` 사용으로 더더욱 정상.

2. **가설 B (중간):** "`DeepConvertScene`이 PreRotation을 축 변환에서 누락하거나 handedness를 못 다룬다."
   → **부분 반증.** SDK 문서상 ConvertScene은 변환을 *pre-rotation으로 주입*하고(`AdjustPreRotation`), repo의 모든 Mixamo 샘플은 RotationOrder=XYZ였다. 정적 분석만으로는 "임포터 재구성 == SDK"여야 해서 모순이 남았다.

3. **가설 C (확정, 실측):** standalone FBX SDK 하니스로 변환 전/후를 실측하니 — **`DeepConvertScene`이 RotationOrder를 XYZ→YZX로 바꾼다**는 결정적 사실이 드러났다. 엔진은 pre/post를 그 YZX로 합성 → FBX 규약(고정 XYZ) 위반 → 다축 큰 각을 가진 본만 깨짐.

> **교훈:** "코드를 읽어 도출한 당위(should match)"와 "실제 런타임 동작"이 갈릴 때는 **실측**(여기선 SDK를 링크한 standalone 하니스)이 진실을 가른다. RotationOrder가 변환 중에 바뀐다는 것은 코드만 봐선 보이지 않았다.

---

## 5. 근본 원인 (확정)

### 인과 사슬
1. 임포트 파이프라인: `Load → NormalizeScene(DeepConvertScene) → Skeleton → Skin → Animation`
   ([`FbxImporter.cpp:347`](FbxImporter.cpp), [`FbxSceneLoader.cpp:423-428`](Fbx/FbxSceneLoader.cpp))
2. **`DeepConvertScene`(Y-up→Z-up, LH)이 모든 본의 `RotationOrder`를 XYZ(0) → YZX(2)로 변경**하고 PreRotation 값을 그에 맞게 치환한다. *(실측 확인)*
3. per-frame 경로 `EvaluateLocalFbxMatrixFromCurves`는 PreRotation/PostRotation을 **노드 RotationOrder(=YZX)** 로 euler→matrix 합성했다.
4. 그러나 FBX 규약상 **pre/post rotation은 항상 고정 XYZ**로 합성해야 한다.
   - 근거: `ThirdParty/fbx/include/fbxsdk/scene/geometry/fbxnode.h:598`
     > `// Rotation order don't affect post rotation, so just use the default XYZ order`
5. → 본의 PreRotation이 **다축에 큰 각**을 가지면 YZX vs XYZ 합성 차이가 ≈180° 오류로 폭발. 단일축/소각이면 사실상 무해.

### "왜 어깨만?" (오차가 PreRotation 각도에 의존)
- **어깨**: PreRot=(180, 36.6, 81.8) — 3축 모두 큰 각 → 순서 뒤바뀜 = 거의 완전한 플립.
- **다리(UpLeg)**: PreRot=(−179.5, −0.5, 0) — 사실상 단일축 180° → euler 순서 불변 → 정상으로 보임.
- **팔꿈치(ForeArm)**: PreRot=(−9.5, −1.3, 0.2) — 소각 → 거의 무해.
- **engine_ok**: PreRot=(0,0,0) (회전이 Lcl Rotation에 있음) → 순서 뒤바뀜이 적용될 대상이 없음 → 항상 정상.

### 버그 위치 = per-frame 경로(b) 단독
- rest/bind 경로([`FbxSkeletonImporter.cpp:48`](Fbx/FbxSkeletonImporter.cpp), SDK `EvaluateLocalTransform`)와 skin bind([`FbxSkinWeightImporter.cpp:285,387`](Fbx/FbxSkinWeightImporter.cpp), cluster `GetTransformLinkMatrix`)는 **결함 없음**. → "rest 정상, 애니에서만 플립" 증상과 정확히 일치.

---

## 6. 실측 증거 (standalone FBX SDK 하니스)

엔진과 동일하게 `FbxSystemUnit::m.ConvertScene` → `FbxAxisSystem(eZAxis, eParityEven, eLeftHanded).DeepConvertScene` 적용 후, 각 본에 대해:
- `manual(nodeOrder)Err` = |SDK `EvaluateLocalTransform(t)` − 엔진 수동 재구성(pre/post=노드순서)|
- `manual(XYZpre/post)Err` = |SDK − 수동 재구성(pre/post=XYZ 고정)|

| 파일 / 본 | RotOrder 전→후 | PreRot(후) | **nodeOrder 오차** | **XYZ 오차** |
|---|---|---|---|---|
| problem `LeftShoulder` | XYZ→**YZX** | (180.0, 36.6, 81.8) | **1.855 / 1.957** | **0.000000** |
| problem `LeftForeArm` | XYZ→YZX | (−9.5, −1.3, 0.2) | 0.0035 | 0.000000 |
| problem `LeftUpLeg` | XYZ→YZX | (−179.5, −0.5, 0.0) | 0.016 | 0.000000 |
| ok `LeftShoulder` | XYZ→YZX | (0,0,0) | 0.000000 | 0.000000 |
| ok `LeftForeArm` / `LeftUpLeg` | XYZ→YZX | (0,0,0) | 0.000000 | 0.000000 |

> Q2(node global at rest vs cluster bind link) 잔차도 engine_problem에서 ~0 → rest/bind 일관성 정상. 버그는 순수 per-frame.

**결론:** pre/post를 XYZ로 고정하면 SDK와 오차가 **정확히 0** → 단일 원인 확정.

---

## 7. 수정 내용 (옵션 C)

`FbxAnimationImporter.cpp`에서 pre/post rotation 합성의 2번째 인자를 `RotationOrder` → `eEulerXYZ`로 고정 (6곳).

```cpp
// BEFORE (버그) — 노드 RotationOrder(변환 후 YZX)로 pre/post 합성
const FbxAMatrix PreRotationMatrix  = bRotationActive
    ? MakeFbxRotationMatrixByOrder(Node->GetPreRotation(FbxNode::eSourcePivot),  RotationOrder) : Identity;
const FbxAMatrix PostRotationMatrix = bRotationActive
    ? MakeFbxRotationMatrixByOrder(Node->GetPostRotation(FbxNode::eSourcePivot), RotationOrder) : Identity;

// AFTER (수정) — FBX 규약대로 pre/post는 항상 XYZ
const FbxAMatrix PreRotationMatrix  = bRotationActive
    ? MakeFbxRotationMatrixByOrder(Node->GetPreRotation(FbxNode::eSourcePivot),  eEulerXYZ /* FBX: pre/post는 항상 XYZ */) : Identity;
const FbxAMatrix PostRotationMatrix = bRotationActive
    ? MakeFbxRotationMatrixByOrder(Node->GetPostRotation(FbxNode::eSourcePivot), eEulerXYZ /* FBX: pre/post는 항상 XYZ */) : Identity;
```

| 함수 | 라인(pre/post) | 경로 |
|---|---|---|
| `EvaluateLocalFbxMatrixFromCurves` | 262, 266 | 단일 레이어 |
| `MakeLocalFbxMatrixFromTRS` | 1042, 1045 | 멀티 레이어 (Euler) |
| `MakeLocalFbxMatrixFromTRSQuat` | 1073, 1076 | 멀티 레이어 (Quat) |

**건드리지 않은 곳 (순서 의존이 정상):**
- `RotationMatrix`(Lcl Rotation) 합성 — 264, 1044 → `RotationOrder` 유지
- `MakeQuatFromFbxEulerDegree`(Lcl Rotation의 euler→quat) — 961 → `RotationOrder` 유지

---

## 8. 수정 옵션 비교 (왜 C인가)

| 옵션 | 내용 | 결과 | 채택 |
|---|---|---|---|
| **C** | pre/post 합성을 `eEulerXYZ` 고정 | 6줄, 멀티레이어 블렌딩·rest·성능 보존, 실측 오차 0 | ✅ **채택** |
| A | import 직후 `ConvertPivotAnimationRecursive`로 pre/post를 Lcl 커브에 베이크 | 동작하나 레이어별 베이크 미검증·키 리샘플 → 중~고 리스크 | ❌ |
| B | per-frame을 `EvaluateLocalTransform`(SdkEvaluatorOnly)로 교체 | 회전은 고쳐지나 **`CompositeAnimLayersToLocalTRS` 우회 → 베이스 외 모든 애니 레이어 무음 손실** (고/치명적 회귀) | ❌ |

> 주의: 직관적으로 "정석"처럼 보이는 옵션 B가 이 코드베이스에선 위험하다. per-frame 경로는 **멀티레이어 블렌딩을 직접 구현**한 것이라 SDK 평가로 바꾸면 그 기능을 잃는다.

---

## 9. 검증

- ✅ **엔진 빌드**: MSBuild `Release|x64` 증분 빌드 — **오류 0개** (경고 33개는 기존, 무관).
- ✅ **수학적 정확성**: 하니스에서 수정 동작(pre/post=XYZ)이 SDK 대비 오차 **0.000000** (전 본·전 프레임).
- ✅ **변경 범위**: `FbxAnimationImporter.cpp` 1파일 6줄(`6 insertions / 6 deletions`), 의도 외 변경 없음.
- ⏳ **시각 검증 (사용자 측, 에디터 GUI 필요):**
  1. 에디터에서 `engine_problem.fbx` **재임포트** (기존 임포트 에셋은 옛 베이크값이라 갱신 필요)
  2. 애니메이션 재생 → 어깨/팔꿈치 플립 소멸 확인, 다른 본 불변 확인
  3. (선택, 비-GUI 회귀가드) `GValidateDirectBakeAgainstSdk = true`로 재임포트 → mismatch 로그가 전 본 0 근처면 OK

---

## 10. 교훈 & 재발 방지

- **교훈 1:** 축 변환(`DeepConvertScene`)은 좌표뿐 아니라 **노드의 RotationOrder까지 바꾼다.** pre/post rotation을 노드 순서로 합성하면 변환 후 깨진다.
- **교훈 2:** FBX의 pre/post rotation은 **항상 XYZ 고정**. Lcl Rotation만 노드 RotationOrder를 따른다. (둘을 같은 헬퍼로 처리할 때 인자를 구분할 것.)
- **교훈 3:** "코드상 같아야 한다"와 실제가 갈리면 **SDK를 링크한 standalone 하니스로 실측**. 정적 추론의 사각지대(여기선 RotationOrder 변경)를 잡아낸다.
- **재발 방지(권장):** 디버그 빌드에서 `GValidateDirectBakeAgainstSdk = true` 상시화 또는 CI에 하니스 회귀 테스트 편입 → 수동 재구성과 SDK의 오차가 임계 초과 시 경고.

---

## 11. 부록

### 진단 하니스
- 위치: `Intermediate/fbxdiag/fbxdiag.cpp` (git-ignore). FBX SDK를 링크해 엔진과 동일한 정규화 후 본별 `EvaluateLocalTransform` vs 수동 재구성(node-order / XYZ) 오차를 출력.
- 빌드: `Intermediate/fbxdiag/build.bat` (vcvars64 + cl, libfbxsdk.dll 동봉).
- 회귀 테스트로 보존 가치 있음 — 향후 pre/post 6곳 오차가 0 유지되는지 재확인용.

### 참고 코드/문서 위치
- 결함/수정: `Source/Engine/Mesh/Importer/Fbx/FbxAnimationImporter.cpp` (262/266, 1042/1045, 1073/1076)
- 축 변환: `Source/Engine/Mesh/Importer/Fbx/FbxSceneLoader.cpp:423-428` (`DeepConvertScene`)
- rest 경로(정상): `Source/Engine/Mesh/Importer/Fbx/FbxSkeletonImporter.cpp:48`
- skin bind(정상): `Source/Engine/Mesh/Importer/Fbx/FbxSkinWeightImporter.cpp:285,387`
- FBX 규약 근거: `ThirdParty/fbx/include/fbxsdk/scene/geometry/fbxnode.h:598` (pre/post는 XYZ), `:1414` (RotationOrder 기본 XYZ), `:711-716,1496` (RotationActive 기본 false / false면 pre·post·order 무시)

### 용어
- **PreRotation (Rpre)**: 애니메이션 회전 *앞에* 적용되는 정적 회전(조인트 오리엔트). Mixamo 원본은 rest 방향을 여기에 저장.
- **RotationOrder**: Lcl Rotation의 euler 적용 순서. **pre/post에는 적용되지 않음(항상 XYZ).**
- **DeepConvertScene**: 씬 전체를 목표 축계로 변환. 변환 회전을 pre-rotation으로 주입하며 RotationOrder도 갱신.
