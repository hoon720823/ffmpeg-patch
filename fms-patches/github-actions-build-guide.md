# GitHub Actions 로 ffmpeg-patch 자동 빌드 — 처음 셋업 가이드

> **목적**: 우리 ffmpeg-patch (n8.1.1 + ZMQ encoder bridge patch) 를 4 platform
> (Windows x64/arm64, Linux x64/arm64) 으로 자동 빌드하는 GitHub Actions
> 파이프라인 셋업.
>
> **누구를 위해**: GitHub fork / Actions / PAT / Secrets 처음 다루는 사람.
> 30분~1시간 내에 첫 자동 빌드까지.

---

## 0. 큰 그림 (왜 이걸 하는가)

ffmpeg 를 4 OS × 2 architecture (= 4 platform) 으로 직접 빌드하려면 각 환경마다
의존성 (libx264 / libzmq / libsrt / NVENC SDK 등) 을 셋업해야 하고 시간이 매우
오래 걸립니다.

**BtbN** 이라는 GitHub 사용자가 그 작업을 자동화한 빌드 시스템
(`BtbN/FFmpeg-Builds`) 을 공개해뒀습니다. 우리는 그 빌드 시스템을 본인 GitHub
계정으로 **복사 (= fork)** 한 뒤, 빌드 대상 ffmpeg 소스만 우리 fork
(`hoon720823/ffmpeg-patch`) 로 바꿉니다.

```
hoon720823/ffmpeg-patch       <- 우리가 만든 ffmpeg fork (patched)
                ↑ git clone (PAT 인증)
hoon720823/FFmpeg-Builds      <- BtbN/FFmpeg-Builds 의 fork (빌드 시스템)
                │
                └─ GitHub Actions 자동 실행
                       │
                       └─ 4 platform .zip / .tar.xz artifact 생성
```

Push 1번 → 1시간 후 4개 platform 빌드 완료 → 본인 GitHub 의 Releases / Artifacts
에 zip 파일 자동 업로드.

---

## 1. 사전 준비

이미 되어 있는 것 (확인만):

- [x] GitHub 개인 계정 (`hoon720823`)
- [x] `https://github.com/hoon720823/ffmpeg-patch` repo (private). master branch
      에 우리 patch 적용된 ffmpeg n8.1.1 소스가 push 되어 있음.

이제 할 것 (5단계):

| 단계 | 어디서 | 작업 | 예상 시간 |
|---|---|---|---|
| 1 | github.com (UI) | BtbN/FFmpeg-Builds 를 본인 계정으로 fork | 30초 |
| 2 | github.com (UI) | PAT (Personal Access Token) 생성 | 2분 |
| 3 | github.com (UI) | fork repo 에 PAT 를 secret 으로 등록 | 1분 |
| 4 | github.com (UI) | fork 의 build.yml 통째 교체 | 2분 |
| 5 | github.com (UI) | Commit → 자동 빌드 trigger | 1분 |
| 검증 | github.com (UI) | Actions 탭에서 진행 + Artifacts 다운로드 | 빌드 1시간 대기 |

**총 셋업 (대기 제외) 약 6~7분**. 빌드는 백그라운드.

---

## 2. 단계 1: BtbN/FFmpeg-Builds fork

### 무엇

BtbN 이라는 사람이 만든 빌드 시스템 repo 를 본인 GitHub 계정으로 복사합니다.
fork 는 GitHub 의 핵심 기능 — 다른 사람의 repo 를 내 계정으로 통째 복사해서
내 변경을 추가할 수 있게 하는 것.

### 어떻게

1. 브라우저로 https://github.com/BtbN/FFmpeg-Builds 접속
2. 우상단 **Fork** 버튼 클릭 (별⭐ 모양 옆)
3. "Create a new fork" 화면이 뜸:
   - Owner: `hoon720823` (본인)
   - Repository name: `FFmpeg-Builds` (그대로)
   - "Copy the **default branch** only" 체크 (그대로)
4. 초록 **Create fork** 버튼 클릭

