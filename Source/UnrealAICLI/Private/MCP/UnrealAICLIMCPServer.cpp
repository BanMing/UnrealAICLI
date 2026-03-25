// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealAICLIMCPServer.h"
#include "MCPToolRegistry.h"
#include "UnrealAICLIModule.h"
#include "UnrealAICLIConstants.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FUnrealAICLIMCPServer::FUnrealAICLIMCPServer()
	: bIsRunning(false)
	, ServerPort(UnrealAICLIConstants::MCPServer::DefaultPort)
{
	ToolRegistry = MakeShared<FMCPToolRegistry>();
}

FUnrealAICLIMCPServer::~FUnrealAICLIMCPServer()
{
	Stop();
}

bool FUnrealAICLIMCPServer::Start(uint32 Port)
{
	if (bIsRunning)
	{
		UE_LOG(LogUnrealAICLI, Warning, TEXT("MCP Server is already running on port %d"), ServerPort);
		return true;
	}

	// Commandlets (HotPatcher/Cook/Build pipelines) do not require MCP HTTP endpoints.
	// Starting a listener in this mode can fail when port 3000 is already occupied,
	// which would pollute commandlet logs with error-level messages and fail CI gates.
	// We therefore short-circuit startup here to keep packaging deterministic.
	if (IsRunningCommandlet())
	{
		UE_LOG(LogUnrealAICLI, Display, TEXT("Skipping MCP server startup in commandlet mode."));
		return true;
	}

	ServerPort = Port;

	// Get or start the HTTP server module.
	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();

	// Enable listener startup, then request a router.
	// Use non-failing bind behavior so the editor can continue even if the default
	// port is occupied by another local process.
	HttpServerModule.StartAllListeners();
	HttpRouter = HttpServerModule.GetHttpRouter(ServerPort, false);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogUnrealAICLI, Warning, TEXT("MCP listener unavailable on 127.0.0.1:%d. MCP tools disabled for this session."), ServerPort);
		return true;
	}

	// Setup routes after listener is confirmed bindable.
	SetupRoutes();

	bIsRunning = true;

	// Start the async task queue
	if (ToolRegistry.IsValid())
	{
		ToolRegistry->StartTaskQueue();
	}

	UE_LOG(LogUnrealAICLI, Log, TEXT("MCP Server started on http://localhost:%d"), ServerPort);
	UE_LOG(LogUnrealAICLI, Log, TEXT("  GET  /mcp/tools      - List available tools"));
	UE_LOG(LogUnrealAICLI, Log, TEXT("  POST /mcp/tool/{name} - Execute a tool"));
	UE_LOG(LogUnrealAICLI, Log, TEXT("  GET  /mcp/status     - Server status"));

	return true;
}


void FUnrealAICLIMCPServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	// Stop the async task queue first
	if (ToolRegistry.IsValid())
	{
		ToolRegistry->StopTaskQueue();
	}

	// Unbind routes
	if (HttpRouter.IsValid())
	{
		if (ListToolsHandle.IsValid())
		{
			HttpRouter->UnbindRoute(ListToolsHandle);
		}
		if (ExecuteToolHandle.IsValid())
		{
			HttpRouter->UnbindRoute(ExecuteToolHandle);
		}
		if (StatusHandle.IsValid())
		{
			HttpRouter->UnbindRoute(StatusHandle);
		}
	}

	bIsRunning = false;
	UE_LOG(LogUnrealAICLI, Log, TEXT("MCP Server stopped"));
}

