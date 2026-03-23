# UnrealAICLI Plugin Changes (Claude / Codex / MCPOnly)

This document explains the UnrealAICLI (`UnrealAICLI-Win64-v1.4.0`) changes and how to use the new modes.

## 1. Goal

The plugin was extended from "Claude Code chat + MCP" to three selectable modes:

- `Claude`: use Claude Code CLI for chat, keep MCP server.
- `Codex`: use Codex CLI for chat, keep MCP server.
- `MCPOnly`: disable chat provider, keep MCP server only.

## 2. Main Code Changes

### 2.1 Provider mode abstraction

Added provider mode and helper APIs in `ClaudeCodeRunner`:

- `EUnrealAICLIProviderMode`: `Claude` / `Codex` / `MCPOnly`
- config resolver: `GetProviderMode()`
- mode check: `IsMCPOnlyMode()`
- availability check: `IsConfiguredProviderAvailable()`
- provider hints:
  - `GetConfiguredProviderName()`
  - `GetConfiguredProviderInstallHint()`
  - `GetConfiguredProviderLoginHint()`

Hardcoded Claude-only availability/error messages were replaced with provider-aware text.

Files:

- `Source/UnrealAICLI/Public/ClaudeCodeRunner.h`
- `Source/UnrealAICLI/Private/ClaudeCodeRunner.cpp`

### 2.2 Block chat in MCPOnly

Added MCP-only guard in `ClaudeSubsystem::SendPrompt()`:

- when `ProviderMode=MCPOnly`, chat request is rejected early.

File:

- `Source/UnrealAICLI/Private/ClaudeSubsystem.cpp`

### 2.3 Startup flow split by mode

Module startup now branches by mode:

- non-`MCPOnly`: register chat commands/tab/menu/QuickAsk.
- `MCPOnly`: do not register chat UI, only start MCP server.

Shutdown cleanup remains symmetric.

Files:

- `Source/UnrealAICLI/Public/UnrealAICLIModule.h`
- `Source/UnrealAICLI/Private/UnrealAICLIModule.cpp`

### 2.4 Provider-aware editor widget text/status

In the chat panel:

- startup message now shows current provider.
- missing CLI message uses provider-specific install/login hints.
- `MCPOnly` explicitly shows chat-disabled message.
- status text is provider-neutral.

File:

- `Source/UnrealAICLI/Private/ClaudeEditorWidget.cpp`

### 2.5 Config update

Unified provider config in project settings:

- `ProviderMode=Claude|Codex|MCPOnly`
- `ProviderMode` is the only mode switch now. `bMCPOnlyMode` is removed.

File:

- `Config/DefaultEditor.ini`

## 3. Usage

### 3.1 Set mode

`Config/DefaultEditor.ini`

```ini
[UnrealAICLI]
; ProviderMode options: Claude | Codex | MCPOnly
ProviderMode=Claude
```

### 3.2 Mode workflows

#### A. Claude mode

1. Set `ProviderMode=Claude`
2. Install CLI: `npm install -g @anthropic-ai/claude-code`
3. Login: `claude auth login`
4. Start Unreal Editor and use AI Assistant + MCP tools.

#### B. Codex mode

1. Set `ProviderMode=Codex`
2. Install CLI: `npm install -g @openai/codex`
3. Login: `codex login`
4. Start Unreal Editor and use AI Assistant + MCP tools.

#### C. MCP-only mode

1. Set `ProviderMode=MCPOnly`
2. Start Unreal Editor.
3. Chat UI is disabled (or not registered), MCP server remains active.

## 4. VS2026 + UE5.3 Build Notes

If you use VS2026 (v18.x), UE5.3 may fail with latest MSVC (14.50+). Validated setup:

1. Install MSVC `14.38.33130` inside VS2026.
2. Configure UBT:

`C:\Users\ban-m\AppData\Roaming\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`

```xml
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
  <WindowsPlatform>
    <Compiler>VisualStudio2022</Compiler>
    <CompilerVersion>14.38.33130</CompilerVersion>
  </WindowsPlatform>
</Configuration>
```

Notes:

- UE5.3 does not recognize `VisualStudio2026` enum in UBT config.
- Log may still say "Using Visual Studio 2022..."; this is expected for UE5.3 enum naming.
- With this config, UBT still uses MSVC 14.38 from your VS2026 installation path.

## 5. Quick verification

From project root:

```powershell
& 'C:\UE_5.3\Engine\Build\BatchFiles\Build.bat' LyraEditor Win64 Development 'G:\Unreal\LyraStarterGame\LyraStarterGame.uproject' -WaitMutex -NoHotReloadFromIDE
```

If build succeeds, it validates:

- provider-mode code compiles,
- UnrealAICLI links correctly,
- current toolchain config is valid.

