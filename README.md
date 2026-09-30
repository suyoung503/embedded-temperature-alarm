# Embedded Temperature Alarm

**ATmega128과 uC/OS-II로 구현한 온도 경보 시스템입니다.** ATS75 센서에서 온도를 읽고 FND에 표시하며, 임계 온도 이상에서 LED와 버저로 알립니다. 버튼 인터럽트로 음소거와 최고 온도 표시를 처리합니다.

임베디드 수업 기말 프로젝트 · C · RTOS · TWI(I²C) · Interrupt · Mailbox · Message Queue

## 구현 목적

센서 입력, 표시 장치, 경보 출력, 사용자 입력을 각각의 태스크로 분리해 동시에 처리하는 임베디드 시스템을 구현했습니다. 단일 반복문 대신 RTOS 스케줄링과 태스크 간 메시지 전달 구조를 사용했습니다.

## 보드 및 개발 환경

| 구분 | 내용 |
|---|---|
| MCU | ATmega128, 소스의 `F_CPU=16000000UL` 기준 16 MHz |
| RTOS | uC/OS-II |
| 언어 | C, AVR 헤더 및 인터럽트 사용 |
| 온도 센서 | ATS75, TWI 통신 |
| 출력 | 4자리 FND, LED 8개, 버저 |
| 버튼 | INT4: 경보음 음소거 / INT5: 최고 온도 표시 |
| 빌드 의존성 | 수업용 uC/OS-II 포트, BSP, `includes.h`, OS 설정 및 시작 코드 |

정확한 교육용 보드 제품명과 IDE 버전은 제공 자료에 기록되어 있지 않습니다. 이 저장소는 애플리케이션 소스를 보존한 것으로, **BSP와 RTOS 포트가 빠져 있어 단독으로 빌드할 수 없습니다.**

## 주요 기능

- **온도 측정:** TemperatureTask가 ATS75를 초기화하고 약 1초마다 값을 읽습니다.
- **현재 온도 표시:** FND 네 자리에 부호·정수부·소수부(0 또는 5)를 구성하여 멀티플렉싱합니다.
- **온도 경보:** `ALARM_TH_C=30`을 기준으로 LED 점멸과 버저 패턴을 제어합니다.
- **음소거:** INT4에서 이벤트 플래그를 세우고 ButtonTask가 경보음을 끕니다. LED 경보는 유지합니다.
- **최고 온도:** INT5 입력 후 저장된 최고 온도를 약 2초간 표시합니다.

## 실행 구조

```text
main: OSInit → 포트·인터럽트·타이머 설정
      → Mailbox/Queue 생성 → 6개 Task 생성 → OSStart

ATS75 ──TWI──> TemperatureTask ──Mailbox──> FndTask
                                               │
                                         Message Queue
                                               ▼
                                        FndDisplayTask ──> FND

INT4 ──event flag──> ButtonTask ──mute──> BuzzerTask
INT5 ──show-max flag───────────────────> FndTask
FndTask ──alarm flag──> LedTask / BuzzerTask
Timer2 overflow ISR ──> 버저 핀 토글
```

| 태스크 | 우선순위 값 | 역할 |
|---|---:|---|
| TemperatureTask | 0 | 온도 읽기, 최고 온도 갱신, Mailbox 전달 |
| FndTask | 1 | 표시 데이터 변환, 경보 상태 판단, Queue 전달 |
| LedTask | 2 | 경보 시 LED를 약 300 ms 간격으로 점멸 |
| ButtonTask | 3 | INT4 이벤트 처리 및 음소거 |
| BuzzerTask | 4 | 200 ms ON / 300 ms OFF의 3회 패턴 반복 |
| FndDisplayTask | 5 | Queue의 표시 데이터로 FND 멀티플렉싱 |

### 실제 사용한 RTOS·인터럽트 기능

- `OSTaskCreate`, `OSTimeDlyHMSM`: 태스크 생성 및 주기 제어.
- `OSMboxCreate/Post/Pend`: 온도 데이터 포인터 전달.
- `OSQCreate/Post/Accept`: FND 표시 버퍼 포인터 전달.
- `INT4_vect`, `INT5_vect`: 버튼 이벤트 처리.
- `TIMER2_OVF_vect`: 버저 출력 토글.
- Timer0: uC/OS-II tick을 위한 초기화 코드. ISR 및 포트 구현은 외부 의존성입니다.

**Semaphore API는 이 소스에서 사용하지 않았습니다.** Mailbox와 Message Queue를 세마포어 사용 경험으로 대체해 표기하지 않습니다.

## 본인 역할

이수영의 기말 프로젝트 소스와 본인 명의 보고서를 정리했습니다. 보고서에서 다룬 구현 범위는 센서 입력, 6개 태스크 구성, 태스크 간 메시지 전달, FND·LED·버저 출력, 외부 인터럽트 기반 버튼 동작입니다. 제공되지 않은 uC/OS-II 커널이나 수업용 BSP를 직접 작성한 것으로 주장하지 않습니다.

## 소스 구성 및 실행 준비

```text
src/lab1.c   기말 프로젝트 애플리케이션 원본 소스
README.md    하드웨어·태스크·실행 구조 및 한계
```

1. ATmega128용 수업 프로젝트와 uC/OS-II 포트·BSP를 준비합니다.
2. `src/lab1.c`를 애플리케이션 소스로 포함합니다. `includes.h`, OS 설정, 시작 코드 및 tick ISR이 필요합니다.
3. 보드 클럭과 `CPU_CLOCK_HZ`, `OS_TICKS_PER_SEC`, 태스크 스택 설정을 확인합니다.
4. 실제 보드 회로와 PORTA(LED), PORTC/PORTG(FND), PB4(버저), PE4/PE5(버튼), TWI 배선을 대조합니다.
5. 해당 보드의 기존 빌드·플래시 절차로 실행한 뒤 센서 읽기, 30°C 경보, 음소거, 최고 온도 표시를 점검합니다.

이 저장소 정리 과정에서는 실물 보드에서 재실행하거나 펌웨어를 새로 빌드하지 않았습니다. 빌드된 ELF와 학번이 있는 보고서 원문, 동영상은 포함하지 않았습니다.

## 알려진 한계 및 개선 방향

- Mailbox/Queue에 데이터 복사본이 아닌 공유 버퍼 포인터를 전달합니다. 동시 접근과 버퍼 수명에 대한 검토가 필요합니다.
- 8비트 MCU에서 공유 16비트 온도 값 접근의 원자성을 검토해야 합니다.
- 최고 온도를 표시하는 동안 FndTask의 경보 판단이 지연될 수 있습니다. 표시와 경보 판단 분리가 개선 방향입니다.
- 음수 온도 변환 및 부호 없는 최고 온도 비교는 영하 조건에서 추가 검증·수정이 필요합니다.
- TWI 대기에 타임아웃이 없어 센서 응답이 없을 때 태스크가 멈출 수 있습니다.

위 사항을 숨기지 않고 원본 교육용 구현의 범위로 기록했습니다. 안전이 중요한 실제 경보 장치로 사용하기 전에는 별도 검증이 필요합니다.
