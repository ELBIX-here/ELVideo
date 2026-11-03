# ELVideo

Windows 화면 녹화기. WGC 캡처, D3D11 NV12 변환, Media Foundation 인코딩, Qt/QML UI로 구성합니다.

라이선스: **GPL-3.0-or-later** ([LICENSE](LICENSE)). 동영상 플레이어가 쓰는 libmpv와 FFmpeg가 GPL이고, Qt는 LGPL-3.0입니다.

## 구조

| 경로 | 내용 |
|---|---|
| `core/` | 엔진 정적 라이브러리 (`elv::core`). Qt에 의존하지 않음 |
| `tools/cli/` | 엔진 단독 테스트용 콘솔 앱 (`elvideo-cli`) |
| `app/` | Qt 6 QML 앱 (`ELVideo.exe`) |
| `tests/` | 단위 테스트 (`ELV_BUILD_TESTS=ON`, 아직 없음) |
| `installer/` | Inno Setup 스크립트 (`ELVideo.iss`) |
| `scripts/test/` | 앱 UI 자동 점검(UI Automation, 단축키 입력, 창 스크린샷)과 A/V 싱크 자극·측정 스크립트 |

## 필요 도구

1. **Visual Studio 2022 Build Tools 이상** + *C++를 사용한 데스크톱 개발* 워크로드 (MSVC, Windows 11 SDK, CMake, Ninja, vcpkg 포함)
2. **Qt 6.8 이상** MSVC 64-bit 키트. 계정 없이 설치하려면:
   ```bat
   python -m venv C:\Qt\.aqt
   C:\Qt\.aqt\Scripts\pip install aqtinstall
   C:\Qt\.aqt\Scripts\aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir C:\Qt
   ```

vcpkg는 VS에 포함된 것을 씁니다(`VCPKG_ROOT`가 없을 때). 버전은 `vcpkg-configuration.json`의 baseline으로 고정됩니다.
C++/WinRT는 Windows SDK에 들어 있는 헤더(`Include\<ver>\cppwinrt`)를 그대로 씁니다.

## 빌드

일반 명령 프롬프트에서 실행하면 됩니다. 스크립트가 MSVC 환경을 직접 잡습니다.

```bat
scripts\build.cmd debug
build\debug\tools\cli\elvideo-cli.exe info
```

Qt 없이 엔진과 CLI만 빌드하려면 `debug-engine-only` 프리셋을 씁니다. Qt 경로가 `C:\Qt\6.8.3\msvc2022_64`가 아니면 `QTDIR`을 지정하세요.
앱을 빌드 폴더에서 바로 실행하려면 `%QTDIR%\bin`이 PATH에 있어야 합니다(배포 시에는 windeployqt로 해결).

## 배포 (Phase 6)

```bat
powershell -ExecutionPolicy Bypass -File scripts\package.ps1          :: Release 빌드 + 패키징
powershell -ExecutionPolicy Bypass -File scripts\test\check_deps.ps1  :: 패키지 DLL 의존성 확인
powershell -ExecutionPolicy Bypass -File scripts\test\run_packaged.ps1 :: Qt 없는 PATH로 실행해 화면·녹화 점검
```

