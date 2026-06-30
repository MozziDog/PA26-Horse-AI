#pragma once
// =====================================================================================
// Tier-1 Precompiled Header (PCH)
// -------------------------------------------------------------------------------------
// 광범위하게 include 되면서 거의 변하지 않는 "초안정" 헤더만 모은다.
// codegen 결합 헤더(*.generated.h 를 끌어오는 Object.h / EngineTypes.h / Matrix·Quat·
// Transform.h 등)는 PCH 무효화 위험 때문에 의도적으로 제외(Tier-2).
//
// 이 PCH 는 GenerateProjectFiles.py 가 모든 엔진 C++ TU 에 ForcedIncludeFiles 로 주입한다.
// ThirdParty(*) / 생성 허브(Reflection.generated.cpp) / .c 파일은 제외된다.
// =====================================================================================

// --- STL (전 TU 광역 사용) ---
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// --- 수학 SIMD 백엔드 (Math/Vector.h 가 끌어옴; 146+ TU 에 전이 포함) ---
#include <DirectXMath.h>

// --- 엔진 초안정 코어 (codegen 비결합 확인됨) ---
#include "Core/Types/CoreTypes.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/FName.h"
#include "Math/Vector.h"
#include "Core/Logging/Log.h"
