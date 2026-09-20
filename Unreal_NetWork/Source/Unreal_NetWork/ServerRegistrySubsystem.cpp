#include "ServerRegistrySubsystem.h"
#include "NetRegistrySettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogServerRegistry, Log, All);

bool UServerRegistrySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return IsRunningDedicatedServer() || FParse::Param(FCommandLine::Get(), TEXT("RegisterServer"));
}

void UServerRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const float Interval = FMath::Max(2.f, GetDefault<UNetRegistrySettings>()->HeartbeatSeconds);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UServerRegistrySubsystem::Tick), Interval);
	// ?�드가 준비되???��?즉시 �??�록
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float) { Tick(0.f); return false; }), 3.f);
}

void UServerRegistrySubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	Post(TEXT("/api/servers/unregister"));
	Super::Deinitialize();
}

bool UServerRegistrySubsystem::Tick(float)
{
	Post(TEXT("/api/servers/register"));
	return true;
}

void UServerRegistrySubsystem::Post(const FString& Path)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const UNetRegistrySettings* Settings = GetDefault<UNetRegistrySettings>();

	FString Name = Settings->ServerName, Ip = Settings->PublicIp, Key = Settings->ServerKey;
	FParse::Value(FCommandLine::Get(), TEXT("ServerName="), Name);
	FParse::Value(FCommandLine::Get(), TEXT("PublicIp="), Ip);
	FParse::Value(FCommandLine::Get(), TEXT("ServerKey="), Key);

	AGameModeBase* GM = World->GetAuthGameMode();
	const int32 Players = GM ? GM->GetNumPlayers() : 0;

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Name);
	if (!Ip.IsEmpty())
	{
		Json->SetStringField(TEXT("ip"), Ip);
	}
	Json->SetNumberField(TEXT("port"), World->URL.Port);
	Json->SetStringField(TEXT("map"), World->GetMapName());
	Json->SetNumberField(TEXT("players"), Players);
	Json->SetNumberField(TEXT("maxPlayers"), 16);

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Json, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(UNetRegistrySettings::GetRegistryUrl() + Path);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("X-Server-Key"), Key);
	Req->SetContentAsString(Body);
	Req->OnProcessRequestComplete().BindLambda([Path](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
		{
			UE_LOG(LogServerRegistry, Log, TEXT("%s OK: %s"), *Path, *Resp->GetContentAsString());
		}
		else
		{
			UE_LOG(LogServerRegistry, Warning, TEXT("%s ?�패 (code %d)"), *Path, Resp.IsValid() ? Resp->GetResponseCode() : 0);
		}
	});
	Req->ProcessRequest();
}