### 결과

`https://github.com/hoon720823/FFmpeg-Builds` 가 생김. BtbN 의 모든 파일
(빌드 스크립트, GitHub Actions 워크플로우 등) 이 본인 계정 안에 복사됨.

> ✅ 이미 완료하셨다면 다음 단계.

---

## 3. 단계 2: PAT (Personal Access Token) 생성

### 무엇

GitHub Actions 가 우리 **private** repo (`ffmpeg-patch`) 에서 소스를 받으려면
인증이 필요합니다. 비밀번호 대신 사용하는 안전한 token 을 만듭니다.

### 어떻게

1. https://github.com/settings/tokens?type=beta 접속 (Fine-grained tokens 페이지
   직접 이동)
2. **Generate new token** 버튼 클릭
3. 폼 채우기:
   - **Token name**: `ffmpeg-patch-read` (편한 이름이면 OK)
   - **Expiration**: `90 days` (보안상 권장. "No expiration" 도 가능하지만
     운영에서는 짧은 기간 권장)
   - **Repository access**: **Only select repositories** 선택 → 검색창에
     `ffmpeg-patch` 입력 → **`hoon720823/ffmpeg-patch`** 항목 클릭해서 추가
   - **Permissions** 섹션 → **Repository permissions** 펼치기 → 아래로 스크롤 →
     **Contents** 항목 → 드롭다운에서 **Read-only** 선택
   - 다른 권한은 **No access** 그대로
4. 화면 하단 **Generate token** 버튼 클릭
5. 다음 화면에 token 이 한 번만 보임 (시작이 `github_pat_...`):
   - 이걸 **즉시 복사** (창 닫으면 다시 못 봄)
   - 메모장에 임시 보관 또는 바로 다음 단계로

### ⚠️ 보안 주의

- token 을 **누구에게도 보여주지 마세요** (assistant 포함). 캡처/이미지에 일부
  라도 노출되면 GitHub 의 secret scanner 가 자동으로 revoke 합니다.
- secret 등록 끝나면 메모장의 token 도 삭제하세요.
- 의심되면 https://github.com/settings/tokens?type=beta 의 token 옆 "..." →
  **Revoke** 후 새로 발급 + secret 갱신.

### 결과

token 1개 발급됨 (`github_pat_11...`). 메모장에 임시 보관.

> ✅ 이미 완료하셨다면 다음 단계.

---

## 4. 단계 3: fork 에 PAT secret 등록

### 무엇

방금 만든 token 을 fork repo (`FFmpeg-Builds`) 의 안전한 저장소 (Secret) 에
등록합니다. GitHub Actions 워크플로우가 이 secret 을 읽어 우리 ffmpeg-patch
repo 에 인증합니다.

### 어떻게

1. https://github.com/hoon720823/FFmpeg-Builds/settings/secrets/actions 접속
   (fork 의 Secrets 페이지 직접 이동)
2. **New repository secret** (초록 버튼) 클릭
3. 폼 채우기:
   - **Name**: `FFMPEG_PAT` (정확히 이 이름. 우리 build.yml 이 이 이름으로 참조)
   - **Secret**: 단계 2 에서 복사한 token 값 통째로 (`github_pat_11...` 시작
     부분 포함 전체)
4. **Add secret** 버튼 클릭

### 결과

화면에 `FFMPEG_PAT` 가 보임. 값은 한 번 등록 후 보이지 않음 (Update / Remove 만
가능). GitHub Actions 만 이 값을 읽을 수 있음.

> ✅ 이미 완료하셨다면 다음 단계.

---

## 5. 단계 4: fork 의 build.yml 교체

### 무엇

BtbN 의 원본 build.yml 은 모든 ffmpeg 변형 (gpl/lgpl/static/shared × 4 platform
× 옛 버전들 = 수십 빌드) 을 합니다. 우리는:

- 우리 fork 만 받게 (`FFMPEG_REPO` env 추가)
- gpl-shared 8.1 변형 + 4 platform 만 빌드 (matrix 단순화)

