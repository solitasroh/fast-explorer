# Migration bundle — temporary

이 디렉터리는 다른 머신에서 fast-explorer 작업을 이어받기 위한 일회성 자료다. **이전 완료 후 삭제할 것** (`git rm -rf docs/handoffs/migration && git commit`).

## 포함된 자료

```
migration/
├── memory/                                  # Claude 프로젝트 메모리 (Claude Code 자동 저장본)
│   ├── MEMORY.md
│   ├── feedback_use_question_tool.md
│   └── feedback_batch_review.md
└── rkit-state/                              # rkit 런타임 상태 (gitignored 정상)
    ├── pdca-status.json
    ├── code-quality-metrics.json
    └── profile.md
```

## 새 머신에서 복원 절차

### 1. 저장소 clone

```powershell
git clone https://github.com/solitasroh/fast-explorer.git
cd fast-explorer
```

### 2. Claude 프로젝트 메모리 복원

Claude 프로젝트 메모리는 OS별로 위치가 다르다.

Windows:
```powershell
$dest = "$env:USERPROFILE\.claude\projects\D--work-private-fast-explorer\memory"
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item docs\handoffs\migration\memory\* $dest -Force
```

macOS / Linux (참고용, 현재 프로젝트는 Windows 전용):
```bash
dest="$HOME/.claude/projects/<프로젝트-경로-슬러그>/memory"
mkdir -p "$dest"
cp docs/handoffs/migration/memory/*.md "$dest/"
```

복원 후 새 Claude 세션에서 첫 prompt 입력 시 메모리가 자동 로드된다. 동작 검증: 분기 질문에 `AskUserQuestion` 툴을 사용하면 OK.

### 3. rkit 런타임 상태 복원 (선택)

기록 보존이 필요하면 복원. 새로 시작해도 무관.

```powershell
New-Item -ItemType Directory -Path .rkit\state, .rkit\instinct -Force | Out-Null
Copy-Item docs\handoffs\migration\rkit-state\pdca-status.json          .rkit\state\
Copy-Item docs\handoffs\migration\rkit-state\code-quality-metrics.json .rkit\state\
Copy-Item docs\handoffs\migration\rkit-state\profile.md                .rkit\instinct\
```

`.rkit/`는 `.gitignore`에 포함되어 있으므로 위 작업이 깃에 영향을 주지 않는다.

### 4. 개발 환경 확인

- Visual Studio 2026 Professional (또는 2022 17.6+) + "Desktop development with C++" workload
- Windows SDK 10.0.22621.0+
- CMake 3.24+ + Ninja

### 5. 빌드 + 테스트

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -property installationPath
& (Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake -S . -B build -G Ninja
cmake --build build --config Release
.\build\core-tests.exe
```

기대 결과: `27 passed, 0 failed`.

### 6. 핸드오프 재개

```
/resume_handoff docs/handoffs/2026-05-14_19-58-49_m1-done-m2-substep-2-complete.md
```

### 7. 이 디렉터리 삭제

이전이 끝났음을 확인했으면:

```powershell
git rm -rf docs/handoffs/migration
git commit -m "chore: remove migration bundle after handoff"
git push
```

## 주의

- 메모리 파일의 `originSessionId` 필드는 이전 머신의 Claude 세션 ID다. 새 머신에서 그대로 두어도 작동하지만 의미는 historical reference에 가깝다.
- `pdca-status.json` 의 절대경로(`lastFile` 등)는 이전 머신 경로 (`D:\work\private\fast-explorer\...`)이다. 새 머신 경로가 다르면 그대로 두거나 수동으로 갱신하면 된다 — rkit는 새 활동이 발생하면 덮어쓴다.
- 이 migration 디렉터리는 작업 컨텍스트 전송 전용이며 production 산출물이 아니다.