`dist\`에 다음이 만들어집니다(버전은 최상위 `CMakeLists.txt`의 `project(VERSION)` 하나에서 나옵니다. 엔진 `version()`, exe 속성의 VERSIONINFO, 설치 파일 이름이 모두 같은 값).

| 파일 | 내용 |
|---|---|
| `ELVideo\` | 실행에 필요한 모든 파일(Qt, QML 플러그인, MSVC 런타임 앱 로컬 복사). 약 54 MB, 208개 |
| `ELVideo-<ver>-portable.zip` | 위 폴더 압축본(약 21 MB) |
| `ELVideo-<ver>-symbols.zip` | `ELVideo.exe` + `ELVideo.pdb`. 크래시 덤프 분석용이며 배포하지 않습니다 |
| `ELVideo-<ver>-setup.exe` | Inno Setup 6이 설치돼 있을 때만 만들어지는 설치 파일 |

- **windeployqt**는 `--qmldir app\qml`로 QML import를 찾아 넣고, 앱이 쓰지 않는 Controls 스타일(Basic 외 전부), QML 디버깅, TLS 플러그인은 스크립트가 지웁니다(1,358개 → 208개 파일).
- **MSVC 런타임**은 vc_redist 설치 대신 exe 옆에 복사합니다.
- **설치 파일**: 기본은 사용자별 설치(관리자 권한 없음, 대화 상자에서 모든 사용자 선택 가능), 시작 메뉴 바로 가기, 바탕 화면 아이콘(선택), 설치 후 실행. 실행 중인 ELVideo가 있으면 닫으라고 묻습니다(`AppMutex`). 제거해도 설정·로그·녹화 파일은 남깁니다. 최소 Windows 10 2004.

### 크래시 덤프

- 앱이 죽으면 `%LOCALAPPDATA%\ELVideo\crashes\ELVideo_<ver>_<날짜-시간>_<pid>.dmp`가 남습니다(최근 10개 유지). 처리하는 경우: 접근 위반 등 SEH 예외(다른 스레드 포함), 스택 오버플로, `std::terminate`, `abort()`, 순수 가상 함수 호출, CRT 잘못된 매개변수. 덤프는 시작 때 미리 만들어 둔 스레드가 쓰므로 스택이 바닥난 경우에도 동작합니다.
- 다음 실행 때 메인 창에 "지난번에 비정상 종료" 알림과 폴더 열기 버튼이 뜹니다.
- 녹화 중에 죽어도 fMP4는 그때까지 재생됩니다(약 5초 녹화 중 크래시 → 4.2초 분량, 오디오 포함, 끊김 0으로 복구 확인).
- 덤프 보기: `elvideo-cli dumpinfo <file.dmp>`(예외 코드, 위치한 모듈+오프셋, 스레드 수). 스택은 symbols.zip의 pdb와 함께 Visual Studio나 WinDbg로 엽니다.
- 테스트: `ELVideo.exe --crash-test <ui|thread|stack|abort|terminate|pure> [초]`. 6가지 모두 덤프 생성과 `dumpinfo` 판독을 확인했습니다.
- `/GS` 버퍼 검사나 `__fastfail` 같은 fail-fast 종료는 프로세스 안에서 잡을 수 없어 Windows 오류 보고(WER)에 맡깁니다.

## CLI

```bat
elvideo-cli info                              :: WGC 지원 여부
elvideo-cli list                              :: 모니터(m0..)와 창(w0..) 목록
elvideo-cli capture m0 10 captures            :: 10초 캡처, 1초마다와 크기 변경 시 PNG 저장
elvideo-cli capture 0x1A2B3C 30 captures      :: HWND로 지정
```

창이 닫히면 `capture`는 바로 종료합니다. 최소화한 동안에는 프레임이 오지 않습니다(WGC 동작).

```bat
elvideo-cli record m0 60 out.mp4 --fps 60 --bitrate 12000   :: 60초 H.264 녹화 (fMP4)
elvideo-cli record w3 30 out.mp4 --software --mp4           :: 소프트웨어 인코더, 일반 MP4
elvideo-cli probe out.mp4 frame.png                         :: 프레임 수·타임스탬프 갭 확인, 중간 프레임 디코드
```

`record`는 초마다 캡처/인코딩/복제/드롭 프레임 수, CPU, 메모리를 출력합니다.

```bat
elvideo-cli record w3 8 out.mp4 --pause-at 3 --resume-at 6   :: 일시정지 구간은 파일에서 빠짐
elvideo-cli selftest w3                                     :: 상태머신·에러 복구 점검 (Debug 빌드는 장애 주입 포함)
elvideo-cli stress m0 100 1                                 :: 시작/일시정지/재개/정지 반복, 메모리·핸들·스레드·GPU 메모리 추적
elvideo-cli leakcheck m0                                    :: 구성 요소별 핸들 증가량(종류별)
elvideo-cli thumb all thumbs                                :: 모든 소스의 썸네일(320x180)과 소요 시간
elvideo-cli audio                                           :: 오디오 출력/입력 장치와 id
elvideo-cli record m0 30 out.mp4 --audio both               :: 시스템 소리 + 마이크 (none|system|mic|both)
elvideo-cli record m0 30 out.mp4 --audio mic --mic-device "{0.0.1.00000000}.{...}"
elvideo-cli synccheck out.mp4                               :: 화면 깜빡임과 비프음의 시간 차(A/V 싱크)
elvideo-cli record m0 10 out.mp4 --region 100,50,1280,720   :: 모니터의 일부 영역만 (물리 픽셀)
elvideo-cli record m0 10 out.mp4 --codec hevc               :: HEVC (자동으로 일반 MP4)
elvideo-cli record m0 10 out.mp4 --preview mid.png          :: 녹화 중간에 미리보기 프레임 저장
elvideo-cli record w3 10 out.mp4 --audio system --app-audio :: 녹화하는 창의 앱 소리만 (Windows 11)
elvideo-cli record m0 10 out.mp4 --webcam test              :: 웹캠 오버레이 (default|test|장치 id)
elvideo-cli record m0 60 out.mp4 --replay 30 --save-at 40,55  :: 리플레이 버퍼, 40초와 55초에 out_replayN.mp4 저장
elvideo-cli webcams                                         :: 카메라 목록과 id
```
`info`는 등록된 비디오 인코더 목록과, 모니터마다 H.264/HEVC/AV1을 실제로 짧게 인코딩해 본 결과를 보여 줍니다.

`record`의 오디오 기본값은 `none`이고, `stress`와 `selftest`는 엔진 기본값(시스템 소리 켬)으로 녹음합니다.
`probe`는 오디오 트랙의 길이(비디오와의 차이), 끊김, 최대 레벨도 보여 줍니다.

A/V 싱크 확인: `scripts\test\sync_run.ps1`이 흰 화면 깜빡임과 1 kHz 비프음을 같은 시계로 내는 창을 띄워 녹화하고 `synccheck`를 돌립니다(이어폰/스피커에서 작은 비프음이 납니다).

## 동영상 플레이어 (libmpv)

- **준비**: `powershell -ExecutionPolicy Bypass -File scripts\get-libmpv.ps1`이 고정된 libmpv 빌드(shinchiro/mpv-winbuild-cmake `20261103`, x86-64 기본판, SHA-256 확인)를 `third_party\mpv`에 받습니다. MSVC용 import 라이브러리는 CMake가 DLL의 export 목록으로 만듭니다(`cmake/Libmpv.cmake`). libmpv가 없으면 플레이어 없이 빌드됩니다.
- **실행**: 메인 창 위쪽의 재생 버튼, 녹화·리플레이 저장 알림의 "재생", 또는 `ELVideo.exe --player <파일>`.
- **구조**: 플레이어는 별도 프로세스입니다. Qt Quick이 그래픽 API를 프로세스 단위로 정하므로, 녹화 앱은 D3D11 그대로 두고 플레이어 프로세스만 OpenGL로 돌려 mpv 렌더 API(`MpvItem`, QQuickFramebufferObject)로 그립니다. 플레이어가 죽어도 녹화에는 영향이 없습니다. 로그는 `elvideo-player.log`.
- **기능**: 끌어다 놓기·파일 열기, 같은 폴더의 동영상 자동 재생목록(PgUp/PgDn), 이어 보기(마지막 위치 기억), 하드웨어 디코딩(D3D11VA, OpenGL 연동이 없으면 copy 방식), 정확한 탐색, 프레임 단위 이동, 배속 0.25~4배(음정 유지), A-B 구간 반복, 오디오·자막 트랙 선택, 외부 자막(같은 이름의 SRT/ASS/SMI 자동, 끌어다 놓기·메뉴로 추가), 자막·오디오 싱크, 볼륨 최대 150%, 현재 장면 저장(캡처 폴더), 항상 위, 전체 화면, 미디어 정보. 컨트롤은 재생 중 2.5초 뒤 숨고, 그동안 자막은 컨트롤 위로 올라갑니다.

| 키 | 동작 | 키 | 동작 |
|---|---|---|---|
| Space / 클릭 | 재생·일시정지 | Enter / 더블클릭 | 전체 화면 |
| ← → | 5초 이동 | Ctrl+← → | 30초 이동 |
| ↑ ↓ / 휠 | 볼륨 | M | 음소거 |
| . F / , D | 다음 / 이전 프레임 | C X Z | 배속 + / − / 원래대로 |
| L | A-B 구간 반복 | S | 현재 장면 저장 |
| < > | 자막 싱크 ∓0.1초 | [ ] | 오디오 싱크 ∓0.05초 |
| I | 미디어 정보 | Ctrl+T | 항상 위 |
| Ctrl+O | 파일 열기 | Home | 처음으로 |

- **시험**: `scripts\test\player_test.ps1`(Debug 빌드). `ELV_PLAYER_IPC`로 mpv의 JSON IPC를 열어(Debug 전용, IPC는 임의 명령을 실행할 수 있음) 15개 항목을 확인합니다: 열기·길이, 트랙(자막 자동 로드), 폴더 재생목록, 하드웨어 디코딩, 정확한 탐색, 프레임 앞/뒤(정확히 1/60초), 배속·음정 유지, A-B 반복, 자막 싱크, 장면 저장, UI 버튼 2개(UI Automation), HEVC 다음 파일, 이어 보기.
- **라이선스**: libmpv(와 그 안의 FFmpeg)는 GPL입니다. 플레이어를 포함한 ELVideo 배포본은 GPL 조건을 따릅니다.

## 화면 캡처

- **시작**: `Ctrl+Shift+F12`, 녹화 바의 카메라 버튼, 트레이 메뉴 "화면 캡처".
- 모든 모니터를 먼저 원본 해상도로 찍어 두고(화면이 멈춘 상태), 그 위에 오버레이를 띄웁니다.
  - **드래그**: 영역
  - **클릭**: 마우스 아래 창. 창은 WGC로 따로 찍으므로 다른 창에 가려진 부분까지 온전히 나옵니다. 바탕 화면을 클릭하면 그 모니터를 찍습니다
  - **툴바**: "이 모니터", "전체 화면"(모든 모니터를 실제 배치대로 이어 붙임), "취소"
  - **Esc / 오른쪽 클릭**: 취소
- 결과는 클립보드 복사와 파일 저장(기본 `사진\ELVideo\ELVideo_Capture_<날짜-시간>.png`, JPG 선택 가능). 저장 후 알림의 "열기 / 폴더에서 보기".
- 설정: 클립보드 복사, 파일로 저장, 저장 폴더, 형식, 마우스 커서 포함, 지연(3/5/10초, 화면 가운데에 카운트다운).
- ELVideo의 창과 오버레이는 캡처에 찍히지 않습니다.
- 시험: `scripts\test\shot_test.ps1`(Debug 빌드). 영역과 창은 마우스 대신 Debug 전용 단축키(`Ctrl+Shift+F1/F2`)로 시험합니다. 사용 중인 PC에서 합성 마우스 입력은 실제 마우스와 섞이기 때문입니다.

## v2·v3 기능

| 기능 | 앱에서 | 동작 방식 |
|---|---|---|
| 영역 녹화 | 소스 선택의 **영역** 탭 → 화면을 드래그 | 모니터를 캡처한 뒤 변환 단계(VideoProcessor)에서 잘라 냅니다. 출력 크기는 영역 크기. 녹화 중에는 영역 둘레에 빨간 테두리가 뜨며 영상에는 찍히지 않습니다 |
| 녹화 중 미리보기 | 녹화 중 메인 창 | 인코더에 들어가는 NV12 프레임을 GPU에서 줄여 초당 약 10장 읽어 옵니다(1~3 ms). 영역 자르기와 웹캠까지 파일 그대로 보입니다. 기본 설정은 녹화 중 메인 창을 숨기므로 트레이에서 열면 보입니다 |
| HEVC / AV1 | 설정 → 비디오 → 코덱 | 시작할 때 각 코덱을 실제로 짧게 인코딩해 보고 쓸 수 없는 코덱은 비활성으로 보여 줍니다(주 모니터 GPU 기준). Media Foundation의 fMP4는 H.264 전용이라 HEVC/AV1은 일반 MP4로 저장됩니다 |
| 앱 소리만 녹음 | 설정 → 오디오 → "창을 녹화할 때 그 앱 소리만" | WASAPI 프로세스 루프백(Windows 11). 녹화하는 창의 프로세스와 자식 프로세스 소리만 담깁니다. 디스플레이·영역 녹화에는 적용되지 않습니다 |
| 웹캠 오버레이 | 설정 → 웹캠 | Media Foundation으로 카메라를 BGRA로 받아 변환 단계의 두 번째 입력 스트림으로 겹칩니다. 위치(네 모서리), 크기(10~50%), 좌우 반전. 화면이 멈춰 있어도 카메라 프레임마다 다시 합성합니다. 카메라는 녹화 중에만 켜집니다 |
| 리플레이 버퍼 | 녹화 바의 리플레이 버튼, 저장 `Ctrl+Shift+F11` | SinkWriter가 fMP4를 메모리 스트림에 쓰고, 최근 N초 분량의 조각만 보관합니다(메모리 일정). 저장할 때는 창 시작 직전 키프레임에서 첫 조각을 잘라 시간값을 0부터로 다시 써서 파일로 냅니다. 파일 이름은 `ELVideo_Replay_<날짜-시간>.mp4` |

검증 결과(이 PC):
- **영역**: 800×450 녹화 정확, 끊김 0. 화면 밖으로 걸친 영역은 잘리고, 완전히 밖이면 오류.
- **HEVC**: Intel QSV 하드웨어로 녹화. NVIDIA 쪽 모니터는 MF가 소프트웨어 HEVC를 고릅니다(1080p60에서 CPU 약 6%). AV1은 두 GPU 모두 사용할 수 없음으로 판정.
- **앱 소리만**: 다른 프로세스의 연속음이 빠짐(무음 92% vs 시스템 소리 1%). 싱크 차이 평균 +15~19 ms.
- **웹캠**: 이 PC의 카메라는 Media Foundation에 잡히지 않아 내장 테스트 패턴(`test`)으로 합성·위치·반전·움직임을 확인했습니다. 실제 카메라 경로는 시험하지 못했습니다. 카메라가 없으면 경고만 띄우고 녹화는 계속됩니다.
- **리플레이**: 10초 창 → 10.7~11.8초, 8초 창 → 9.6초(창 + 다음 키프레임까지 최대 2초). 끊김 0, A/V ±15 ms 이내. 60초 동안 메모리 일정.

## 오디오 (Phase 5)

```
WASAPI 루프백(출력 장치) ─┐                       ┌─▶ AAC 48 kHz 스테레오 ─┐
                          ├─▶ AudioMixer(타임라인) ┤                        ├─▶ 같은 SinkWriter (.mp4)
