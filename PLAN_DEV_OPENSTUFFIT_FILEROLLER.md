# PLAN: libopenstuffit 기반 File Roller 플러그인 개발 (실행용)

작성일: 2026-04-23  
대상 저장소: `/mnt/USERS/onion/DATA_ORIGN/Workspace/openstuffit`

## 1. 목적과 원칙

이 문서는 `openstuffit` 코어 기능 보강 문서가 아니다.  
이미 존재하는 `libopenstuffit`를 재사용해 **별도 실사용 산출물(File Roller 연동)** 을 만드는 실행 계획이다.

최종 목표:

1. `openstuffit-fr-bridge` 실행 파일 구현 (`libopenstuffit` 소비자)
2. File Roller backend(`fr-command-openstuffit`) 구현
3. Linux 배포 가능한 패치/검증 산출물 생성


## 2. 현재 기준선(Baseline)

현재 확인된 상태:

- `openstuffit`는 CLI + shared/static library 구조로 분리되어 있음
- public API: `include/openstuffit/openstuffit.h`
- File Roller 소스 존재: `reference_repos/file-roller`
- File Roller는 현재 StuffIt에 대해 `fr-command-unstuff` + `fr-command-unarchiver` 사용
- `application/x-stuffit` MIME가 이미 등록되어 있음 (`fr-init.c`)

즉, 신규 개발은 "기존 엔진 교체"가 아니라 **새 backend 추가 + 우선순위 조정** 방식으로 진행한다.

로컬 호환성 기준선(추가):

- `reference_repos/file-roller-local` (tag `3.40.0`, gtk3 기반)
- 로컬 환경 `gtk+-3.0 3.24.33`에서 빌드 가능
- `reference_repos/file-roller` (44.6, gtk4>=4.8.1)는 별도 상위 트랙으로 유지


## 3. 범위

포함:

- Linux 환경
- `.sit`, `.sea`, `.sea.bin` (File Roller MIME 경로상 `application/x-stuffit`)
- 목록 조회(list) + 추출(extract, 전체/선택)
- 비밀번호/포맷오류/미지원 오류 전달
- Unicode normalization 옵션 연동 (`none|nfc|nfd`)

제외:

- `.sitx` 실제 해제 지원 (미지원 유지, 오류 매핑만)
- File Roller 전체 아키텍처 변경
- Windows `.exe` SFX 경로


## 4. 개발 산출물(파일 단위)

### 4.1 openstuffit 저장소

신규:

- `src/bridge/openstuffit-fr-bridge.c`
- `src/bridge/openstuffit_fr_bridge_json.c`
- `src/bridge/openstuffit_fr_bridge_json.h`
- `tests/test_fr_bridge.sh`
- `tests/expected/fr_bridge_list_stuffit45.json`
- `tests/expected/fr_bridge_identify_sea_bin.json`

수정:

- `Makefile` (`build/openstuffit-fr-bridge`, `test-fr-bridge` 타깃 추가)
- `README.md` (bridge 사용법)
- `document/API_OPENSTUFFIT.md` (bridge CLI 계약)

### 4.2 file-roller 저장소 패치 (`reference_repos/file-roller`, 44.6 트랙)

신규:

- `src/fr-command-openstuffit.c`
- `src/fr-command-openstuffit.h`

수정:

- `src/meson.build` (source/header 등록)
- `src/fr-init.c` (register 순서: openstuffit 우선)
- `data/packages.match` (`openstuffit=` 추가)

산출 패치 저장 경로:

- `package/linux/patches/file-roller/0001-openstuffit-backend.patch`

### 4.3 file-roller-local 저장소 패치 (`reference_repos/file-roller-local`, 로컬 호환 트랙)

신규:

- `src/fr-command-openstuffit.c`
- `src/fr-command-openstuffit.h`

수정:

- `src/meson.build`
- `src/fr-init.c`
- `data/packages.match`

산출 패치 저장 경로:

- `package/linux/patches/file-roller-local/0001-openstuffit-backend-3.40.0.patch`


## 5. Bridge CLI 고정 계약

명령:

```text
openstuffit-fr-bridge identify --json <archive>
openstuffit-fr-bridge list --json <archive> [--password <text>] [--unicode-normalization none|nfc|nfd]
openstuffit-fr-bridge extract --output-dir <dir> <archive> [--password <text>] [--overwrite|--skip-existing|--rename-existing] [--forks skip|rsrc|appledouble|both|native] [--finder skip|sidecar] [--unicode-normalization none|nfc|nfd] [--entry <path>]...
```

exit code:

- `0`: success
- `1`: io/internal
- `2`: unsupported
- `3`: bad-format/checksum
- `4`: password-required/password-bad
- `5`: usage

stdout/stderr 정책:

- 정상: stdout에 JSON 1개(개행 포함)
- 실패: stderr 1줄 + exit code

### 5.1 `list --json` 최소 스키마

```json
{
  "status": "ok",
  "wrapper": "raw|macbinary|applesingle|appledouble|binhex",
  "format": "sit-classic|sit5|unknown",
  "entries": [
    {
      "index": 0,
      "path": "testfile.txt",
      "is_dir": false,
      "size": 12,
      "mtime_unix": 1675463846,
      "encrypted": false,
      "data_method": 0,
      "resource_method": 13
    }
  ]
}
```

시간 변환 규칙:

- `mtime_unix = modify_time_mac - 2082844800`
- 값이 epoch 이전이거나 변환 불가면 `0`


## 6. File Roller backend 설계

### 6.1 구현 방식

`fr-command-unarchiver.c`의 패턴을 따른다.

- `list`: bridge JSON 출력 파싱 후 `FrFileData` 채움
- `extract`: bridge 실행 결과(exit/status)만 사용
- `handle_error`: exit code 기반 `FrError` 변환

### 6.2 bridge 경로 해석 우선순위

1. 환경변수 `OPENSTUFFIT_FR_BRIDGE`
2. 고정 경로 `/usr/bin/openstuffit-fr-bridge`
3. `PATH` 검색 (`openstuffit-fr-bridge`)

### 6.3 오류 매핑

- rc `4` -> `FR_ERROR_ASK_PASSWORD`
- rc `2` -> `FR_ERROR_UNSUPPORTED_FORMAT`
- rc `3` -> `FR_ERROR_COMMAND_ERROR`
- rc `1/5/기타` -> `FR_ERROR_COMMAND_ERROR`

### 6.4 MIME/우선순위 정책

- MIME: `application/x-stuffit`
- 등록 순서에서 `fr_command_openstuffit_get_type()`를 `unstuff`, `unarchiver`보다 먼저 등록
- bridge가 없거나 실행 실패하면 기존 backend로 자동 폴백


## 7. 구현 순서 (실제 작업 절차)

## Step 0. 베이스라인 확인

```sh
make all
make test
```

통과 기준:

- 기존 테스트 회귀 없음

## Step 1. bridge MVP 구현

핵심 API 사용:

- `ost_archive_handle_open_file`
- `ost_archive_handle_entry_count`
- `ost_archive_handle_entry`
- `ost_archive_handle_extract`

스모크:

```sh
make all
build/openstuffit-fr-bridge identify --json reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
build/openstuffit-fr-bridge list --json reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
build/openstuffit-fr-bridge extract --overwrite --output-dir /tmp/openstuffit_fr_bridge_smoke reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
```

## Step 2. bridge 테스트 추가

```sh
make test-fr-bridge
```

검증 항목:

- `.sit` list/extract rc=0
- `.sea.bin` identify/list rc=0
- password sample rc=4 (without password)
- `.sitx` rc=2
- JSON golden 비교

## Step 3. File Roller backend 구현 (로컬 호환 우선)

```sh
cd reference_repos/file-roller-local
meson setup _build --prefix=/usr -Dnautilus-actions=disabled -Dpackagekit=false
ninja -C _build
```

실행:

```sh
OPENSTUFFIT_FR_BRIDGE=/mnt/USERS/onion/DATA_ORIGN/Workspace/openstuffit/build/openstuffit-fr-bridge \
meson devenv -C _build file-roller
```

## Step 4. 통합 검증 및 패치 산출

