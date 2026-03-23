# UnrealClaude 插件改造说明（Claude / Codex / MCPOnly）

本文档说明本次对 `UnrealClaude-Win64-v1.4.0` 的改造内容，以及如何在项目中使用。

## 1. 改造目标

将插件从“仅支持 Claude Code 聊天 + MCP”扩展为三种可切换模式：

- `Claude`：使用 Claude Code CLI 作为聊天提供方，同时保留 MCP 服务器能力。
- `Codex`：使用 Codex CLI 作为聊天提供方，同时保留 MCP 服务器能力。
- `MCPOnly`：关闭聊天提供方，仅保留 MCP 服务器能力。

## 2. 主要代码改动

### 2.1 Provider 模式抽象

在 `ClaudeCodeRunner` 中新增 Provider 模式与能力接口：

- `EUnrealClaudeProviderMode` 枚举：`Claude` / `Codex` / `MCPOnly`
- 配置读取：`GetProviderMode()`
- 模式判断：`IsMCPOnlyMode()`
- 可用性判断：`IsConfiguredProviderAvailable()`
- 提示信息：
  - `GetConfiguredProviderName()`
  - `GetConfiguredProviderInstallHint()`
  - `GetConfiguredProviderLoginHint()`

并把原先写死的 Claude CLI 检测/报错文本改为按 Provider 动态生成。

涉及文件：

- `Source/UnrealClaude/Public/ClaudeCodeRunner.h`
- `Source/UnrealClaude/Private/ClaudeCodeRunner.cpp`

### 2.2 MCPOnly 下禁用聊天请求

在 `ClaudeSubsystem` 的 `SendPrompt()` 中增加 MCPOnly 保护：

- 当 `ProviderMode=MCPOnly` 时，直接返回错误信息并中止聊天请求。

涉及文件：

- `Source/UnrealClaude/Private/ClaudeSubsystem.cpp`

### 2.3 模块启动逻辑按模式分流

在模块启动中按模式处理 UI：

- 非 `MCPOnly`：注册聊天相关命令、Tab、菜单、QuickAsk。
- `MCPOnly`：不注册聊天 UI，仅启动 MCP 服务器。

同时保留模块关闭时的对称清理逻辑。

涉及文件：

- `Source/UnrealClaude/Public/UnrealClaudeModule.h`
- `Source/UnrealClaude/Private/UnrealClaudeModule.cpp`

### 2.4 编辑器聊天 UI 文案/状态动态化

在聊天面板中：

- 启动提示改为显示当前 Provider。
- CLI 不可用时显示对应 Provider 的安装与登录提示。
- `MCPOnly` 时显示“仅 MCP 模式，聊天关闭”。
- 状态栏文本改为通用 AI 文案。

涉及文件：

- `Source/UnrealClaude/Private/ClaudeEditorWidget.cpp`

### 2.5 项目配置项更新

在项目配置中新增/统一 Provider 模式配置：

- `ProviderMode=Claude|Codex|MCPOnly`
- 现在只使用 `ProviderMode`；`bMCPOnlyMode` 已移除

涉及文件：

- `Config/DefaultEditor.ini`

## 3. 如何使用

### 3.1 配置模式

在项目配置中设置：

`Config/DefaultEditor.ini`

```ini
[UnrealClaude]
; ProviderMode options: Claude | Codex | MCPOnly
ProviderMode=Claude
```

### 3.2 三种模式的使用方法

#### A. 使用 Claude

1. 设置：`ProviderMode=Claude`
2. 安装 CLI：`npm install -g @anthropic-ai/claude-code`
3. 登录：`claude auth login`
4. 启动 Unreal Editor，使用 AI Assistant + MCP 工具。

#### B. 使用 Codex

1. 设置：`ProviderMode=Codex`
2. 安装 CLI：`npm install -g @openai/codex`
3. 登录：`codex login`
4. 启动 Unreal Editor，使用 AI Assistant + MCP 工具。

#### C. 仅 MCP 服务器

1. 设置：`ProviderMode=MCPOnly`
2. 启动 Unreal Editor。
3. 聊天 UI 会被禁用（或不注册），MCP 服务保持可用。

## 4. VS2026 + UE5.3 编译说明

如果你使用 VS2026（v18.x），UE5.3 可能与最新 MSVC（14.50+）不兼容。建议使用已验证方案：

1. 在 VS2026 中安装 MSVC `14.38.33130`。
2. 配置 UBT：

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

说明：

- UE5.3 不识别 `VisualStudio2026` 枚举；需要使用 `VisualStudio2022`。
- 即便日志显示“Using Visual Studio 2022...”，实际可来自 VS2026 安装路径。

## 5. 快速验证

在项目根目录执行：

```powershell
& 'C:\UE_5.3\Engine\Build\BatchFiles\Build.bat' LyraEditor Win64 Development 'G:\Unreal\LyraStarterGame\LyraStarterGame.uproject' -WaitMutex -NoHotReloadFromIDE
```

如果构建成功，说明：

- Provider 模式代码可编译。
- UnrealClaude 插件可被编辑器正常链接。
- 当前工具链配置有效。

初始化一下这个工程的git仓库，并连接远程仓库，当前gitignore只支持了UE的，添加对ts项目的忽略
这个工程的GitHub地址：git@github.com:BanMing/CardGame.git
HotPatcher插件的地址：git@github.com:BanMing/HotPatcher.git
puerts插件的地址：git@github.com:BanMing/puerts.git