void FUnrealAICLIMCPServer::SetupRoutes()
{
	if (!HttpRouter.IsValid())
	{
		return;
	}

	// GET /mcp/tools - List all available tools
	ListToolsHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp/tools")),
		EHttpServerRequestVerbs::VERB_GET,
		// Use CreateRaw to bind a stable member-function delegate that exactly matches
		// FHttpRequestHandler in UE5.7. This avoids lambda conversion edge cases across
		// engine minor updates and keeps the route binding ABI-compatible.
		FHttpRequestHandler::CreateRaw(this, &FUnrealAICLIMCPServer::HandleListTools)
	);

	// POST /mcp/tool/* - Execute a tool (wildcard path)
	ExecuteToolHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp/tool")),
		EHttpServerRequestVerbs::VERB_POST,
		// Bind as member function for the same reason as above: deterministic delegate
		// type conversion for HttpServer's request handler signature.
		FHttpRequestHandler::CreateRaw(this, &FUnrealAICLIMCPServer::HandleExecuteTool)
	);

	// GET /mcp/status - Server status
	StatusHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp/status")),
		EHttpServerRequestVerbs::VERB_GET,
		// Keep route registration style consistent across endpoints to reduce maintenance
		// risk when upgrading HTTPServer internals.
		FHttpRequestHandler::CreateRaw(this, &FUnrealAICLIMCPServer::HandleStatus)
	);
}

bool FUnrealAICLIMCPServer::HandleListTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> ToolsArray;

	if (ToolRegistry.IsValid())
	{
		TArray<FMCPToolInfo> Tools = ToolRegistry->GetAllTools();
		for (const FMCPToolInfo& Tool : Tools)
		{
			TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
			ToolJson->SetStringField(TEXT("name"), Tool.Name);
			ToolJson->SetStringField(TEXT("description"), Tool.Description);

			// Add parameters schema
			TArray<TSharedPtr<FJsonValue>> ParamsArray;
			for (const FMCPToolParameter& Param : Tool.Parameters)
			{
				TSharedPtr<FJsonObject> ParamJson = MakeShared<FJsonObject>();
				ParamJson->SetStringField(TEXT("name"), Param.Name);
				ParamJson->SetStringField(TEXT("type"), Param.Type);
				ParamJson->SetStringField(TEXT("description"), Param.Description);
				ParamJson->SetBoolField(TEXT("required"), Param.bRequired);
				if (!Param.DefaultValue.IsEmpty())
				{
					ParamJson->SetStringField(TEXT("default"), Param.DefaultValue);
				}
				ParamsArray.Add(MakeShared<FJsonValueObject>(ParamJson));
			}
			ToolJson->SetArrayField(TEXT("parameters"), ParamsArray);

			// Add tool annotations (behavioral hints for LLM clients)
			TSharedPtr<FJsonObject> AnnotationsJson = MakeShared<FJsonObject>();
			AnnotationsJson->SetBoolField(TEXT("readOnlyHint"), Tool.Annotations.bReadOnlyHint);
			AnnotationsJson->SetBoolField(TEXT("destructiveHint"), Tool.Annotations.bDestructiveHint);
			AnnotationsJson->SetBoolField(TEXT("idempotentHint"), Tool.Annotations.bIdempotentHint);
			AnnotationsJson->SetBoolField(TEXT("openWorldHint"), Tool.Annotations.bOpenWorldHint);
			ToolJson->SetObjectField(TEXT("annotations"), AnnotationsJson);

			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolJson));
		}
	}

	ResponseJson->SetArrayField(TEXT("tools"), ToolsArray);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);

	OnComplete(CreateJsonResponse(JsonString));
	return true;
}

