// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Wrecktide : ModuleRules
{
	public Wrecktide(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"NavigationSystem",
			"UMG",
			"Slate",
			// SlateCore: the C++ menu builds SCompoundWidget/SButton/FSlateBrush directly (UI\SWrecktideMenu)
			"SlateCore",
			"ApplicationCore",
			// RenderCore: the HUD map layer draws solid Canvas triangles (player arrow, rim chevron,
			// objective arrow) with FCanvasTriangleItem, whose only texture-free surface is the
			// engine's GWhiteTexture. First-party engine module, nothing new is downloaded or linked
			// beyond what Engine already pulls in.
			"RenderCore",
			// Niagara: the smithy's forge glow is the Synty pack's NS_FX_Fire (spec C10), and a
			// Niagara system can only be spawned in C++ through UNiagaraComponent. First-party
			// engine module, already enabled as a plugin (the pack ships NS_FX_* assets).
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// editor-only authoring helpers (WrecktideEditorTools::CreateNavBoundsVolume builds a real brush)
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"Wrecktide",
			"Wrecktide/Variant_Platforming",
			"Wrecktide/Variant_Platforming/Animation",
			"Wrecktide/Variant_Combat",
			"Wrecktide/Variant_Combat/AI",
			"Wrecktide/Variant_Combat/Animation",
			"Wrecktide/Variant_Combat/Gameplay",
			"Wrecktide/Variant_Combat/Interfaces",
			"Wrecktide/Variant_Combat/UI",
			"Wrecktide/Variant_SideScrolling",
			"Wrecktide/Variant_SideScrolling/AI",
			"Wrecktide/Variant_SideScrolling/Gameplay",
			"Wrecktide/Variant_SideScrolling/Interfaces",
			"Wrecktide/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
