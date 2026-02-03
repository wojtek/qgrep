# AGENTS

Project notes for automation and code changes:

- Language: C++.
- Repo search: prefer `rg` for file/text search.
- Build (Windows): `qgrep.sln` with VS/MSBuild. Release x64 output is `E:\qgrep\build\Release_x64\qgrep.exe`.
- Build (CMake): output is in the chosen build dir (single-config: `build\qgrep.exe`; multi-config: `build\Release\qgrep.exe`).
- Deploy updated binary to: `E:\wojtek_elysium5.6_tsunami\agentx\bin\qgrep\qgrep.exe`.
- Index config(s) live under: `E:\wojtek_elysium5.6_tsunami\agentx\bin\qgrep\indexes\`.
  - Current convo index cfg: `E:\wojtek_elysium5.6_tsunami\agentx\bin\qgrep\indexes\conv.cfg`.
- Typical update command (from `E:\wojtek_elysium5.6_tsunami`):
  - `.\agentx\bin\qgrep\qgrep.exe update .\agentx\bin\qgrep\indexes\conv.cfg`
- `conv.cfg` content summary:
  - JSONL transform on `C:\Users\wojtek\.claude\projects\**\*.jsonl` and `C:\Users\wojtek\.codex\sessions\**\*.jsonl`
  - Raw JSON for `C:\Users\wojtek\.gemini\tmp\**\*.json` (excluding `bin/`)
- Transform caching change: after deploying new qgrep, run update once to seed transform metadata; subsequent updates should be fast unless logs change.
- `CLAUDE.md` is a symlink to this file.