WASAPI 캡처(마이크) ──────┘   10 ms마다, 100 ms 지연 └─ 비디오 페이서와 같은 시작점·일시정지
```

- **형식**: 소스마다 Windows 공유 모드 엔진에 48 kHz 스테레오 float로 변환을 맡기고(`AUTOCONVERTPCM`), 믹서가 하나의 트랙으로 섞어 16-bit PCM → AAC-LC(96~192 kbps)로 인코딩합니다.
- **싱크**: 오디오 타임라인은 첫 비디오 프레임에서 시작하고, 일시정지 구간을 뺍니다(비디오 페이서와 같은 규칙). 각 패킷은 WASAPI가 주는 QPC 시각에 놓이므로, 아무 소리도 안 날 때 루프백이 패킷을 보내지 않아도 그 구간은 무음으로 채워지고 이후 소리가 제자리에 들어갑니다. 20 ms가 넘는 어긋남은 무음 삽입이나 겹친 부분 잘라내기로 바로잡습니다.
- **측정값**(`sync_run.ps1`): 2분 녹화에서 비프음 60개가 모두 정확히 2.000초 간격(드리프트 0), 화면 깜빡임과의 차이는 −32~+18 ms(자극 창 타이머의 1프레임 흔들림 포함). 일시정지 전후로도 같은 범위. 오디오 트랙 길이는 비디오와 3~20 ms 차이(AAC 프레임 단위).
- **장치**: 장치 id를 비우면 Windows 기본 장치를 따르고, 녹화 중 기본 장치를 바꾸면 1초 안에 새 장치로 옮겨 갑니다. 장치를 열 수 없거나 빠지면 녹화는 계속되고(그 소스만 무음) `onWarning`으로 한 번 알린 뒤 1초마다 다시 시도합니다.
- **앱**: 설정의 오디오 항목에서 시스템 소리/마이크 켜기, 장치, 볼륨(0~200%), 비트레이트를 고릅니다(기본: 시스템 소리만). 녹화 중에는 녹화 바에 소스별 레벨 미터가 나오고, 오디오 경고는 노란 알림으로 뜹니다.

## 앱 (Phase 4)

- **화면 선택**: 디스플레이/창 탭에 썸네일이 나옵니다. 썸네일은 `elv::captureThumbnail()`로 한 프레임만 받아 축소한 것으로, 창이 다시 활성화될 때(3초 간격 이상)와 새로 고침 버튼으로 갱신됩니다. 최소화된 창은 "미리보기 없음"으로 표시됩니다. 카드를 두 번 누르면 바로 녹화합니다.
- **녹화 바**: 녹화/정지, 일시정지, 녹화 시간, 해상도·인코더 정보, 드롭 프레임 수. 끝나면 하단에 저장 알림(재생 / 폴더에서 보기)이 뜹니다.
- **단축키**(어디서나 동작, `RegisterHotKey`): 시작/정지 `Ctrl+Shift+F9`, 일시정지/다시 시작 `Ctrl+Shift+F10`. 설정에서 바꿀 수 있고, 다른 프로그램이 이미 쓰는 조합이면 설정 화면에 빨간 글씨로 알려 줍니다.
- **트레이**: 녹화 중에는 빨간 배지, 일시정지 중에는 노란 배지가 붙습니다. 메뉴에서 열기, 녹화 시작/정지, 일시정지, 녹화 폴더 열기, 종료를 할 수 있습니다. 창을 닫으면 트레이로 들어갑니다(설정에서 끌 수 있음).
- **플로팅 바**: 녹화 중 화면 위쪽 가운데에 시간과 일시정지/정지 버튼이 있는 작은 바가 뜹니다. 왼쪽 점 부분을 끌어 옮길 수 있습니다.
- **녹화에 찍히지 않음**: 메인 창과 플로팅 바는 `WDA_EXCLUDEFROMCAPTURE`로 모든 화면 캡처에서 빠집니다(모니터 전체를 녹화해도 영상에 없음). 기본 설정은 녹화 중 메인 창을 숨기고, 끝나면 다시 보여 줍니다.
- **한 번만 실행**: 이미 실행 중이면 새로 실행해도 기존 창이 앞으로 나옵니다.
- **파일 이름**: `<저장 폴더>\ELVideo_YYYY-MM-DD_HH-mm-ss.mp4`. 기본 저장 폴더는 `동영상\ELVideo`입니다.
- **설정 파일**: `%APPDATA%\ELVideo\settings.json`(바꾸면 바로 저장). **로그**: `%LOCALAPPDATA%\ELVideo\logs\elvideo.log`(엔진 로그와 Qt 경고, 5MB씩 3개).
- **Debug 빌드 테스트 훅**: `ELV_TEST_CAPTURABLE=1`이면 앱 창을 캡처에서 빼지 않습니다(UI 스크린샷용). 앱의 버튼은 UI Automation 이름(접근성 이름)으로도 누를 수 있습니다.

## 엔진 API (Phase 3)

`elv::createRecorder()`가 돌려주는 `IRecorder`([IRecorder.h](core/include/core/IRecorder.h))만 쓰면 됩니다.

- `start/pause/resume/stop`은 **즉시 반환**하고 명령이 받아들여졌는지만 알려 줍니다. 실제 작업과 모든 리스너 콜백은 엔진 스레드 하나에서 순서대로 실행됩니다.
- 상태: `Idle/Error → Starting → Recording ⇄ Paused → Stopping → Idle`. 실패하면 `Error`가 되고, `Error`에서 다시 `start`할 수 있습니다.
- 이벤트([Events.h](core/include/core/Events.h)): `onStateChanged`, `onStarted`(해상도·인코더 정보), `onStats`(초당 약 4회), `onFinished`(파일 확정, 사유 User/SourceClosed/Error), `onError`.
- 일시정지 중에는 프레임을 만들지 않고, 재개하면 타임라인이 끊김 없이 이어집니다(`elapsed`도 일시정지 시간 제외).
- 녹화 중 창이 닫히면 자동으로 멈추고 파일을 확정합니다(`SourceClosed`). 인코더나 GPU가 실패하면(예: 디바이스 제거) 그때까지의 파일을 확정하고 `Error`로 갑니다.
- 첫 프레임 전에 멈추면 빈 파일을 지우고 `onFinished`는 보내지 않습니다.
- 녹화기를 파괴하면 진행 중인 녹화를 멈추고 파일을 확정합니다.
- `captureThumbnail(source, maxW, maxH)`: 소스의 한 프레임을 커서 없이 축소해 돌려줍니다(보통 40~200ms). UI 스레드가 아닌 곳에서, 여러 개를 동시에 불러도 됩니다.

## 녹화 파이프라인 (Phase 2)

```
WGC 프레임(BGRA) ─▶ VideoConverter ─▶ 최신 NV12 텍스처 ─▶ FramePacer(CFR 틱) ─▶ MediaWriter ─▶ .mp4
   캡처 스레드        (VideoProcessor,                        페이서 스레드           (SinkWriter)
                       BT.709 limited, 레터박스)
