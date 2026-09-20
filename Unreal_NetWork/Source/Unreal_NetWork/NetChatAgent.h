#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NetChatAgent.generated.h"

class AGameModeBase;
class APlayerController;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNetChatReceived, const FString& /*Sender*/, const FString& /*Message*/);
DECLARE_MULTICAST_DELEGATE(FOnNetChatOpenRequested);

/**
 * 플레이어마다 서버가 하나씩 생성해 주는 채팅 전달용 액터 (소유자 = PlayerController).
 * 블루프린트 게임모드/컨트롤러를 수정하지 않고도 채팅 RPC를 쓰기 위한 구조.
 */
UCLASS()
class ANetChatAgent : public AActor
{
	GENERATED_BODY()

public:
	ANetChatAgent();

	/** FGameModeEvents::OnGameModePostLoginEvent 에 등록되는 서버측 스폰 함수 */
	static void SpawnForPlayer(AGameModeBase* GameMode, APlayerController* NewPlayer);

	/** 로컬 플레이어의 에이전트 (없으면 nullptr) */
	static ANetChatAgent* GetLocalAgent(UWorld* World);

	/** 로그인한 계정 이름을 서버의 PlayerState에 반영 */
	UFUNCTION(Server, Reliable)
	void ServerSetName(const FString& Name);

	UFUNCTION(Server, Reliable)
	void ServerSendChat(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientReceiveChat(const FString& Sender, const FString& Message);

	/** 로비에서 로그인한 이름 (에이전트가 로컬에서 생성될 때 서버로 전달) */
	static FString LocalUserName;

	static FOnNetChatReceived OnChatReceived;
	static FOnNetChatOpenRequested OnChatOpenRequested;

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_Owner() override;

private:
	void TryInitLocal();
	void OnEnterPressed();

	bool bLocalInit = false;
};
