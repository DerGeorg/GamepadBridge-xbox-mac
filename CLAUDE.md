# GamepadBridge

## Abweichungen von den Skills

| Skill | Regel | Hier stattdessen | Warum | Seit |
|---|---|---|---|---|
| gitlab-devops | Pipelines bestehen nur aus Imports aus `ci/components` | eigene Jobs in `.gitlab-ci.yml`, die `scripts/verify-release.sh` und `scripts/publish-release.sh` aufrufen | Die Logik ist app-spezifisch — Sparkle-EdDSA-Prüfung, Appcast, Homebrew-Tap. Eine Component für ein einziges Projekt wäre Verwaltung ohne Wiederverwendung. Kommt eine zweite Mac-App mit Sparkle dazu, gehört das in eine Component. | 2026-09 |
| gitlab-devops | Version aus Conventional Commits (semantic-release) | `XOW_RELEASE_VERSION` in `CMakeLists.txt`, von Hand gesetzt | Gebaut und signiert wird auf dem Mac, vor der Pipeline. Die Version steckt dabei im signierten Bundle und in der Sparkle-Signatur; semantic-release rechnet sie erst in der Pipeline aus, also zu spät. | 2026-09 |
