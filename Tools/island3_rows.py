"""Append Mangrove Hollow CSV rows; plain CPython, no engine required.

Existing rows are preserved, duplicate keys or conflicting island rows fail closed.
New enemy columns default old rows to their current musket/melee behaviour. HexAcolyte
reuses the musket range/damage/cooldown/windup and sight rules, with bRangedUnarmed
explicitly allowing an empty hand and RangedFlashColor setting its green curse light.
Skin materials are also referenced by island props, so they cook through the level.
Usage: python Tools/island3_rows.py [--check] [--story]
"""
import csv
import io
from pathlib import Path
import sys

DATA = Path(__file__).resolve().parents[1] / "Content" / "Data"
CHAR = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/"
MAT = "/Game/Materials/"
NEW_COLUMNS = {"bRangedUnarmed": "False", "RangedFlashColor": "(R=1.0,G=0.65,B=0.2,A=1.0)", "SkinMaterial": ""}


def skin(name):
    return CHAR + name + "." + name


def enemy(key, title, level, hp, damage, xp, mesh, spirit=False, ranged=False, boss=False):
    mult = 3 if boss else 1
    material = "M_MangroveWraith" if spirit else "M_MangroveCloth"
    return {"---": key, "DisplayName": title, "Level": str(level), "MaxHealth": str(hp),
            "BaseDamage": str(damage), "GoldDropMin": str(mult * (5 + 3 * (level - 1))),
            "GoldDropMax": str(mult * (11 + 6 * (level - 1))), "XPAward": str(xp),
            "XPSkill": "Voodoo", "SkinMesh": skin(mesh), "AggroRadius": "1100", "LeashRadius": "2600",
            "RangedRange": "1400" if ranged else "0", "RangedDamage": "10" if ranged else "0",
            "RangedCooldownSeconds": "3.0" if ranged else "0", "RangedWindupSeconds": "0.8" if ranged else "0",
            "RangedWeaponMesh": "", "bRangedUnarmed": "True" if ranged else "False",
            "RangedFlashColor": "(R=0.12,G=1.0,B=0.3,A=1.0)" if ranged else NEW_COLUMNS["RangedFlashColor"],
            "SkinMaterial": MAT + material + "." + material}


ROWS = {
    "Enemies.csv": [
        enemy("BogCultist", "Bog Cultist", 16, 430, 28, 390, "SK_Chr_Pirate_Deckhand_01"),
        enemy("HexAcolyte", "Hex Acolyte", 20, 500, 24, 480, "SK_Chr_Female_Pirate_01", ranged=True),
        enemy("BogWraith", "Bog Wraith", 24, 780, 34, 680, "SK_Chr_Skeleton_01", spirit=True),
        enemy("HexMother", "The Hex Mother", 28, 1500, 42, 1400, "SK_Chr_Female_Wench_01", boss=True),
    ],
    "Chests.csv": [{"---": "AltarChest", "DisplayName": "Bog Altar Chest", "Gold": "180", "Tier": "3"}],
}


def mission(key, title, giver, intro, objective, outro, kind, target, count, gold, xp, order,
            require="None", unlock="None", repeat=False, cooldown="0.0", location="(X=0,Y=0,Z=0)"):
    return {"---": key, "Title": title, "Giver": "Ada Kell" if giver == "Ada" else "Mama Oduya",
            "GiverId": giver, "Intro": intro, "Objective": objective, "Outro": outro, "Type": kind,
            "TargetName": target, "TargetCount": str(count), "TargetLocation": location,
            "TargetRadius": "900.0" if kind == "ReachSpot" else "400.0", "RewardGold": str(gold),
            "RewardXP": str(xp), "RewardSkill": "Sailing" if kind == "ReachSpot" else "Melee",
            "bRepeatable": str(repeat), "CooldownSeconds": cooldown, "RequireFlag": require,
            "bAutoComplete": str(kind == "ReachSpot"), "UnlockFlag": unlock, "Order": str(order),
            "RewardWeapon": "None"}


