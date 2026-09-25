using UnrealBuildTool;
using System.Collections.Generic;

public class HeatlineMXEditorTarget : TargetRules
{
	public HeatlineMXEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HeatlineMX");
	}
}
