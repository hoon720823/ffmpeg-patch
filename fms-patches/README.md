# fms-patches — ABR seamless reconfigure for fms-agent

ffmpeg n8.1.1 위에 얹는 fms 운영용 patch. 작업 결과물은 모두 이 디렉토리 안 또는
mainline 파일의 마킹된 hunk 로만. mainline rebase 충돌 최소화 목적.

## 왜 patch 인가

mainline ffmpeg 의 ZMQ filter (`libavfilter/f_zmq.c`) 는 `avfilter_graph_send_command` 만
호출 → filter graph 안의 filter 옵션만 변경 가능. **인코더 (`AVCodecContext`) 에는
못 닿음**.

fms-agent 는 ABR 트리거 시 인코더 `bit_rate` / `rc_max_rate` / `rc_buffer_size` 를
mid-stream 으로 바꿔야 함. 현재 동작은 ffmpeg 프로세스 재시작 → 1~2초 frame loss.

PoC 검증 (fms-agent 의 `cmd/astiav-poc/`) 결과:
- ffmpeg 의 인코더 wrapper (`libx264.c` / `nvenc.c` 등) 는 `ctx->bit_rate` 변화를
  감지해서 native reconfigure API (`x264_encoder_reconfig` / `nvEncReconfigureEncoder`)
  를 자동 호출. **즉 인코더 reconfigure 메커니즘은 이미 동작**.
- 막힌 건 **외부 → 인코더 컨텍스트 통로** 뿐. 그 통로를 ZMQ 위에 만드는 게 본 patch.

## Patch 설계 (옵션 C — 사용자 합의 2026-05-09)

```
ZMQ message ── f_zmq.c ──▶ 기존 path (filter graph 명령)
                  │
                  └──▶ "@enc_<stream_idx>" target 검출 시
                       └──▶ 등록된 callback 호출
                            └──▶ fftools/ffmpeg.c 안의 인코더 lookup
                                 + AVCodecContext field mutation
                                 (다음 frame 처리 시 wrapper 가 자동 reconfig)
```

핵심:
- f_zmq.c 가 callback 함수 포인터를 등록받음 (default = NULL = 현재 동작).
- main loop (ffmpeg.c) 가 callback 등록 + 안에서 stream_idx → AVCodecContext 매핑.
- AVCodecContext->bit_rate 등 mutation 만 → 다음 frame 처리 시 mainline wrapper 의
  `reconfig_encoder()` 가 자동으로 native API 호출. **인코더별 추가 patch 불필요**.

ZMQ 명령 형식:
```
@enc_<stream_idx> <option> <value>

예:
  @enc_0 b 1000k          # AVCodecContext->bit_rate = 1000000
  @enc_0 maxrate 1200k    # AVCodecContext->rc_max_rate = 1200000
  @enc_0 bufsize 1000k    # AVCodecContext->rc_buffer_size = 1000000
```

기존 ZMQ 명령 (`Parsed_scale_0 width 640` 등) 은 그대로 동작.

## 단계 (Phase)

1. **stub patch** — f_zmq.c 가 `@enc_*` 명령 받으면 stderr log 만. 실제 mutation X.
   목적: ZMQ 통로 + 명령 parse 까지 동작 확인.
2. **callback registry** — f_zmq.c 에 `void(*)(const char *target, const char *cmd, const char *arg)` 등록 인터페이스.
3. **ffmpeg.c 통합** — main loop 에서 callback 등록 + stream_idx 로 인코더 lookup +
   AVCodecContext field mutation. thread-safety (mutex).
4. **검증** — fms-agent 의 PoC 와 동일 시나리오 (libx264 / NVENC) 로 mid-stream
   bitrate 변경 확인.
5. **자체 빌드 파이프라인** — BtbN GitHub Actions workflow fork. Linux/Windows × x64/arm64.
6. **release tag** — `release-fms-8.1.1-r1` 같은 식.

## License / 충돌

- patch 자체는 ffmpeg 와 동일 라이센스 (LGPL/GPL).
- mainline rebase: ffmpeg upstream 새 release 시 `git rebase --onto upstream/release/N.N abr-zmq-patches`. f_zmq.c / ffmpeg.c 는 자주 안 변하므로 conflict 작을 것.

## Branch

- `abr-zmq-patches` (현재) — n8.1.1 base + 우리 commit 누적.
- mainline 은 `upstream` remote 의 `master` / `release/8.1` 등.
