#include "Unreal_NetWork.h"
#include "NetChatAgent.h"
#include "GameFramework/GameModeBase.h"
#include "Modules/ModuleManager.h"

class FUnrealNetWorkModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		PostLoginHandle = FGameModeEvents::OnGameModePostLoginEvent().AddStatic(&ANetChatAgent::SpawnForPlayer);
	}

	virtual void ShutdownModule() override
	{
		FGameModeEvents::OnGameModePostLoginEvent().Remove(PostLoginHandle);
	}

private:
	FDelegateHandle PostLoginHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FUnrealNetWorkModule, Unreal_NetWork, "Unreal_NetWork");
