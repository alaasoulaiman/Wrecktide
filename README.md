# Wrecktide

Wrecktide is a pirate action RPG made with Unreal Engine 5.6. You wash up at Wreck Cove with a broken crew and a name nobody remembers, then fight, sail, trade and upgrade your way from the first stolen handful of gold toward a captain who owns the sea. The world is dark and gritty with a fantastical streak: lantern lit ports, a sea that is a threat rather than a backdrop, and islands such as Gallows Reach and Mangrove Hollow with their own quest chains and bosses.

Trailer: linked from the launch post.

## Requirements

- Unreal Engine 5.6
- Visual Studio 2022 with the Game development with C++ workload
- Windows 10 or 11
- The free Game Animation Sample from Fab (setup step 2)

## Setup

1. Clone the repository:
   `git clone https://github.com/alaasoulaiman/Wrecktide.git`
2. In the Epic Games Launcher, add the free Game Animation Sample from Fab and create that sample project once, in a folder outside this repository.
3. Copy the sample files this project uses into Content, first as a dry run to see the list, then for real:
   `powershell -ExecutionPolicy Bypass -File Tools/import_game_animation_sample.ps1 -Source "<sample project>/Content" -DryRun`
   `powershell -ExecutionPolicy Bypass -File Tools/import_game_animation_sample.ps1 -Source "<sample project>/Content"`
   The script copies only the paths listed in `Tools/game_animation_sample_files.txt`: the sample character and its Blueprints, its input, its mannequins, its ambient, foley and mix audio, and 23 LevelPrototyping files. It never overwrites a file that is already there. The sample's licence keeps these files out of this repository.
4. Optional: the POLYGON Pirates pack by Synty Studios. It is paid, and its licence forbids sharing the files, which is why it is not here. Add it to this project so it sits in `Content/PolygonPirates`. Without it the island loses its dressing (the pirate camp, the pier and its moored ship, the buildings and props), the boats and stalls come up bare, and every character falls back to the plain mannequin; the game should still load and play on the bare island.
5. Open `Wrecktide.uproject` and let it compile the Wrecktide module when it asks.
6. Apply the edits in [SAMPLE_EDITS.md](SAMPLE_EDITS.md). The sample's character, animation Blueprint, input mapping, skeleton and retargeter need a handful of edits in the editor before the sample character becomes the Wrecktide player; that file lists each one, with the scripts in `Tools` that make some of them.
7. Play `Lvl_Island` (it is the default map).

## Controls

The same list is on the in game Controls screen.

- Move: W A S D or the arrow keys
- Sprint: Left Shift
- Walk: Left Ctrl
- Crouch: C
- Jump: Space
- Attack combo: left mouse button
- Charged attack: right mouse button, hold to charge and release to strike
- Aim: mouse
- Strafe: middle mouse button
- Camera zoom: mouse wheel
- Talk, board, use: F
- Weapon bar: 1 to 5
- Captain's Chest: Tab or I
- Journal: J
- Map cycle: M
- Pause: Esc
- On the boat: W and S to sail, A and D to steer, F to go ashore
- Swimming: W A S D
- Menus: arrow keys and Enter, Left and Right turn the chest pages, Esc goes back

The bindings live in `Content/Input`, in the sample's IMC_Sandbox after the edits above, and in the C++ under `Source/Wrecktide`.

## Project layout

- `Source/Wrecktide`: the C++ game module. `Gameplay` holds the actors (boats, stalls, mission givers, chests), `Systems` the game logic (economy, stats, skills, progress and saving, rewards, missions, quests, wardrobe, options, audio), `UI` the HUD and menus, `Data` the table row types. `Variant_Combat`, `Variant_Platforming` and `Variant_SideScrolling` are the Third Person template code the game grew from.
- `Content/Lvl_Island.umap`: the game level.
- `Content/Data`: the CSV tables for weapons, enemies, loot, missions, quests, NPCs, outfits, chests and upgrades. The game reads them from disk at runtime, so you edit the CSV files directly (see `Source/Wrecktide/Data/README.md`).
- `Content/UI`: menu art, the island chart plates and the three fonts.
- `Content/Audio/SFX`: the game's sounds, synthesised in house.
- `Content/Materials`, `Content/Input`, `Content/Splash`: the project's materials, input actions and splash image.
- `Content/ThirdPerson`, `Content/Variant_Combat`, `Content/Variant_Platforming`, `Content/Variant_SideScrolling`, `Content/LevelPrototyping`, `Content/Characters/Mannequins`, `Content/__ExternalActors__`, `Content/__ExternalObjects__`: Epic's Third Person template content, see THIRD_PARTY_NOTICES.md.
- `Config`: project settings.
- `Tools`: editor scripts (Python run as editor commandlets) and PowerShell helpers, including the sample import script.
- `Build/Windows`: the game icon.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). [SAMPLE_EDITS.md](SAMPLE_EDITS.md) names open work that needs no art: moving the sample edits into the project's own code and assets.

## Licence

Licence: MIT, see LICENSE. Unreal Engine template code and content stay under the Unreal Engine EULA and are not covered by the MIT licence: the source files that carry an Epic Games copyright line, and the Epic template content listed in THIRD_PARTY_NOTICES.md. Seven of those source files also carry a Modifications Copyright line; the code added to them for Wrecktide is under MIT. The Wrecktide name and logo are not covered by the MIT licence and remain with Alaa Soulaiman. Third party content: see THIRD_PARTY_NOTICES.md.
