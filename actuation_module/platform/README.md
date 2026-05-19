# platform/

RTOS バックエンド層。`core/` が依存する唯一の境界（プラットフォーム interface）と、
その各 RTOS 実装を置く。

- `platform.hpp` — プラットフォーム interface（タスク / 時間 / 実時刻 / config /
  ネットワーク / ログ出力）。Phase 2 で追加する。
- `zephyr/` — Zephyr バックエンド。Phase 3 で追加する。
- `freertos/posix/` — FreeRTOS POSIX シミュレータバックエンド（Stage A）。
- `freertos/cortex_r52/` — FreeRTOS Cortex-R52 + lwIP バックエンド（Stage B）。

設計の全体像は `docs/superpowers/specs/2026-05-18-freertos-safety-island-feasibility-design.md`
を参照。
