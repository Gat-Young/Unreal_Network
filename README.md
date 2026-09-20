# Unreal_Network

언리얼 엔진 5.8 데디케이티드 서버 + 웹 레지스트리 서버 예제입니다.

1. **언리얼 서버**가 시작되면 웹서버에 자신의 IP/포트를 자동 등록합니다 (10초마다 하트비트).
2. **클라이언트**가 로그인하면 웹서버에서 등록된 서버 목록을 받아 선택·접속할 수 있고, 접속 후 **채팅**이 가능합니다.

## 구성

| 경로 | 설명 |
| --- | --- |
| `WebServer/registry_server.py` | 레지스트리 웹서버 (파이썬 3 표준 라이브러리만 사용) |
| `Unreal_NetWork/Source/Unreal_NetWork/ServerRegistrySubsystem.*` | 서버 측: 시작 시 웹서버에 서버 등록 + 하트비트 + 종료 시 해제 |
| `Unreal_NetWork/Source/Unreal_NetWork/NetLobbySubsystem.*` | 클라이언트 측: 로그인 UI, 서버 목록 수신, 접속, 채팅 UI (Slate) |
| `Unreal_NetWork/Source/Unreal_NetWork/NetChatAgent.*` | 채팅 RPC 전달용 액터 (플레이어 접속 시 서버가 자동 생성) |
| `Unreal_NetWork/Source/Unreal_NetWork/NetRegistrySettings.*` | 레지스트리 URL/키 설정 (`Config/DefaultGame.ini`) |

### 웹서버 API

| 메서드 | 경로 | 설명 |
| --- | --- | --- |
| POST | `/api/login` | `{username, password}` → `{token}`. 없는 계정은 최초 로그인 시 자동 가입 (PBKDF2 해시 저장) |
| POST | `/api/servers/register` | 서버 등록/하트비트 (`X-Server-Key` 헤더 필요). `ip`를 생략하면 요청 IP로 등록(로컬호스트면 LAN IP) |
| POST | `/api/servers/unregister` | 서버 등록 해제 (`X-Server-Key` 필요) |
| GET | `/api/servers` | 등록된 서버 목록 (`Authorization: Bearer <token>` 필요) |

환경변수: `REGISTRY_HOST`(0.0.0.0), `REGISTRY_PORT`(8080), `REGISTRY_SERVER_KEY`(dev-server-key), `REGISTRY_TTL`(30초).
하트비트가 TTL 동안 없으면 목록에서 자동 제거됩니다.

## 실행 방법

### 1) 빌드
Visual Studio 2022 + UE 5.8이 필요합니다.
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" Unreal_NetWorkEditor Win64 Development -Project="<경로>\Unreal_NetWork\Unreal_NetWork.uproject"
```

### 2) 웹서버
```
python WebServer/registry_server.py
```

### 3) 언리얼 데디케이티드 서버
```
UnrealEditor-Cmd.exe Unreal_NetWork.uproject /Game/DemoTemplate/_Core/Lvl_IntroRoom -server -game -log -ServerName=MyServer
```
시작 후 몇 초 내에 웹서버 로그에 `register <ip>:7777`이 찍힙니다.

### 4) 클라이언트
```
UnrealEditor.exe Unreal_NetWork.uproject -game -windowed
```
1. 로비 화면에서 사용자명/비밀번호 입력 후 **Login** (처음 쓰는 이름이면 자동 가입)
2. 서버 목록에서 서버 버튼을 눌러 접속
3. 접속 후 `Enter`로 채팅 입력창을 열고, 메시지 입력 후 `Enter`로 전송

### 설정 (`Unreal_NetWork/Config/DefaultGame.ini`, `[/Script/Unreal_NetWork.NetRegistrySettings]`)
- `RegistryUrl`, `ServerKey`, `ServerName`, `PublicIp`, `HeartbeatSeconds`
- 명령줄 `-RegistryUrl= -ServerKey= -ServerName= -PublicIp=` 로 덮어쓸 수 있습니다.
- 서버와 웹서버가 다른 PC라면 서버 쪽 `RegistryUrl`을 웹서버 주소로 바꾸고, 클라이언트도 동일하게 맞춥니다. 외부 접속용이면 `-PublicIp=`로 공인 IP를 지정하세요.

## 자동 테스트용 명령줄 (클라이언트)
`-AutoUser=이름 -AutoPass=비밀번호 -AutoJoin -AutoChat=메시지`
로그인 → 서버 목록 수신 → 첫 서버 자동 접속 → 접속 후 채팅 1회 전송까지 UI 조작 없이 수행합니다.
서버 로그에는 `[Chat] 이름: 메시지`, 클라이언트 로그에는 `[ChatRecv] 이름: 메시지`가 출력됩니다.

## 참고
- 기본 서버 키(`dev-server-key`)는 개발용입니다. 배포 시 `REGISTRY_SERVER_KEY`와 `ServerKey`를 변경하세요.
- 웹서버는 평문 HTTP입니다. 외부에 공개할 경우 HTTPS 리버스 프록시를 앞에 두세요.
