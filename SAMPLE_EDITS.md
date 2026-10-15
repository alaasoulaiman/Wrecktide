# Game Animation Sample edits

Edits this project makes to Game Animation Sample assets, which the licence keeps out of this repository.

The player character, its animation Blueprint, its input mapping, its skeleton and its retargeter all start life in Epic's Game Animation Sample. The sample is free on Fab, but its licence does not let me publish its files, edited or not, so this repository leaves them out. `Tools/import_game_animation_sample.ps1` copies the sample's original files from your own copy of the sample (see the README). Five of those files carry edits the game depends on, listed below. After the import script runs, you apply these edits in the editor; until then the player is the sample's own character. Moving these edits into C++ or into the project's own assets, so that a clone plays without this step, is open work.

How to read each entry: the commits come from `git log --follow --format='%h %ad %s' --date=short -- <path>` on my private development repository, newest first (the public history starts fresh, so the hashes are references, not links). Two subjects named the play tester; that name is left out here. Quotes are from the commit messages and my progress notes for those commits. Where a script in `Tools` made the same change, it is named; the scripts run as `-run=pythonscript` commandlets, see each script's header.

Every other sample file the import script copies is unchanged since it was imported (one commit each in that history), and the 23 LevelPrototyping files are byte identical to a copy of the sample.

The original order, by date: SK_UEFN_Mannequin (07-26), CBP_SandboxCharacter and IMC_Sandbox (07-26 to 07-28), RTG_UEFN_to_UE4_Mannequin (07-27 and 07-28), ABP_SandboxCharacter (08-21 and 09-02), CBP_SandboxCharacter again (09-02), RTG_UEFN_to_UE4_Mannequin again (09-04 and 09-05).

## 1. Content/Blueprints/CBP_SandboxCharacter

What the edit does: the Blueprint is reparented from Character to the project's CombatCharacter class, and its class defaults carry the camera distance, the pirate skin with its retarget setup, the held weapon table and the player's own attack montages.

Commits:

- `5189fbb` 2026-09-02 Player attack montages on the UpperBody slot, CBP points at the _Player copies; editor-tools library for headless slot edits
- `e2a0bcb` 2026-07-28 Straight legs: leg IK goals BlendToSource 1->0 (root cause of crooked pose)
- `270e33d` 2026-07-27 Fix dead player movement: restore stripped LevelBlock_Traversable refs + real-input probe
- `4a44c4d` 2026-07-26 Chunk C leftover: held-weapon visuals driven by the equipped weapon row
- `b231868` 2026-07-26 Chunk C: Synty pirate skin on the combat pawn (retarget rig + tag contract)
- `4fd407b` 2026-07-26 Chunk B: GAS locomotion pawn IS the combat player (reparent + CDO bake + probes)
- `a028fa5` 2026-07-21 Chunk A.2: migrate GAS locomotion content (UEFN+UE4 mannequin, sandbox CBP/ABP, foley, input) (the import of the sample's file)

Notes:

- 4fd407b: "CBP_SandboxCharacter reparented ACharacter -> ACombatCharacter and wired as the GameMode default pawn (+ PC CharacterClass)." The game mode and player controller side of that wiring lives in the template Blueprints this repository keeps (BP_CombatGameMode, BP_CombatPlayerController).
- 4fd407b, the camera: "hip-cam 100 -> GAS framing 300 via DefaultCameraDistance on the CBP CDO."
- b231868: "a C++ CosmeticMesh child component under the driving UEFN mesh, posed by ABP_GenericRetarget via RTG_UEFN_to_UE4_Mannequin. Config is data-side on the CBP CDO (skin/anim class/retargeter)".
- 4a44c4d: "Data-driven: WeaponVisuals map on the CBP CDO (WornCutlass->SM_Wep_Cutlass_01, BoardingAxe->SM_Wep_Axe_01, OfficersSabre->SM_Wep_Cutlass_04, configured by Tools/chunkc2_apply_weapons.py)". The weapon meshes are from the POLYGON Pirates pack.
- 5189fbb, progress note: "AM_ComboAttack_Player + AM_ChargedAttack_Player slot tracks -> UpperBody, CBP_SandboxCharacter CDO Combo/ChargedAttackMontage -> the _Player copies, enemies keep the originals on DefaultSlot." The two _Player montages are in Content/Variant_Combat/Anims.
- 270e33d, a warning for whoever repeats the reparent: "the Chunk B reparent save (4fd407b) then STRIPPED the dead class ref from the asset (a028fa5 had it x2, 4fd407b x0)". The fix: "restore the pristine CBP from a028fa5, replay the scripted mutation chain (reparent + camera + cosmetic + weapons, all fail-closed, zero drift), refs verified surviving every save." So run the import script first: the 23 LevelPrototyping files must be in place before you reparent and save.
- e2a0bcb: the message does not describe a change to this Blueprint, and its size stayed 2331017 bytes. Tools/pose_skin_swap.py from the same commit swaps the player's skin on this asset for a control test and names the pirate skin script as its restore.

Scripts: Tools/abp_upperbody_part2.py makes the montage change (5189fbb). The reparent, the camera distance, the skin and the weapon table were set by one off scripts that are not in this repository; apply them by hand from the notes above.

## 2. Content/Blueprints/ABP_SandboxCharacter

What the edit does: it adds an upper body layer to the sample's animation graph, so the player's attack montages play on an UpperBody slot layered over the locomotion pose from spine_01 up.

Commits:

- `585f435` 2026-09-02 ABP upper-body layer configured headless: spine_01 branch filter + mesh-space rotation, compiled clean, wiring re-verified from T3D
- `9ecbd61` 2026-08-21 WIP: ABP mid-surgery checkpoint, graph is NOT done, do not treat as complete
- `a028fa5` 2026-07-21 Chunk A.2: migrate GAS locomotion content (UEFN+UE4 mannequin, sandbox CBP/ABP, foley, input) (the import of the sample's file)

Notes:

- 9ecbd61: "Protective checkpoint of the working tree exactly as it sat after the 2026-07-28 hand-wired anim graph session." The graph edit itself was done by hand in the editor.
- 585f435, progress note, the wiring: "LocoPose cache -> base + UpperBody slot -> layer 0 -> DefaultSlot -> OffsetRootBone."
- 585f435, progress note, the settings: "Layer Setup[0] = spine_01 depth 0, Mesh Space Rotation Blend ON".

Scripts: wire the five connections by hand first (Python has no access to graph pins), then Tools/abp_upperbody_step11.py sets the layer settings.

## 3. Content/Input/IMC_Sandbox

What the edit does: it makes the sample's input mapping fit the combat player: four rows that clashed with the attack buttons are removed, the arrow keys drive IA_Move the way W A S D do, and the mouse wheel drives IA_CameraZoom.

Commits:

- `f1901f8` 2026-07-27 Scroll-wheel camera zoom, hard-capped (round-3 request)
- `110375c` 2026-07-27 Attack input redesign + air momentum + arrow keys (playtest round 2)
- `4fd407b` 2026-07-26 Chunk B: GAS locomotion pawn IS the combat player (reparent + CDO bake + probes)
- `a028fa5` 2026-07-21 Chunk A.2: migrate GAS locomotion content (UEFN+UE4 mannequin, sandbox CBP/ABP, foley, input) (the import of the sample's file)

Notes:

- 4fd407b: "IMC strip, attacks win on conflicts: IA_Aim@RMB, IA_Walk@Gamepad_RightShoulder (fail-closed rail surfaced it; walk keeps its keyboard key), IA_NextCharacter x2."
- 110375c: "ARROW KEYS: they were bound to the GAS demo world-axis action (IA_Move_WorldSpace) = the scrambled directions. Rebound to IA_Move with WASD-matched modifiers".
- f1901f8: "New IA_CameraZoom (Axis1D, cloned headless via Tools/add_zoom_input.py with enum-discovery) mapped to MouseWheelAxis in IMC_Sandbox". IA_CameraZoom itself is the project's own asset and is in this repository.

Scripts: Tools/fix_arrow_keys.py (the arrow keys) and Tools/add_zoom_input.py (the wheel row; it reuses IA_CameraZoom when the asset already exists). Remove the four clashing rows by hand.

## 4. Content/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin

What the edit does: it makes the pirate skins stand and run with straight legs: the leg IK goals stop chasing the mannequin's absolute foot positions, their static offset is zeroed, the SyntyStraight retarget pose is current, and Pole Vector Alignment is off on both leg chains.

Commits:

- `c718b39` 2026-09-05 Straighten the running legs: disable Pole Vector Alignment on the leg chains
- `f6d08a4` 2026-09-04 Crooked legs: zero the leg IK goal StaticLocalOffset (+4/-4 on the foot's local X = 4 cm straight up on this skeleton family, 22 deg of manufactured knee bend); -PoseProbe numeric leg gate (driver vs skin knee angle, idle and moving); retarget/rig recon script
- `e2a0bcb` 2026-07-28 Straight legs: leg IK goals BlendToSource 1->0 (root cause of crooked pose)
- `59f0ed1` 2026-07-27 Night: pose-quality attempts 1+2 REVERTED (honest fail), tools kept
- `a028fa5` 2026-07-21 Chunk A.2: migrate GAS locomotion content (UEFN+UE4 mannequin, sandbox CBP/ABP, foley, input) (the import of the sample's file)

Notes:

- e2a0bcb: "The Retarget IK Goals op shipped BlendToSource=1.0 on LeftLeg/RightLeg, so Synty skins chased the source mannequin's absolute foot positions: tiptoe trailing foot, scissored idle, combo leg-clipping." The fix sets it to 0 on both leg chains.
- f6d08a4, progress note: "both leg offsets zeroed; arms untouched."
- c718b39: "SyntyStraight stays the current pose with its 07-28 calf and thigh offsets; the op is disabled on LeftLeg and RightLeg."
- 59f0ed1 tried two changes and reverted both; an extra retarget pose it added was left inactive and is not needed.
- The leg edits only matter with the POLYGON Pirates skins; e2a0bcb calls the defect "Invisible on mannequin targets (the GAS case)".

Scripts, in commit order: Tools/pose_fix_ikgoals.py (BlendToSource), Tools/pose_fix_synty.py (creates SyntyStraight; it aligns against the Synty captain mesh, so it needs the POLYGON Pirates pack), Tools/pose_fix_localoffset.py (the static offset), Tools/pose_final_legs.py (SyntyStraight current with its calf and thigh offsets, Pole Vector Alignment off).

## 5. Content/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin

What the edit does: it lists SK_Mannequin as a compatible skeleton on SK_UEFN_Mannequin, so the combat attack montages, authored on SK_Mannequin, play on the sample character's skeleton.

Commits:

- `9a22753` 2026-07-26 Chunk B step 1: register SK_Mannequin as compatible on SK_UEFN_Mannequin
- `a028fa5` 2026-07-21 Chunk A.2: migrate GAS locomotion content (UEFN+UE4 mannequin, sandbox CBP/ABP, foley, input) (the import of the sample's file)

Notes:

- 9a22753: "one property edit registering SK_Mannequin on SK_UEFN_Mannequin.compatible_skeletons." Without it: "A reparented pirate would have moved but its attacks would have silently no-opped."

Scripts: Tools/register_skeleton_compat.py.
