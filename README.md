# Horse AI Research
## Overview
도로를 추종하고, 플레이어 입력에 반응하며, 장애물을 우회하는 말의 행동 AI를 구현한다.

## Reference
[The Legend of Zelda: Breath Of The Wild - Switch - Horse Riding](https://youtu.be/lq6MznRT5tI?si=edZSRRu8y7blf9po)

## Goals
* 길을 따라 주행하는 상태가 아니라면 직진
* 길 가까이 있을 경우 길을 따라 자동으로 주행
* 플레이어의 입력에 따라 갈림길 선택
* 플레이어의 입력에 따라 길에서 이탈
* 상황에 따라 장애물 우회 또한 점프로 극복
* 장애물 사이즈, 조향각 등 고려해서 우회 불가능하다면 rearing과 함께 멈춰서기
* Debugging features
  * 길의 node와 edge 시각화
  * 말의 시선 등 정보 수집 시각화

## No-Goals
* '친밀도' 스탯에 따른 조작 반응성 변화 혹은 간헐적인 돌발 행동
* 말 탑승/하차 시스템
* Foot IK

## Plan
### W1
* 엔진 준비
* 테스트를 위한 고저차가 있는 맵 준비
### W2 ~ W3
* 도로 구현 및 시각화
* Steering 초기 구현
* Behavior tree 구현
* AI 프로토타입 작업
### W4
* 말 애니메이션 구현
* 장애물과의 상호작용 구현 (우회/점프/정지)
### W5
* 플레이어 조작에 대한 반응 구현
* 움직이는 다른 Agent와 충돌 회피 구현
### W6
* 조작성 & steering 품질 개선
  * 장애물/플레이어 조작으로 도로에서 벗어났을 때의 움직임 고도화, 조향각 변화 등 주행 속도에 따른 움직임 차이 구현 등
### W7
* AI 고도화
  * 절벽에 대한 반응, '공포심' 개념 추가 등
### W8
* 폴리싱 (가능할 경우)