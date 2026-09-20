#include "NetChatAgent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetChat, Log, All);

FOnNetChatReceived ANetChatAgent::OnChatReceived;
FOnNetChatOpenRequested ANetChatAgent::OnChatOpenRequested;

ANetChatAgent::ANetChatAgent()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bOnlyRelevantToOwner = true;
	SetReplicatingMovement(false);
}

void ANetChatAgent::SpawnForPlayer(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || !NewPlayer || GameMode->GetNetMode() == NM_Client)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = NewPlayer;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GameMode->GetWorld()->SpawnActor<ANetChatAgent>(ANetChatAgent::StaticClass(), FTransform::Identity, Params);
}

ANetChatAgent* ANetChatAgent::GetLocalAgent(UWorld* World)
{
	if (World)
	{
		for (TActorIterator<ANetChatAgent> It(World); It; ++It)
		{
			const APlayerController* PC = Cast<APlayerController>(It->GetOwner());
			if (PC && PC->IsLocalController())
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void ANetChatAgent::BeginPlay()
{
	Super::BeginPlay();
	TryInitLocal();
}

void ANetChatAgent::OnRep_Owner()
{
	Super::OnRep_Owner();
	TryInitLocal();
}

void ANetChatAgent::TryInitLocal()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (bLocalInit || !PC || !PC->IsLocalController())
	{
		return;
	}
	bLocalInit = true;
	EnableInput(PC);
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ANetChatAgent::OnEnterPressed);
	}
}

void ANetChatAgent::OnEnterPressed()
{
	OnChatOpenRequested.Broadcast();
}

void ANetChatAgent::ServerSendChat_Implementation(const FString& Message)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	FString Text = Message.TrimStartAndEnd().Left(200);
	if (!PC || Text.IsEmpty())
	{
		return;
	}
	const FString Sender = PC->PlayerState ? PC->PlayerState->GetPlayerName() : TEXT("?");
	UE_LOG(LogNetChat, Log, TEXT("[Chat] %s: %s"), *Sender, *Text);
	for (TActorIterator<ANetChatAgent> It(GetWorld()); It; ++It)
	{
		It->ClientReceiveChat(Sender, Text);
	}
}

void ANetChatAgent::ClientReceiveChat_Implementation(const FString& Sender, const FString& Message)
{
	UE_LOG(LogNetChat, Log, TEXT("[ChatRecv] %s: %s"), *Sender, *Message);
	OnChatReceived.Broadcast(Sender, Message);
}