```sh
cd /mnt/USERS/onion/DATA_ORIGN/Workspace/openstuffit/reference_repos/file-roller-local
git diff > /mnt/USERS/onion/DATA_ORIGN/Workspace/openstuffit/package/linux/patches/file-roller-local/0001-openstuffit-backend-3.40.0.patch
```


## 8. 테스트 계획 (개발 착수용 상세)

테스트 fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit`
- `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin`
- `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit`
- `reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx`
- (선택) `.hqx` 샘플 1건

### 8.1 Bridge 자동 테스트

신규 타깃:

- `make test-fr-bridge`

검증 포인트:

1. identify/list JSON key 검사 (`format`, `wrapper`, `entries`)
2. `mtime_unix` 값 존재 및 정수형
3. extract 결과 파일 존재 + 내용 비교
4. 비밀번호 요구/오류 rc=4
5. unsupported rc=2

### 8.2 File Roller 통합 테스트

수동 시나리오:

1. `.sit` 오픈 -> 목록 표시
2. `.sea.bin` 오픈 -> 목록 표시
3. password `.sit` 오픈 -> 비밀번호 요청 다이얼로그
4. `.sitx` 오픈 -> 미지원 오류

자동/반자동:

- `ninja -C _build test`
- bridge backend stderr/stdout 로그 확인

### 8.3 릴리즈 전 게이트

```sh
make test
make distcheck
cd reference_repos/file-roller-local && ninja -C _build test
```

모두 통과 시 배포 후보로 판단한다.


## 9. Linux 의존성 메모 (Ubuntu/Debian 예시)

openstuffit:

- `build-essential`
- `zlib1g-dev`

file-roller 개발 빌드:

- `meson`
- `ninja-build`
- `pkg-config`
- `libglib2.0-dev`
- `libgtk-4-dev`
- `libadwaita-1-dev`
- `libjson-glib-dev`

로컬 호환 트랙(`file-roller-local` 3.40.0) 추가 의존성:

- `libgtk-3-dev`
- (`nautilus-actions` 비활성 시) `libnautilus-extension` 미설치 가능


## 10. 리스크와 대응

1. File Roller 내부 API 변동  
대응: 현재 로컬 clone 기준으로 패치 작성, 추후 버전은 rebase 검증

2. 선택 추출 경로 매핑 오류  
대응: `from_file`/`file_list`를 `--entry`로 전달하고 bridge/CLI 테스트로 회귀 방지

3. 경로/환경 차이로 bridge 탐색 실패  
대응: `OPENSTUFFIT_FR_BRIDGE` 우선 사용

4. JSON 계약 변경으로 파서 깨짐  
대응: `tests/expected/*.json` golden 고정


## 11. 완료 기준 (DoD)

아래 항목을 모두 만족하면 완료:

1. `build/openstuffit-fr-bridge` 빌드 및 동작
2. File Roller(local track)가 `application/x-stuffit`에서 openstuffit backend를 우선 사용
3. `.sit`/`.sea.bin` 목록+전체/선택 추출 성공
4. 비밀번호/미지원/포맷 오류가 의도한 `FrError`로 표시
5. 패치 파일 생성: `package/linux/patches/file-roller-local/0001-openstuffit-backend-3.40.0.patch`
6. `make test`, `make distcheck`, `ninja -C _build test` 통과


## 12. 즉시 시작 체크리스트 (진행 상태)

- [x] Step 0: `make all && make test`
- [x] Step 1: bridge 소스 + Makefile 타깃 추가
- [x] Step 2: `tests/test_fr_bridge.sh` + golden 추가
- [x] Step 3: file-roller backend 소스 추가
- [x] Step 4: `file-roller-local (3.40.0)` 빌드/테스트 검증 완료 (`meson setup`, `ninja`, `ninja test`)
- [x] Step 5: patch + 최종 리포트 작성

참고:

- `reference_repos/file-roller` 44.6 트랙은 여전히 GTK4 버전 제약(>=4.8.1)으로 로컬 빌드 불가
- 로컬 우선 개발/검증은 `reference_repos/file-roller-local` 기준으로 진행