이걸 한 번에 교체합니다.

### 어떻게

1. https://github.com/hoon720823/FFmpeg-Builds/edit/master/.github/workflows/build.yml
   접속 (편집 모드 직접 이동)
2. 화면에 BtbN 의 원본 build.yml 이 텍스트 에디터로 열림
3. **`Ctrl+A`** (전체 선택) → **`Delete`** (전체 삭제)
4. 우리 파일 (`D:\GoLand\workspace\ffmpeg-patch\fms-patches\FFmpeg-Builds-fork-build.yml`)
   을 메모장 등으로 열기 → **`Ctrl+A`** → **`Ctrl+C`**
5. 빈 GitHub 에디터로 돌아와서 **`Ctrl+V`** (붙여넣기)
6. 화면 하단의 commit 영역:
   - 메시지 (위 칸): `Use ffmpeg-patch fork (master) and limit matrix` 또는
     적당히
   - 설명 (아래 칸): 비워둠
   - **Commit directly to the `master` branch** 라디오 버튼 선택
7. 초록 **Commit changes** 버튼 클릭

### 결과

build.yml 이 새 내용으로 교체됨. push 즉시 GitHub Actions 자동 trigger.

---

## 6. 단계 5: 자동 trigger 확인

### 어떻게

push 직후 https://github.com/hoon720823/FFmpeg-Builds/actions 접속.

화면에:
- 상단에 **"Build FFmpeg"** 워크플로우 진행 중 (노란색 동그라미 = 진행, 초록 =
  성공, 빨강 = 실패)
- 클릭하면 jobs 목록 보임:
  - `Pre Checks` (몇 초)
  - `Build base image` (몇 분)
  - `Build target base image` × 4 platform (몇 분 ~ 10분)
  - `Build target-variant image` × 4 platform (10~30분)
  - `Build ffmpeg` × 4 platform (10~30분)
  - `Publish release` (몇 초, schedule trigger 시에만)

### 시간

- **첫 빌드**: 1~1.5시간 (docker layer cache miss, 의존성 모두 처음 빌드)
- **두번째 이후**: 30~45분 (cache hit, 변경 부분만 재빌드)

### 모든 job 초록색 ✅ 이면 빌드 성공.

---

## 7. 빌드 결과 (Artifact) 다운로드

### 어떻게

1. https://github.com/hoon720823/FFmpeg-Builds/actions 의 빌드 클릭
2. 페이지 하단 **Artifacts** 섹션:
   - `ffmpeg-win64-gpl-shared-8.1` (Windows x64)
   - `ffmpeg-winarm64-gpl-shared-8.1` (Windows arm64)
   - `ffmpeg-linux64-gpl-shared-8.1` (Linux x64)
   - `ffmpeg-linuxarm64-gpl-shared-8.1` (Linux arm64)
3. 각 artifact 클릭하면 zip 다운로드 (브라우저)
4. 압축 풀면:
   - Windows = `ffmpeg.exe`, `ffprobe.exe`, `*.dll` (lib)
   - Linux = `ffmpeg`, `ffprobe`, `*.so` (lib)

### 검증

다운로드한 ffmpeg 실행:
```bash
ffmpeg -version | head -3
ffmpeg -version | tr ' ' '\n' | grep -E '\-\-enable\-(libzmq|libx264|ffnvcodec)'
```

세 옵션 모두 보이면 OK.

ZMQ patch 적용 확인 — patch 자체 동작은 다른 단계 (PoC 시나리오) 에서 검증.

---

## 8. 자주 발생하는 문제

### "PAT secret 인증 실패"

증상: `Build ffmpeg` job 의 ffmpeg 다운로드 단계에서 `Authentication failed` 또는
`Repository not found`.

원인:
- secret 이름이 build.yml 의 `secrets.FFMPEG_PAT` 와 안 맞음
- token 권한 부족 (Contents: Read-only 가 아님)
- token expired

