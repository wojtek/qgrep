# AGENTS

Project notes for automation and code changes:

- Language: C++.
- Repo search: prefer `rg` for file/text search.
- Build (Windows): `qgrep.sln` with VS/MSBuild. Release x64 output is `E:\qgrep\build\Release_x64\qgrep.exe`.
- Build (CMake): output is in the chosen build dir (single-config: `build\qgrep.exe`; multi-config: `build\Release\qgrep.exe`).
- Deploy updated binary to: `E:\wojtek_elysium_tsunami\agentx\bin\qgrep\qgrep.exe`.
- Current conversation config for this user: `C:\Users\wojtek\.qgrep\conv.cfg` (`~/.qgrep/conv.cfg`).
- Typical PowerShell update command (both paths are passed separately):
  - `& 'E:\wojtek_elysium_tsunami\agentx\bin\qgrep\qgrep.exe' update 'C:\Users\wojtek\.qgrep\conv.cfg'`
- `conv.cfg` content summary:
  - JSONL transform on `C:\Users\wojtek\.claude\projects\**\*.jsonl` and `C:\Users\wojtek\.codex\sessions\**\*.jsonl`
  - Gemini transform on `C:\Users\wojtek\.gemini\tmp\*\chats\*.json` (excluding `bin/`)
- Transform caching: unchanged sources can be reused within one compatible content generation. A generation change requires coordinated binary/AgentX-consumer updates followed by an explicit index build; ordinary update must refuse an incompatible generation without changing it.
- `CLAUDE.md` is a symlink to this file.
