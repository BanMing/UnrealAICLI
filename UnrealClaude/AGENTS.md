# UnrealClaude - Claude Code Instructions for Unreal Engine 5.7

This file provides guidance to Claude Code when working with the UnrealClaude plugin and Unreal Engine 5.7 projects.

## Project Overview

**UnrealClaude** is an Unreal Engine 5.7 plugin that provides MCP (Model Context Protocol) integration, enabling Claude AI to interact directly with the Unreal Editor via REST API tools.

### MCP Tool Priority

When working with Unreal Editor content, ALWAYS prefer MCP tools over filesystem tools:
- Use `asset_search` instead of Glob/Grep to find assets
- Use `spawn_actor` instead of writing Python scripts to create actors
- Use `get_level_actors` instead of reading level files to see what's in a scene
- Use `blueprint_query` instead of reading .uasset files to inspect blueprints
- MCP tools operate on the live editor state — filesystem tools see serialized data

### Tool Router (MANDATORY)

Domain-specific operations MUST go through the `unreal_ue` router tool. NEVER call these tools directly:
- `blueprint_modify` → use `unreal_ue` with `domain: "blueprint"`
- `anim_blueprint_modify` → use `unreal_ue` with `domain: "anim"`
- `character` / `character_data` → use `unreal_ue` with `domain: "character"`
- `enhanced_input` → use `unreal_ue` with `domain: "enhanced_input"`
- `material` → use `unreal_ue` with `domain: "material"`
- `asset` (create/import/export) → use `unreal_ue` with `domain: "asset"`

Simple tools are called directly with `unreal_` prefix: `unreal_spawn_actor`, `unreal_move_actor`, `unreal_get_level_actors`, `unreal_asset_search`, `unreal_blueprint_query`, etc.

**Router call format:**
```json
{
  "domain": "blueprint",
  "operation": "add_variable",
  "params": { "blueprint_path": "/Game/BP_Example", "var_name": "Health", "var_type": "float" }
}
```

### Parallel Tool Execution

When performing complex Unreal tasks, use parallel MCP tool calls and subagents to maximize throughput.

