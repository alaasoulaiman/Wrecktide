# Third party notices

Wrecktide's own code and content are under the MIT licence (see LICENSE). This file lists what in the repository, and what the game needs, comes from others.

## Unreal Engine (Epic Games, Inc.)

Wrecktide is made with Unreal Engine 5.6, used under the Unreal Engine End User License Agreement: https://www.unrealengine.com/en-US/eula/unreal

Wrecktide uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved.

## Epic template content in this repository

The folders and files below are Epic's content provided with the engine (the Third Person template and its shared packs) and are used under the Unreal Engine EULA. The MIT licence does not cover them.

- Content/ThirdPerson
- Content/Variant_Combat. Three of its Blueprints carry changes for Wrecktide (BP_CombatEnemy, BP_CombatGameMode, BP_CombatPlayerController), and Anims/AM_ChargedAttack_Player and Anims/AM_ComboAttack_Player are copies of the template's attack montages.
- Content/Variant_Platforming
- Content/Variant_SideScrolling
- Content/LevelPrototyping: the 36 files of the template's LevelPrototyping pack
- Content/Characters/Mannequins: the template's Manny and Quinn mannequins
- Content/__ExternalActors__ and Content/__ExternalObjects__: the actors of the four template levels
- Content/Input: Actions/IA_Jump, Actions/IA_Look, Actions/IA_MouseLook, Actions/IA_Move, IMC_Default, IMC_MouseLook, Touch/BPI_TouchInterface, Touch/UI_Thumbstick and Touch/UI_TouchSimple
- Source files whose first line is "// Copyright Epic Games, Inc. All Rights Reserved.": the Third Person template code, renamed from TP_ThirdPerson to Wrecktide (Wrecktide.Target.cs, WrecktideEditor.Target.cs, Wrecktide.Build.cs, Wrecktide.cpp and .h, WrecktideCharacter, WrecktideGameMode, WrecktidePlayerController, and everything under Source/Wrecktide/Variant_Combat, Variant_Platforming and Variant_SideScrolling)

## Content this repository leaves out

- **Game Animation Sample** by Epic Games, free on Fab, under the Fab Standard License, which does not allow its files in a public repository. The game uses its character, animation Blueprint, input, mannequins and audio. `Tools/import_game_animation_sample.ps1` copies exactly the files listed in `Tools/game_animation_sample_files.txt` from your own copy of the sample: Content/Blueprints, 23 files in Content/Input, Content/Characters/UE4_Mannequin, Content/Characters/UEFN_Mannequin, Content/Audio/Ambient, Content/Audio/Foley, Content/Audio/Mix and 23 files in Content/LevelPrototyping. The project's edits to five of those files are described in SAMPLE_EDITS.md.
- **Starter Content** by Epic Games. Four of its sounds (Starter_Birds01, Starter_Music01, Starter_Wind05, Starter_Wind06) come inside the Game Animation Sample's Content/Audio/Ambient folder, so the import script brings them with the rest; Starter Content itself ships with the engine and is not needed separately.
- **POLYGON Pirates** by Synty Studios, paid, from Synty Studios (syntystore.com). Its licence does not allow sharing the source files outside your team. The game looks for it in Content/PolygonPirates; the README says what is missing without it.

## Fonts

Three font families under the SIL Open Font License, Version 1.1. The licence text of each sits beside its files in Content/UI/Fonts.

- Cinzel Decorative, Copyright (c) 2012 Natanael Gama: CinzelDecorative-Regular.ttf, CinzelDecorative-Bold.ttf; licence OFL_CinzelDecorative.txt
- IM Fell English, Copyright (c) 2010 Igino Marini: IMFellEnglish-Regular.ttf, IMFellEnglish-Italic.ttf; licence OFL_IMFellEnglish.txt
- Pirata One, Copyright (c) 2012 Rodrigo Fuenzalida and Nicolas Massi: PirataOne-Regular.ttf; licence OFL_PirataOne.txt

## Sounds

The sounds in Content/Audio/SFX are my own work, synthesised with Python's standard library (Tools/gen_pistol_sfx.py makes the pistol cues).

## Licence boundaries

Licence: MIT, see LICENSE. Unreal Engine template code and content stay under the Unreal Engine EULA and are not covered by the MIT licence: the source files that carry an Epic Games copyright line, and the Epic template content listed in THIRD_PARTY_NOTICES.md. Seven of those source files also carry a Modifications Copyright line; the code added to them for Wrecktide is under MIT. The Wrecktide name and logo are not covered by the MIT licence and remain with Alaa Soulaiman. Third party content: see THIRD_PARTY_NOTICES.md.
