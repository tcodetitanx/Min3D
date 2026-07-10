using UnrealBuildTool;

public class Min3D : ModuleRules
{
	public Min3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"EnhancedInput", "ProceduralMeshComponent",
			"UMG", "Slate", "SlateCore", "RenderCore", "RHI",
			"AudioMixer", "Json", "JsonUtilities", "ImageCore", "ImageWrapper",
			"MeshDescription", "StaticMeshDescription"
		});
	}
}
