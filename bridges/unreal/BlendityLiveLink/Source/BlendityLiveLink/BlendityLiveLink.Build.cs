// SPDX-License-Identifier: GPL-2.0-or-later
using UnrealBuildTool;

public class BlendityLiveLink : ModuleRules
{
	public BlendityLiveLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp17;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore", "UnrealEd", "LevelEditor", "Sockets", "Networking", "MeshDescription",
			"StaticMeshDescription", "ImageWrapper", "Projects", "ToolMenus", "WorkspaceMenuStructure"
		});
		// (the protocol layer is standard C++: its exceptions-free code needs nothing more)
	}
}
