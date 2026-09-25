using UnrealBuildTool;

public class HeatlineMX : ModuleRules
{
	public HeatlineMX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"ProceduralMeshComponent", "MeshDescription", "StaticMeshDescription",
			"Json", "JsonUtilities", "AudioMixer", "AudioExtensions",
			"Slate", "SlateCore", "ApplicationCore", "RenderCore", "RHI"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