```

- **CFR**: 페이서가 `N / fps` 시각마다 최신 프레임을 인코더에 넘기므로 타임스탬프는 정확히 `N / fps`입니다. 새 프레임이 없으면 복제하고, 늦어진 틱은 건너뛰어 타임라인이 밀리지 않게 합니다.
- **출력 크기**는 녹화 시작 시 소스 크기(짝수로 내림)로 고정됩니다. 도중에 창 크기가 바뀌면 비율을 유지한 채 레터박스로 들어갑니다.
- **인코더**: 하드웨어 MFT에는 GPU 표면을 그대로 넘깁니다. 하드웨어 인코더가 없거나 `--software`일 때는 NV12를 CPU로 읽어 와서 소프트웨어 인코더에 넘깁니다(GPU 표면을 소프트웨어 인코더에 넘기면 내부 동기화 때문에 프레임이 밀립니다).
- **GPU가 여러 개인 시스템**: 캡처 대상 모니터를 구동하는 어댑터에서 디바이스와 인코더를 만듭니다(예: 모니터 1은 Intel QSV, 모니터 2는 NVIDIA).

## 아이콘과 스플래시

- 원본은 [app/resources/elvideo.svg](app/resources/elvideo.svg)입니다. 바꾼 뒤에는 `.ico`를 다시 생성해 커밋합니다.
  ```bat
  set QT_QPA_PLATFORM=offscreen
  build\debug\tools\icongen\elvideo-icongen.exe app\resources\elvideo.svg app\resources\elvideo.ico
  ```
  `.ico`에는 16~256px 9개 크기가 들어 있고 `elvideo.rc`(IDI_ICON1)로 exe에 포함됩니다. 창 제목 표시줄 아이콘도 이것을 씁니다.
- 앱 안의 로고는 [LogoMark.qml](app/qml/LogoMark.qml)에서 SVG와 같은 좌표로 그립니다. SVG를 고치면 이 파일도 맞춰야 합니다.
- [Splash.qml](app/qml/Splash.qml): 위 릴에서 아래 릴로 필름이 감기며 돌다가 원본 아이콘 자세로 멈추고, ELVIDEO가 페이드 인/아웃한 뒤 메인 창이 뜹니다(약 3초, 클릭하면 건너뜀).

## 알려진 문제

- **Intel Quick Sync + Media Foundation SinkWriter 핸들 누수**: Intel QSV 하드웨어 인코더로 녹화할 때마다 핸들 3개(Event, 레지스트리 Key, WaitCompletionPacket)가 남습니다. 엔진 코드 없이 MF SinkWriter만 만들었다 해제해도 재현되고(`leakcheck`의 `bare MF SinkWriter`), NVIDIA 인코더나 소프트웨어 인코더에서는 생기지 않습니다. 녹화 1,000번에 핸들 3,000개 수준이라 실사용에는 지장이 없습니다. 메모리·스레드·GPU 메모리는 늘지 않습니다(100회 반복에서 확인). 없애려면 SinkWriter 대신 인코더 MFT를 직접 구동해야 해서 보류했습니다.
- **NVIDIA GPU의 HEVC**: Media Foundation이 NVIDIA/DX12 HEVC 하드웨어 인코더 대신 소프트웨어 HEVC(HEVCVideoExtension)를 고릅니다. 녹화는 되지만 CPU를 씁니다.
- **실제 웹캠 미검증**: 카메라 경로(MJPEG/YUY2 → RGB32 변환 포함)는 이 PC에서 시험하지 못했습니다.
- **플레이어 설치 크기**: `libmpv-2.dll`이 FFmpeg를 통째로 포함해 약 116 MB입니다(설치 파일 압축 후 약 30 MB 증가).
- **리플레이 버퍼는 H.264만**: 조각난 MP4로 버퍼를 만드는데 MF가 HEVC/AV1을 fMP4로 쓰지 못합니다.
- **오디오 장치 클럭 차이**: 장치의 실제 샘플 속도는 QPC와 아주 조금 다를 수 있습니다. 차이가 20 ms 쌓이면 그 자리에서 무음을 넣거나 잘라 맞추므로, 몇 분에 한 번 아주 짧은 끊김이 생길 수 있습니다(이 PC의 2분 녹화에서는 보정이 한 번도 일어나지 않음). 리샘플링으로 부드럽게 맞추는 것은 이후 과제입니다.
- **캡처 아이템**: `GraphicsCaptureItem`을 만들 때마다 OS가 ALPC 포트를 하나 열고 프로세스가 끝날 때까지 유지합니다. 그래서 아이템을 소스별로 캐시해 재사용하고(창이 닫히거나 모니터가 빠지면 캐시에서 제거), 같은 소스를 반복 녹화해도 늘지 않습니다.

## 계획서 대비 조정 사항

- **MKV 출력**: Media Foundation에는 MKV 싱크가 없습니다. v1은 MF로 **fMP4/MP4**를 쓰고, MKV는 FFmpeg 기반 `MediaWriter` 구현을 추가할 때 지원합니다. `Container::Mkv`는 그 자리만 잡아 둔 값입니다.
- **fMP4**: SinkWriter에서 `MF_TRANSCODE_CONTAINERTYPE_FMPEG4`를 지정하면 녹화 중 크래시가 나도 그때까지의 파일이 재생 가능합니다. 그래서 기본값으로 잡았습니다.
- **Qt는 vcpkg가 아니라 Qt 설치 관리자로** 받습니다. vcpkg의 Qt 빌드는 시간이 매우 오래 걸리고, windeployqt 배포 흐름과도 공식 설치본이 더 잘 맞습니다.
- **창 크기 변경/복원**: 크기가 바뀐 첫 프레임은 이전 크기 버퍼에 그려진 것이어서 버리고, 대상 창에 다시 그리기를 요청합니다(정적인 창은 요청하지 않으면 새 프레임을 보내지 않습니다). 복원 직후 WGC가 보내는 제목 표시줄 크기의 프레임도 같은 방식으로 걸러집니다.
- **WDA_EXCLUDEFROMCAPTURE**는 Windows 10 2004(19041) 이상에서만 동작합니다. 그보다 낮으면 `WDA_MONITOR`로 폴백하는데, 이때 바는 검게 녹화됩니다.