STORY_ROWS = {
    "Npcs.csv": [{"---": "Oduya", "DisplayName": "Mama Oduya, the bog witch", "PromptName": "Mama Oduya",
                  "UnlockFlag": "Story_Hollow", "BrushOff": "The mud has not learned your footsteps. Speak to Ada Kell before you bring your troubles to my door.", "Order": "6"}],
    "Quests.csv": [
        {"---": "DrownedWatch", "DisplayName": "The Drowned Watch", "Description": "The cultists ring bells for a village that cannot answer. Silence five of them.",
         "TargetEnemyRow": "BogCultist", "TargetCount": "5", "RewardGold": "160", "RewardXP": "520", "RewardSkill": "Melee", "bRepeatable": "true", "SortOrder": "7", "RequireFlag": "Story_Hollow"},
        {"---": "CurseKeepers", "DisplayName": "Curse Keepers", "Description": "Two acolytes guard the drowned paths. Break their watch for Oduya.",
         "TargetEnemyRow": "HexAcolyte", "TargetCount": "2", "RewardGold": "180", "RewardXP": "600", "RewardSkill": "Melee", "bRepeatable": "true", "SortOrder": "8", "RequireFlag": "Story_Hollow"},
    ],
    "Missions.csv": [
        mission("R05_TheHollow", "The Hollow", "Ada",
                "The deserters ran to the swamp beyond Wreck Cove. I struck their names from the book. Something worse than the navy took them in. Find Mama Oduya in the stilt village; she remembers who went under.",
                "Land on Mangrove Hollow at the marked beach.", "That is the Hollow. Find Oduya above the mud, and keep clear of the bells.",
                "ReachSpot", "HollowLanding", 1, 110, 420, 24, require="Story_Gallows", unlock="Story_Hollow", location="(X=-11200,Y=9900,Z=-40)"),
        mission("V01_DrownedBells", "Drowned Bells", "Oduya",
                "I knew the hands that cast those bells. Now the cult rings them to call more people into the mud. Take three bell keepers off the paths.",
                "Kill 3 bog cultists.", "Quiet, for a little while. Now we can hear what lies beneath it.",
                "KillRow", "BogCultist", 3, 130, 500, 25, require="Story_Hollow"),
        mission("V02_TheAltar", "The Altar", "Oduya",
                "There is a chest at the stone altar. Its contents belonged to the village before the Mother claimed them. Open it and bring the old promises back into daylight.",
                "Open the Bog Altar Chest.", "The village has not forgotten its own. Neither have I.",
                "OpenChest", "AltarChest", 1, 100, 460, 26),
        mission("V03_Acolytes", "The Green Hands", "Oduya",
                "Her acolytes tie sickness to a living hand. Watch for the green light, and give them no straight path to your heart. Two of them keep the outer watch.",
                "Kill 2 hex acolytes.", "Two hands empty. One voice remains at the altar.",
                "KillRow", "HexAcolyte", 2, 180, 650, 27),
        mission("V04_HexMother", "The Hex Mother", "Oduya",
                "She calls herself the Mother. Ask the empty houses what she raised here. End her watch at the altar and let the drowned village sleep.",
                "Kill the Hex Mother at the bog altar.", "The bells are only bells again. You have given this place a night of its own.",
                "KillBoss", "HexMother", 1, 450, 1500, 28, unlock="Story_Hollowed"),
        mission("W11_BogSweep", "Bog Sweep", "Oduya",
                "The bell keepers always find their way back. Three fewer on the paths will buy the village another quiet evening.",
                "Kill 3 bog cultists for Oduya.", "That will do. Come back when the bells start again.",
                "KillRow", "BogCultist", 3, 65, 220, 111, repeat=True, cooldown="180.0"),
        mission("W12_LampOil", "Lamp Oil", "Oduya",
                "Forty gold keeps the village lamps burning. I will not have the children cross those boards in the dark.",
                "Pay Oduya 40 gold for lamp oil.", "There. A light in every window that still has one.",
                "Pay", "None", 40, 25, 180, 112, repeat=True, cooldown="300.0"),
    ],
}


def main():
    check = "--check" in sys.argv
    selected = dict(ROWS)
    if "--story" in sys.argv:
        selected.update(STORY_ROWS)
    for name, wanted in selected.items():
        path = DATA / name
        with path.open(encoding="utf-8-sig", newline="") as handle:
            reader = csv.DictReader(handle)
            columns, existing = list(reader.fieldnames), list(reader)
        keys = [row["---"] for row in existing]
        if len(keys) != len(set(keys)) or any(None in row for row in existing):
            raise ValueError("duplicate keys or extra cells in " + name)
        additions = [col for col in NEW_COLUMNS if col not in columns] if name == "Enemies.csv" else []
        if check and additions:
            raise ValueError("missing columns " + str(additions))
        columns.extend(additions)
        for row in existing:
            for col in additions:
                row[col] = NEW_COLUMNS[col]
        present = {row["---"]: row for row in existing}
        missing = []
        for row in wanted:
            if row["---"] in present:
                if any(present[row["---"]].get(col) != val for col, val in row.items()):
                    raise ValueError("conflicting row " + row["---"])
            else:
                missing.append(row)
        if check and missing:
            raise ValueError("missing rows " + str([row["---"] for row in missing]))
        if not check and (missing or additions):
            if additions:
                with path.open("w", encoding="utf-8", newline="") as handle:
                    writer = csv.DictWriter(handle, fieldnames=columns, lineterminator="\r\n")
                    writer.writeheader()
                    writer.writerows(existing + missing)
            else:
                # Appending retains the exact bytes and intentional quoting of all old rows.
                buf = io.StringIO(newline="")
                csv.DictWriter(buf, fieldnames=columns, lineterminator="\r\n").writerows(missing)
                with path.open("ab") as handle:
                    handle.write(buf.getvalue().encode("utf-8"))
        print("WRECKTIDE-ISLE3ROWS %s rows=%s check=%d" % (name, [row["---"] for row in wanted], check))
    print("WRECKTIDE-ISLE3ROWS RESULT=OK")


if __name__ == "__main__":
    main()
