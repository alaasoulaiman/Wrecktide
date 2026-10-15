"""island2_rows.py - append the Gallows Reach data rows to Content\\Data\\*.csv (2026-09-07, spec C34).

Plain CPython (NOT an editor commandlet): the CSVs are loose files read at runtime by
UWrecktideDataSubsystem::LoadCsvTable, so a text append is the whole job and it needs no engine.
Idempotent: a row whose key is already present is left exactly as it is, so re-running never
duplicates or rewrites. CRLF is preserved because every other line in these files uses it.

WHY NavyGarrisonOfficer AND NOT NavyOfficer: Enemies.csv already ships a level 4 `NavyOfficer`
(gold 14..29) and -EconomyProbe pins that row's numbers by name
(WrecktideRewardSubsystem.cpp RunEconomyProbe, the FEnemyGold table). Reusing the name for the
level 17 elite would fail that probe and silently re-tune a Wreck Cove enemy, so the garrison's
elite gets its own row name.

Gold follows the same curve the economy probe documents: per level L, 5+3(L-1) .. 11+6(L-1), a boss
at three times its own level's band.

Run: python Tools\\island2_rows.py [--check]
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "Content", "Data")

SOLDIER = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_English_Soldier_01.SK_Chr_English_Soldier_01"
GOVERNOR = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_English_Govenor_02.SK_Chr_English_Govenor_02"
CAPTAIN = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_English_Captain_01.SK_Chr_English_Captain_01"

ROWS = {
    "Enemies.csv": [
        'NavyMarine,"Navy Marine",9,260,20,29,59,220,Melee,%s,1100,2600' % SOLDIER,
        'NavyMusketeer,"Navy Musketeer",13,360,26,41,83,320,Melee,%s,1100,2600' % SOLDIER,
        'NavyGarrisonOfficer,"Navy Garrison Officer",17,460,30,53,107,420,Melee,%s,1100,2600' % GOVERNOR,
        'LieutenantHarrow,"Lieutenant Harrow",18,900,38,168,339,900,Melee,%s,900,2200' % CAPTAIN,
    ],
    "Chests.csv": [
        'MagazineChest,"Powder Magazine Chest",120,3',
    ],
    "Npcs.csv": [
        'Ada,"Ada Kell, the Turncoat Sergeant","Ada Kell",Story_Reach,'
        '"Keep walking. I wear the wrong coat to be seen talking to a pirate who has not earned the reach.",5',
    ],
    "Quests.csv": [
        'MarineWatch,"The Marine Watch","Redcoat marines hold the garrison square. Every one you leave on the stones is one fewer musket at the wall.",NavyMarine,5,90,300,Melee,true,5,Story_Reach',
        'OfficerHunt,"Officer Hunt","The garrison officers keep the muster rolls. Take two of them and the reach forgets who is counted.",NavyGarrisonOfficer,2,140,420,Melee,true,6,Story_Reach',
    ],
    "Missions.csv": [
        # Cobb's H04, gated on Story_Harbourmaster (set by H03_PaidInFull) and unlocking Ada.
        # ReachSpot with a TargetName no live actor answers to falls back to TargetLocation
        # (WrecktideMissionLogic::ResolveSpotLocation), which is how a spot on a second island with
        # no landmark actor still gives the beacon and the map a place to point at.
        'H04_GallowsReach,"The Gallows Reach","Cobb the harbourmaster",Cobb,'
        '"There is a reach south-east of here the navy hung a captain of mine on. Sail out and put your boots on that sand, '
        'then come back and tell me it is still there.",'
        '"Sail south-east and land on Gallows Reach.",'
        '"So it stands. Find the sergeant who wears the coat and hates it, and mind the lighthouse.",'
        'ReachSpot,GallowsLanding,1,"(X=9500.000,Y=-12700.000,Z=-40.000)",900.0,80,260,Sailing,False,0.0,Story_Harbourmaster,False,Story_Reach,19',
        'R01_ThinTheWatch,"Thin the Watch","Ada Kell",Ada,'
        '"I keep the muster book and I keep the truth apart from it. Three marines off the square and nobody counts them twice.",'
        '"Kill 3 navy marines on the square.",'
        '"Three lines I will not write. Good.",'
        'KillRow,NavyMarine,3,"(X=0.000,Y=0.000,Z=0.000)",400.0,90,320,Melee,False,0.0,Story_Reach,False,None,20',
        'R02_Magazine,"The Powder Magazine","Ada Kell",Ada,'
        '"They keep the powder in a shed with one lock and two drunks. The chest under the crates is not on any list.",'
        '"Open the chest in the powder magazine.",'
        '"Nothing in there was ever the Crown\'s. Take it.",'
        'OpenChest,MagazineChest,1,"(X=0.000,Y=0.000,Z=0.000)",400.0,70,280,Melee,False,0.0,None,False,None,21',
        'R03_TheLight,"The Light","Ada Kell",Ada,'
        '"Climb the lighthouse and stand on the top. Whoever holds that light holds every hull that comes in at night.",'
        '"Climb to the top of the lighthouse.",'
        '"You saw the whole reach from up there. So does Harrow.",'
        'ReachSpot,LighthouseTop,1,"(X=14200.000,Y=-13800.000,Z=640.000)",520.0,100,360,Sailing,False,0.0,None,False,None,22',
        'R04_Harrow,"Lieutenant Harrow","Ada Kell",Ada,'
        '"Harrow gave the order at the gallows. He stands there still, as if the rope were his own work. End him and the reach is nobody\'s.",'
        '"Kill Lieutenant Harrow at the gallows.",'
        '"The reach is quiet. I will burn the muster book tonight.",'
        'KillBoss,LieutenantHarrow,1,"(X=0.000,Y=0.000,Z=0.000)",400.0,300,900,Melee,False,0.0,None,False,Story_Gallows,23',
        'W09_SquareSweep,"Square Sweep","Ada Kell",Ada,'
        '"The square fills up again every watch. Three marines and I will pay you out of the powder money.",'
        '"Kill 3 navy marines for Ada.",'
        '"Quiet stones again. Come back when they are not.",'
        'KillRow,NavyMarine,3,"(X=0.000,Y=0.000,Z=0.000)",400.0,45,150,Melee,True,180.0,None,False,None,109',
        'W10_PowderMoney,"Powder Money","Ada Kell",Ada,'
        '"Thirty gold buys the shed guard a bottle and a blind eye for a week.",'
        '"Pay Ada 30 gold for the guard\'s bottle.",'
        '"He will not see a thing until Thursday.",'
        'Pay,None,30,"(X=0.000,Y=0.000,Z=0.000)",400.0,30,120,Melee,True,300.0,None,False,None,110',
    ],
}


def key_of(line):
    return line.split(",", 1)[0]


def main():
    check = "--check" in sys.argv[1:]
    problems = []
    for name, lines in ROWS.items():
        path = os.path.join(DATA, name)
        if not os.path.isfile(path):
            problems.append("%s missing" % name)
            continue
        with open(path, "r", encoding="utf-8", newline="") as handle:
            text = handle.read()
        present = set()
        for existing in text.splitlines():
            if existing.strip():
                present.add(key_of(existing))
        wanted = [line for line in lines if key_of(line) not in present]
        if check:
            missing = [key_of(line) for line in wanted]
            print("%-14s rows present=%d missing=%s" % (name, len(lines) - len(wanted), missing or "none"))
            if missing:
                problems.append("%s missing %s" % (name, missing))
            continue
        if not wanted:
            print("%-14s already complete" % name)
            continue
        if not text.endswith("\r\n"):
            text += "\r\n"
        text += "".join(line + "\r\n" for line in wanted)
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        print("%-14s appended %d row(s): %s" % (name, len(wanted), [key_of(line) for line in wanted]))

    if problems:
        print("RESULT=FAIL %s" % problems)
        return 1
    print("RESULT=OK")
    return 0


sys.exit(main())