bool FUnrealAICLIMCPServer::HandleExecuteTool(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Extract tool name from path: /mcp/tool/{name}
	FString RelativePath = Request.RelativePath.GetPath();

	// Parse tool name from path
	FString ToolName;
	if (RelativePath.StartsWith(TEXT("/mcp/tool/")))
	{
		ToolName = RelativePath.RightChop(10); // Remove "/mcp/tool/"
	}
	else if (RelativePath.StartsWith(TEXT("/")))
	{
		ToolName = RelativePath.RightChop(1);
	}
	else
	{
		ToolName = RelativePath;
	}

	if (ToolName.IsEmpty())
	{
		OnComplete(CreateErrorResponse(TEXT("Tool name not specified. Use POST /mcp/tool/{toolname}"), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	// Parse JSON body for parameters
	TSharedPtr<FJsonObject> ParamsJson;
	if (Request.Body.Num() > UnrealAICLIConstants::MCPServer::MaxRequestBodySize)
	{
		UE_LOG(LogUnrealAICLI, Warning, TEXT("Request body too large: %d bytes (max %d)"), Request.Body.Num(), UnrealAICLIConstants::MCPServer::MaxRequestBodySize);
		OnComplete(CreateErrorResponse(TEXT("Request body too large"), EHttpServerResponseCodes::BadRequest));
		return true;
	}
	if (Request.Body.Num() > 0)
	{
		// Ensure null-termination for safe string conversion
		TArray<uint8> NullTerminatedBody = Request.Body;
		NullTerminatedBody.Add(0);
		FString BodyString = UTF8_TO_TCHAR(reinterpret_cast<const char*>(NullTerminatedBody.GetData()));

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		if (!FJsonSerializer::Deserialize(Reader, ParamsJson) || !ParamsJson.IsValid())
		{
			UE_LOG(LogUnrealAICLI, Warning, TEXT("Failed to parse JSON body: %s"), *BodyString);
			OnComplete(CreateErrorResponse(TEXT("Invalid JSON body"), EHttpServerResponseCodes::BadRequest));
			return true;
		}
	}
	else
	{
		ParamsJson = MakeShared<FJsonObject>();
	}

	// Execute tool
	if (!ToolRegistry.IsValid())
	{
		OnComplete(CreateErrorResponse(TEXT("Tool registry not initialized"), EHttpServerResponseCodes::ServerError));
		return true;
	}

	FMCPToolResult Result = ToolRegistry->ExecuteTool(ToolName, ParamsJson.ToSharedRef());

	// Build response
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	ResponseJson->SetBoolField(TEXT("success"), Result.bSuccess);
	ResponseJson->SetStringField(TEXT("message"), Result.Message);

	if (Result.Data.IsValid())
	{
		ResponseJson->SetObjectField(TEXT("data"), Result.Data);
	}

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);

	EHttpServerResponseCodes Code = Result.bSuccess ? EHttpServerResponseCodes::Ok : EHttpServerResponseCodes::BadRequest;
	OnComplete(CreateJsonResponse(JsonString, Code));
	return true;
}

bool FUnrealAICLIMCPServer::HandleStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();

	ResponseJson->SetStringField(TEXT("status"), TEXT("running"));
	ResponseJson->SetNumberField(TEXT("port"), ServerPort);
	ResponseJson->SetStringField(TEXT("version"), TEXT("1.0.0"));
	ResponseJson->SetNumberField(TEXT("toolCount"), ToolRegistry.IsValid() ? ToolRegistry->GetAllTools().Num() : 0);

	// Add list of available tools
	if (ToolRegistry.IsValid())
	{
		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		for (const FMCPToolInfo& ToolInfo : ToolRegistry->GetAllTools())
		{
			TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
			ToolObj->SetStringField(TEXT("name"), ToolInfo.Name);
			ToolObj->SetStringField(TEXT("description"), ToolInfo.Description);
			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
		}
		ResponseJson->SetArrayField(TEXT("tools"), ToolsArray);
	}

	// Add project info
	ResponseJson->SetStringField(TEXT("projectName"), FApp::GetProjectName());
	ResponseJson->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);

	OnComplete(CreateJsonResponse(JsonString));
	return true;
}

TUniquePtr<FHttpServerResponse> FUnrealAICLIMCPServer::CreateJsonResponse(const FString& JsonContent, EHttpServerResponseCodes Code)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonContent, TEXT("application/json"));
	Response->Code = Code;

	// Add CORS headers - restricted to localhost for security
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("http://localhost") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type") });

	return Response;
}

TUniquePtr<FHttpServerResponse> FUnrealAICLIMCPServer::CreateErrorResponse(const FString& Message, EHttpServerResponseCodes Code)
{
	TSharedPtr<FJsonObject> ErrorJson = MakeShared<FJsonObject>();
	ErrorJson->SetBoolField(TEXT("success"), false);
	ErrorJson->SetStringField(TEXT("error"), Message);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(ErrorJson.ToSharedRef(), Writer);

	return CreateJsonResponse(JsonString, Code);
}
