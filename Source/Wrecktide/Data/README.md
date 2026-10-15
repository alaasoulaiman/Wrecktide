SHIPPED BEHAVIOUR: these CSVs are NOT imported into the Content Browser and no DataTable uassets
exist. UWrecktideDataSubsystem::LoadCsvTable reads each file off disk at runtime with
FFileHelper::LoadFileToString, builds a transient UDataTable with NewObject, and populates it via
CreateTableFromCSVString. Edit the .csv files directly; there is no asset to re-import.
Content/Data/Weapons.csv uses FWeaponTableRow.
Content/Data/Enemies.csv uses FEnemyTableRow. Its SkinMesh column (added 2026-09-02) is a
  TSoftObjectPtr<USkeletalMesh> written as the object path, /Game/.../SK_Chr_X.SK_Chr_X; an empty
  cell keeps the enemy class default skin. ACombatEnemy::BeginPlay loads it synchronously before the
  cosmetic retarget rig hooks up. Spawners choose the row through ACombatEnemySpawner::EnemyRowOverride.
Content/Data/Loot.csv uses FLootTableRow.
Content/Data/Quests.csv uses FQuestTableRow (bounty board, added 2026-09-04). TargetEnemyRow is an
  Enemies.csv row name; None counts any enemy. RewardSkill is an EWrecktideSkill name. Bounty STATE
  is not in this file: it lives in UProgressComponent::QuestFlags as Q_<Row>_State and Q_<Row>_Kills,
  written only through Systems/WrecktideQuestLogic. RequireFlag (added 2026-09-07) is a QuestFlags
  name that must already be set before ANY board lists or accepts the row; None, an empty cell or no
  column at all means ungated. The one rule is WrecktideQuestLogic::IsQuestOffered.
Keep the first CSV column as the row name; remaining headers map to UPROPERTY names.

PACKAGING (2026-09-02): the load path uses FPaths::ProjectContentDir(), which a cooked build only
satisfies if the loose CSVs are staged. Config/DefaultGame.ini now carries
+DirectoriesToAlwaysStageAsUFS=(Path="Data") so the three files ride in the pak, plus
+DirectoriesToAlwaysCook for the Synty character folder because Enemies.csv references most of those
skins by soft path only. Until the first package run proves both (look for WRECKTIDE-FUNGATE-DATA errors
in the packaged log; none means the tables loaded), treat a cooked build's tables as UNVERIFIED. If the
DataTable-uasset route is ever taken instead, change the code and this file in the SAME commit.
