using UnrealBuildTool;
using System.Collections.Generic;

public class HeatlineMXTarget : TargetRules
{
	public HeatlineMXTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HeatlineMX");
	}
}