해결: 단계 2~3 다시 (이름 정확히 `FFMPEG_PAT`, 권한 Contents: Read-only,
expiration 확인).

### "build.yml 들여쓰기 에러"

증상: Actions 페이지에서 워크플로우 자체가 안 돌고 빨간 X 만 보임. 클릭하면 yaml
parse error.

원인: 복사/붙여넣기 시 들여쓰기 깨짐.

해결: 단계 4 다시. 우리 파일 (`FFmpeg-Builds-fork-build.yml`) 통째로 다시 복사.

### "특정 platform 만 실패"

증상: 4 platform 중 1~2개만 실패 (예: winarm64 만 빨강).

원인: 의존성 빌드 일시적 실패 (네트워크 / docker registry rate limit 등).

해결: Actions 페이지에서 실패한 빌드 → **Re-run failed jobs** 버튼.

### "Schedule trigger 시 pre_check fail"

증상: cron 시간 (매일 12 UTC) 에 자동 실행되지만 `Pre Checks` job 에서 fail.

원인: 우리는 BtbN 본인이 아니라 fork 라 BtbN/FFmpeg-Builds 가 아닌 repo 에서는
schedule 막혀있음 (의도된 안전장치).

해결: schedule trigger 무시. push 시에만 빌드되면 OK. 또는 build.yml 의
`schedule:` 블록 제거하거나 cron 시간을 다른 값으로 바꾸고 안전장치 if 도 같이
수정 (선택).

---

## 9. 두번째 이후 빌드 (patch 갱신 시)

ffmpeg-patch 에 새 patch 추가/수정한 후 새 빌드 받기:

1. local 에서 새 commit + push (`hoon720823/ffmpeg-patch` master 로):
   ```bash
   cd /d/GoLand/workspace/ffmpeg-patch
   # 코드 수정
   git add ...
   git commit -m "..."
   git push origin master
   ```
2. fork 의 GitHub Actions 자동 trigger... **안 됨**. fork 는 자기 build.yml
   변경 시에만 push trigger. ffmpeg-patch 의 push 와 무관.

   해결 두 가지:
   - **(a)** fork repo 에 작은 commit (예: README 한 글자 변경 + push).
     이게 trigger.
   - **(b)** Actions 페이지에서 manual trigger:
     1. https://github.com/hoon720823/FFmpeg-Builds/actions/workflows/build.yml
        접속
     2. 우상단 **Run workflow** 버튼 클릭
     3. branch 그대로 (master) → **Run workflow**
3. 1시간 대기 → 새 artifact 받기.

---

## 10. 전체 흐름 한눈 정리

```
[setup 1회만]
1. BtbN/FFmpeg-Builds  ──fork──▶  hoon720823/FFmpeg-Builds
2. GitHub Settings  ──PAT 생성──▶  github_pat_11...
3. fork settings/secrets  ──FFMPEG_PAT 등록──▶  ✅
4. fork build.yml 교체  ──Commit──▶  자동 trigger
5. Actions 탭  ──1시간 대기──▶  Artifacts (4 zip)

[빌드 갱신 시 반복]
local ffmpeg-patch 수정  ──push master──▶  GitHub
fork build.yml 작은 commit (또는 Run workflow 버튼)  ──▶  자동 trigger
1시간 대기  ──▶  새 Artifacts
```

---

## 11. 추가 참고

- `D:\GoLand\workspace\ffmpeg-patch\fms-patches\FFmpeg-Builds-fork-build.yml`
  — 단계 4 에서 붙여넣을 build.yml 본문
- `D:\GoLand\workspace\ffmpeg-patch\fms-patches\README.md`
  — patch 의 design / 흐름 설명
- BtbN 원본: https://github.com/BtbN/FFmpeg-Builds
- ffmpeg n8.1.1: https://git.ffmpeg.org/gitweb/ffmpeg.git/tag/n8.1.1
- GitHub Fine-grained PAT 가이드:
  https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens

막히는 부분 있으면 단계 번호 + error message 와 함께 문의.