**Concurrency & Timeout Limits (CRITICAL):**
- Unreal's task queue processes **max 4 concurrent tasks**. Extra calls queue automatically but add latency.
- Keep parallel subagent count to **3 subagents max** (leaves 1 slot for the lead agent's own calls).
- Timeout chain: Game thread dispatch = 30s → Task default = 2 min → Bridge async = 5 min.
- Read-only tools are fast (~50-200ms). Modifying tools (blueprint, spawn) may take 1-5s each.
- If a subagent needs 5+ sequential tool calls, budget ~30s and keep tasks focused.

**Tool parallelization classes:**

| Class | Tools | Rule |
|-------|-------|------|
| **Parallel-safe** (read-only) | asset_search, get_level_actors, blueprint_query, asset_dependencies, asset_referencers, capture_viewport, get_output_log | Call freely in parallel. No conflicts. |
| **Per-object safe** (modifying) | spawn_actor, move_actor, set_property, blueprint_modify, material, character, character_data, asset, enhanced_input, anim_blueprint_modify | Parallelize on DIFFERENT actors/assets. Never modify same object from two calls. |
| **Sequential only** | open_level, delete_actors, execute_script, cleanup_scripts, run_console_command | Must run alone. open_level invalidates all refs. |

**When to use subagents (Task tool):**
- Request maps to 3+ independent operations on different objects
- Examples: "set up a level" → lighting agent + mesh agent + gameplay agent (3 subagents)
- Lead agent surveys first (read-only), plans names, spawns subagents, verifies results
- NEVER spawn more than 3 subagents — the task queue limit is 4 concurrent tasks total

**When NOT to parallelize:**
- Single-object operations — just call tools directly
- Operations needing results from prior calls (spawn then move same actor)
- Anything involving open_level, delete_actors, or execute_script
- When the total parallel tool call count would exceed 4 (queue delays + timeout risk)

**Subagent coordination:**
1. Lead surveys state (get_level_actors, asset_search — parallel read-only calls)
2. Lead plans decomposition with unique actor/asset names
3. Lead spawns ≤3 subagents with explicit tool calls, names, and positions
4. Lead verifies results (get_level_actors, capture_viewport)

For detailed workflow patterns: `unreal_get_ue_context` with query "parallel workflows"

### Key Directories
- `Source/UnrealClaude/Private/MCP/` - MCP server and tool implementations
- `Source/UnrealClaude/Private/MCP/Tools/` - Individual MCP tools
- `Source/UnrealClaude/Private/Tests/` - Automation tests
- `Resources/mcp-bridge/` - Node.js MCP bridge

### Build Commands
```bash
# Build plugin (from project root UnrealClaude/)
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin -Plugin="C:/Users/Natal/OneDrive/Documents/Github/UnrealClaude/UnrealClaude/UnrealClaude.uplugin" -Package="C:/Users/Natal/OneDrive/Documents/Github/UnrealClaude/PluginBuild" -TargetPlatforms=Win64 -Rocket

# Run tests (in Unreal Editor console)
Automation RunTests UnrealClaude
```

### Build Directories
- **Temporary Build Output**: `C:/Users/Natal/OneDrive/Documents/Github/UnrealClaude/PluginBuild/` - UAT builds here
- **Engine Installation**: `C:/Program Files/Epic Games/UE_5.7/Engine/Plugins/Marketplace/UnrealClaude/` - Copy binaries here after build

**NOTE**: Binaries are no longer tracked in the repository (LFS removed). After building, copy from `PluginBuild/Binaries/Win64/` to the engine Marketplace folder for local use.

### MCP Bridge (Git Submodule)

`Resources/mcp-bridge/` is a **git submodule** pointing to [`Natfii/ue5-mcp-bridge`](https://github.com/Natfii/ue5-mcp-bridge).

**Fresh clone setup:**
```bash
git clone --recurse-submodules https://github.com/Natfii/UnrealClaude.git
# Or if already cloned:
git submodule update --init
cd Resources/mcp-bridge && npm install
```

**Workflow for MCP bridge changes:**
1. `cd Resources/mcp-bridge` — enter the submodule
2. Make changes, commit, and push within the submodule (`git add . && git commit && git push`)
3. `cd ../..` — return to parent repo
4. `git add Resources/mcp-bridge` — stage the updated submodule ref
5. Commit the parent repo to record the new submodule commit

**Run tests:** `cd Resources/mcp-bridge && npm test`

**When to update tests:**
- Adding or modifying MCP tools
- Changing `context-loader.js`
- Fixing bridge bugs (add regression test first)
- Changing HTTP request/response format

**Test structure:**
- `tests/unit/` — Schema conversion, HTTP client, context loader, async execution
- `tests/integration/` — Tool listing, tool execution with mocked Unreal server
- `tests/helpers/` — Mock fetch, fixtures

---

## Unreal Engine 5.7 C++ Standards

### IMPORTANT: Stay Focused on UE 5.7
- This project targets **Unreal Engine 5.7.2** exclusively
- Use UE 5.7 API patterns and conventions
- Do NOT suggest deprecated APIs or patterns from older engine versions
- When uncertain about API availability, verify against UE 5.7 documentation

### File Organization
- **Maximum 500 lines per file** - Split large files into logical units
- **Maximum 50 lines per function** - Extract helper functions for clarity
- **Private/** folder for implementation files
- **Public/** folder for headers meant for external use

### UE 5.7 C++ Reference

For UPROPERTY/UFUNCTION specifiers, animation API, Slate patterns, and common UE patterns:
- Use `unreal_get_ue_context` with relevant category (animation, blueprint, slate, etc.)
- Use the engine source at `C:/Users/Natal/Github/UnrealEngine/Engine/Source/` for API verification
- Copyright header: `// Copyright Natali Caggiano. All Rights Reserved.`

---

## MCP Tool Development Guidelines

### Creating New MCP Tools
1. Create header in `Private/MCP/Tools/MCPTool_YourTool.h`
2. Inherit from `FMCPToolBase`
3. Implement `GetInfo()` with proper parameters and annotations
4. Implement `Execute()` with parameter validation
5. Register in `MCPToolRegistry.h`
6. Add tests in `Private/Tests/MCPToolTests.cpp`

### Tool Parameter Validation Pattern
```cpp
FMCPToolResult FMCPTool_YourTool::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Extract and validate required parameters
    FString RequiredParam;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("param_name"), RequiredParam, Error))
    {
        return Error.GetValue();
    }

    // Validate paths for security
    if (!ValidateBlueprintPathParam(BlueprintPath, Error))
    {
        return Error.GetValue();
    }

    // Extract optional parameters with defaults
    int32 OptionalInt = Params->GetIntegerField(TEXT("optional_param"));

    // Do work...

    return FMCPToolResult::Success(TEXT("Operation completed"));
}
```

### Tool Annotations
```cpp
// Read-only tool (safe, no modifications)
Info.Annotations = FMCPToolAnnotations::ReadOnly();

// Modifying tool (changes state, reversible)
Info.Annotations = FMCPToolAnnotations::Modifying();

// Destructive tool (cannot be undone via MCP)
Info.Annotations = FMCPToolAnnotations::Destructive();
```

---

## Dynamic UE 5.7 Context System

The MCP bridge includes dynamic context files that provide accurate UE 5.7 API documentation. These are loaded on-demand based on tool usage or explicit queries.

### Context Files Location
```
Resources/mcp-bridge/contexts/
├── actor.md                # Actor spawning, components, transforms
├── animation.md            # Animation Blueprint, state machines, UAnimInstance
├── assets.md               # Asset loading, TSoftObjectPtr, async loading
├── blueprint.md            # Blueprint graphs, UK2Node, FBlueprintEditorUtils
├── character.md            # Character movement, data assets, stats
├── enhanced_input.md       # Enhanced Input actions, mapping contexts
├── material.md             # Material instances, parameters, assignment
├── parallel_workflows.md   # Parallel tool execution, subagent patterns
├── replication.md          # Network replication, RPCs, DOREPLIFETIME
└── slate.md                # Slate UI widgets, SNew/SAssignNew patterns
```

### Maintaining Context Files

**IMPORTANT**: When adding new MCP tools or discovering UE 5.7 API changes:

1. **Update relevant context file** in `Resources/mcp-bridge/contexts/`
2. **Verify accuracy** via web search or official UE 5.7 documentation
3. **Add tool patterns** to `context-loader.js` if new tool categories are added
4. **Test context loading**:
   ```bash
   cd Resources/mcp-bridge
   node -e "import('./context-loader.js').then(m => console.log(m.listCategories()))"
   ```

### Adding New Context Categories

1. Create `Resources/mcp-bridge/contexts/newcategory.md`
2. Add entry to `CONTEXT_CONFIG` in `context-loader.js`:
   ```javascript
   newcategory: {
     files: ["newcategory.md"],
     toolPatterns: [/pattern1/, /pattern2/],
     keywords: ["keyword1", "keyword2", ...]
   }
   ```
3. Update this CLAUDE.md and README.md

### Context Accuracy Guidelines

- All code examples must be verified against UE 5.7 API
- Use web search to confirm API signatures before documenting
- Include source links in context files where helpful
- Remove deprecated patterns from older engine versions

### MCP Tutorials & Guidelines

Tutorial and guideline documents for MCP development are maintained at:

- **Local Path**: `C:/Users/Natal/OneDrive/Documents/Github/UnrealClaude/Tutorials/`

Consult these when doing MCP tool development, bridge modifications, or integration work.

### UE 5.7 Engine Source Reference

A local sparse checkout of the Unreal Engine 5.7.3 source is available for verifying API signatures, finding includes, and checking implementations:

- **Path**: `C:/Users/Natal/Github/UnrealEngine/Engine/Source/`
- **Tag**: `5.7.3-release` (shallow clone, `Engine/Source/` only)

**When to use the engine source:**
- Verifying UPROPERTY/UFUNCTION specifiers and their exact syntax
- Finding the correct `#include` path for a class or struct
- Checking function signatures, parameter types, and return values
- Understanding base class implementations before overriding
- Confirming API availability in 5.7 (vs deprecated/removed APIs)

**When NOT to use the engine source:**
- Don't read entire large files — use Grep to find specific symbols
- Don't browse aimlessly — search for the specific class/function you need
- Prefer MCP tools for runtime editor state (actors, assets, levels)

**Key subdirectories:**

| Directory | Contents |
|-----------|----------|
| `Runtime/Engine/Classes/` | Core classes: AActor, UWorld, UGameInstance, components |
| `Runtime/Engine/Public/` | Engine public headers |
| `Runtime/CoreUObject/` | UObject, UPROPERTY, reflection, garbage collection |
| `Editor/UnrealEd/` | Editor subsystems, FBlueprintEditorUtils, asset tools |
| `Runtime/AnimGraphRuntime/` | Animation blueprint nodes, state machine runtime |
| `Runtime/EnhancedInput/` | Enhanced Input System (UInputAction, UInputMappingContext) |
| `Runtime/SlateCore/` + `Runtime/Slate/` | Slate UI framework |
| `Runtime/UMG/` | UMG/Widget framework |
| `Developer/AssetTools/` | Asset type actions, import/export |

**Search examples:**
```bash
# Find a class header
Grep for "class.*AActor\b" in Engine/Source/ with glob "*.h"

# Find UFUNCTION signature
Grep for "GetActorLocation" in Engine/Source/Runtime/Engine/

# Find include path for a type
Grep for "FStreamableManager" in Engine/Source/ with glob "*.h"
```

---

## Testing Standards

### Automation Test Pattern
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMyTest,
    "UnrealClaude.Category.TestName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMyTest::RunTest(const FString& Parameters)
{
    // Test assertions
    TestTrue("Description of what should be true", bCondition);
    TestFalse("Description of what should be false", bCondition);
    TestEqual("Values should match", ActualValue, ExpectedValue);
    TestNotNull("Pointer should not be null", Pointer);
    TestNull("Pointer should be null", Pointer);

    return true;
}
```

### Test Categories for This Project
- `UnrealClaude.MCP.Tools.*` - Individual tool tests
- `UnrealClaude.MCP.ParamValidator.*` - Parameter validation tests
- `UnrealClaude.MCP.Registry.*` - Tool registry tests

---

## Security Considerations

### Parameter Validation (ALWAYS DO THIS)
1. **Path Validation**: Block `/Engine/`, `/Script/`, path traversal (`../`)
2. **Actor Name Validation**: Block special characters `<>|&;$(){}[]!*?~`
3. **Console Command Validation**: Block dangerous commands (quit, crash, shutdown)
4. **Numeric Validation**: Check for NaN, Infinity, reasonable bounds

### Never Do
- Execute arbitrary code without permission dialogs
- Allow access to engine internals
- Skip parameter validation
- Trust user input without sanitization

---

---

## Commit Standards

When committing changes to this project:
- `feat:` New MCP tools or features
- `fix:` Bug fixes
- `test:` Test additions or fixes
- `docs:` Documentation updates
- `refactor:` Code refactoring without behavior changes

Always run `Automation RunTests UnrealClaude` before committing.
