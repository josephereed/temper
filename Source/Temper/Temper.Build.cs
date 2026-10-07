using UnrealBuildTool;

public class Temper : ModuleRules
{
	public Temper(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "RenderCore", "RHI", "Slate", "SlateCore", "ApplicationCore"
		});
	}
}
