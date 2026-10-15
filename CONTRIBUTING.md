# Contributing to Wrecktide

Thanks for helping. These few rules keep changes easy to review.

## Before you start

- Open an issue first for anything large: a new system, a new island, a change to how combat or sailing feels. We agree on the shape before you spend the time.
- Small fixes can go straight to a pull request.
- Set the project up as the README describes, including the sample import and SAMPLE_EDITS.md, so you test against the game as it really plays.

## Pull requests

- Keep them small: one change per pull request.
- Name your branch the way this project does: `feat/<what it does>`, for example `feat/cannons`.
- Say what you changed and how you checked it: which map, which probe, and a screenshot for anything visual.

## Binary assets

- One asset per commit where possible, so a review can see what changed.
- Never re save assets you did not change. The editor rewrites a .uasset on save, and an untouched asset in a diff hides the real change.
- Keep every file under 100 MB; GitHub refuses larger files and this repository does not use Git LFS.
- Never add files from the packs this repository leaves out (the Game Animation Sample, POLYGON Pirates); see THIRD_PARTY_NOTICES.md.

## Probes

The game has more than forty built in probes. Launch it with one probe switch and it plays a scripted check, writes a verdict line to the log and quits. For example:

```
"<Unreal Engine 5.6>/Engine/Binaries/Win64/UnrealEditor.exe" "<repo>/Wrecktide.uproject" -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -LocoProbe
```

then look for `WRECKTIDE-LOCO-PROBE PASS` or `WRECKTIDE-LOCO-PROBE FAIL` in `Saved/Logs/Wrecktide.log`. The switches (LocoProbe, AttackProbe, ComboProbe, InputProbe, PoseProbe and the rest) are registered in `Source/Wrecktide/Systems/WrecktideRewardSubsystem.cpp` and listed in `Source/Wrecktide/Systems/WrecktidePlayerState.cpp`, where the list is compiled out of shipping builds. The script that ran them as one battery is not in this repository yet, so run the probes that touch your change and list the ones that passed in your pull request.

## Licence of contributions

By sending a pull request you agree that your change is licensed under the MIT licence of this repository, with your own copyright.
