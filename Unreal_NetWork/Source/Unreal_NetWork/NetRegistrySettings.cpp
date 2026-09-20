#include "NetRegistrySettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FString UNetRegistrySettings::GetRegistryUrl()
{
	FString Url = GetDefault<UNetRegistrySettings>()->RegistryUrl;
	FParse::Value(FCommandLine::Get(), TEXT("RegistryUrl="), Url);
	Url.RemoveFromEnd(TEXT("/"));
	return Url;
}
