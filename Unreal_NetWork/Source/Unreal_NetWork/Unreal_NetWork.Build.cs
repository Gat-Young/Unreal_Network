using UnrealBuildTool;

public class Unreal_NetWork : ModuleRules
{
	public Unreal_NetWork(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTP", "Json", "Slate", "SlateCore" });
	}
}
