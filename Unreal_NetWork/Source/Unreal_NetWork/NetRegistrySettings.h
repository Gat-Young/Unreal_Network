#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NetRegistrySettings.generated.h"

/** 서버 IP 레지스트리 웹서버 설정 (Config/DefaultGame.ini). 명령줄 -RegistryUrl=, -ServerKey=, -PublicIp=, -ServerName= 으로 덮어쓸 수 있다. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Net Registry"))
class UNetRegistrySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Registry")
	FString RegistryUrl = TEXT("http://127.0.0.1:8080");

	/** 서버가 등록할 때 쓰는 공유 키 (웹서버의 REGISTRY_SERVER_KEY와 동일해야 함) */
	UPROPERTY(Config, EditAnywhere, Category = "Registry")
	FString ServerKey = TEXT("dev-server-key");

	UPROPERTY(Config, EditAnywhere, Category = "Registry")
	FString ServerName = TEXT("Unreal Dedicated Server");

	/** 비어 있으면 웹서버가 요청 IP로 결정한다 */
	UPROPERTY(Config, EditAnywhere, Category = "Registry")
	FString PublicIp;

	UPROPERTY(Config, EditAnywhere, Category = "Registry")
	float HeartbeatSeconds = 10.f;

	static FString GetRegistryUrl();
};
