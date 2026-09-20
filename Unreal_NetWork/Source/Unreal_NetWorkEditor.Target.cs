using UnrealBuildTool;

public class Unreal_NetWorkEditorTarget : TargetRules
{
	public Unreal_NetWorkEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("Unreal_NetWork");
	}
}
