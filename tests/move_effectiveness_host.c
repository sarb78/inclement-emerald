// The harness uses the game's actual headers, structures and move table.
// test_move_effectiveness.py inserts unmodified production definitions below.
#include <stdio.h>
#include <stdlib.h>
#include "global.h"
#include "pokemon.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_ai_util.h"
#include "event_data.h"
#include "item.h"
#include "random.h"
#include "util.h"
#include "constants/abilities.h"
#include "constants/battle_move_effects.h"
#include "constants/battle_string_ids.h"
#include "constants/hold_effects.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "src/data/battle_moves.h"

struct BattlePokemon gBattleMons[MAX_BATTLERS_COUNT];
struct DisableStruct gDisableStructs[MAX_BATTLERS_COUNT];
struct SpecialStatus gSpecialStatuses[MAX_BATTLERS_COUNT];
struct ProtectStruct gProtectStructs[MAX_BATTLERS_COUNT];
struct BattleEnigmaBerry gEnigmaBerries[MAX_BATTLERS_COUNT];
static struct BattleStruct sBattleStruct;
struct BattleStruct *gBattleStruct = &sBattleStruct;
u8 gBattlerAttacker, gBattlerTarget, gBattlersCount, gCurrentTurnActionNumber;
u8 gPotentialItemEffectBattler, gAbsentBattlerFlags;
u8 gBattlerPositions[MAX_BATTLERS_COUNT], gBattlerByTurnOrder[MAX_BATTLERS_COUNT];
u8 gActionsByTurnOrder[MAX_BATTLERS_COUNT], gBattleCommunication[BATTLE_COMMUNICATION_ENTRIES_COUNT];
u16 gCurrentMove, gMoveResultFlags, gLastUsedAbility, gBattleWeather;
u16 gLastLandedMoves[MAX_BATTLERS_COUNT];
u32 gBattleTypeFlags, gFieldStatuses, gStatuses3[MAX_BATTLERS_COUNT], gStatuses4[MAX_BATTLERS_COUNT];
u32 gRngValue, gRng2Value;
const u32 gBitTable[] = {1, 2, 4, 8};
static bool8 sInverse;
static unsigned sKnowledgeWrites;
static unsigned sChecks;

// External metadata adapters. All effectiveness/dynamic-type logic is production C.
u8 ItemId_GetHoldEffect(u16 item)
{
    switch (item)
    {
    case ITEM_AIR_BALLOON: return HOLD_EFFECT_AIR_BALLOON;
    case ITEM_RING_TARGET: return HOLD_EFFECT_RING_TARGET;
    case ITEM_IRON_BALL: return HOLD_EFFECT_IRON_BALL;
    case ITEM_SAFETY_GOGGLES: return HOLD_EFFECT_SAFETY_GOGGLES;
    case ITEM_UTILITY_UMBRELLA: return HOLD_EFFECT_UTILITY_UMBRELLA;
    case ITEM_FIRE_GEM: return HOLD_EFFECT_GEMS;
    case ITEM_FLAME_PLATE: return HOLD_EFFECT_PLATE;
    default: return HOLD_EFFECT_NONE;
    }
}
u8 ItemId_GetHoldEffectParam(u16 item) { (void)item; return 30; }
u8 ItemId_GetSecondaryId(u16 item)
{
    if (item == ITEM_FIRE_GEM || item == ITEM_FLAME_PLATE)
        return TYPE_FIRE;
    return TYPE_NORMAL;
}
u8 ItemId_GetPocket(u16 item)
{
    return item >= ITEM_CHERI_BERRY && item <= ITEM_MARANGA_BERRY ? POCKET_BERRIES : POCKET_ITEMS;
}
bool8 FlagGet(u16 flag) { (void)flag; return sInverse; }
void RecordAbilityBattle(u8 battler, u16 ability) { (void)battler; (void)ability; sKnowledgeWrites++; }
void RecordItemEffectBattle(u8 battler, u8 effect) { (void)battler; (void)effect; sKnowledgeWrites++; }

/* PRODUCTION CODE */

// Snapshot every mutable global reachable from the extracted calculations,
// including complete game structures, result/knowledge state and both RNGs.
struct Snapshot
{
    struct BattlePokemon mons[MAX_BATTLERS_COUNT];
    struct DisableStruct disable[MAX_BATTLERS_COUNT];
    struct SpecialStatus special[MAX_BATTLERS_COUNT];
    struct ProtectStruct protect[MAX_BATTLERS_COUNT];
    struct BattleEnigmaBerry berries[MAX_BATTLERS_COUNT];
    struct BattleStruct battle;
    u8 bytes[8 + MAX_BATTLERS_COUNT * 3 + BATTLE_COMMUNICATION_ENTRIES_COUNT];
    u16 words[4 + MAX_BATTLERS_COUNT];
    u32 longs[4 + MAX_BATTLERS_COUNT * 2];
    bool8 inverse;
    unsigned knowledge;
};

static struct Snapshot Snapshot(void)
{
    struct Snapshot state;
    u8 *bytes = state.bytes;
    u16 *words = state.words;
    u32 *longs = state.longs;
    memset(&state, 0, sizeof(state));
    memcpy(state.mons, gBattleMons, sizeof(state.mons));
    memcpy(state.disable, gDisableStructs, sizeof(state.disable));
    memcpy(state.special, gSpecialStatuses, sizeof(state.special));
    memcpy(state.protect, gProtectStructs, sizeof(state.protect));
    memcpy(state.berries, gEnigmaBerries, sizeof(state.berries));
    memcpy(&state.battle, gBattleStruct, sizeof(state.battle));
    *bytes++ = gBattlerAttacker; *bytes++ = gBattlerTarget;
    *bytes++ = gBattlersCount; *bytes++ = gCurrentTurnActionNumber;
    *bytes++ = gPotentialItemEffectBattler; *bytes++ = gAbsentBattlerFlags;
    memcpy(bytes, gBattlerPositions, sizeof(gBattlerPositions)); bytes += sizeof(gBattlerPositions);
    memcpy(bytes, gBattlerByTurnOrder, sizeof(gBattlerByTurnOrder)); bytes += sizeof(gBattlerByTurnOrder);
    memcpy(bytes, gActionsByTurnOrder, sizeof(gActionsByTurnOrder)); bytes += sizeof(gActionsByTurnOrder);
    memcpy(bytes, gBattleCommunication, sizeof(gBattleCommunication));
    *words++ = gCurrentMove; *words++ = gMoveResultFlags;
    *words++ = gLastUsedAbility; *words++ = gBattleWeather;
    memcpy(words, gLastLandedMoves, sizeof(gLastLandedMoves));
    *longs++ = gBattleTypeFlags; *longs++ = gFieldStatuses;
    *longs++ = gRngValue; *longs++ = gRng2Value;
    memcpy(longs, gStatuses3, sizeof(gStatuses3)); longs += MAX_BATTLERS_COUNT;
    memcpy(longs, gStatuses4, sizeof(gStatuses4));
    state.inverse = sInverse;
    state.knowledge = sKnowledgeWrites;
    return state;
}

static void Reset(void)
{
    unsigned i;
    memset(gBattleMons, 0, sizeof(gBattleMons));
    memset(gDisableStructs, 0, sizeof(gDisableStructs));
    memset(gSpecialStatuses, 0, sizeof(gSpecialStatuses));
    memset(gProtectStructs, 0, sizeof(gProtectStructs));
    memset(gEnigmaBerries, 0, sizeof(gEnigmaBerries));
    memset(gBattleStruct, 0, sizeof(*gBattleStruct));
    memset(gStatuses3, 0, sizeof(gStatuses3));
    memset(gStatuses4, 0, sizeof(gStatuses4));
    memset(gBattleCommunication, 0xA5, sizeof(gBattleCommunication));
    for (i = 0; i < MAX_BATTLERS_COUNT; i++)
    {
        gBattleMons[i].hp = gBattleMons[i].maxHP = 100;
        gBattleMons[i].type1 = gBattleMons[i].type2 = TYPE_NORMAL;
        gBattleMons[i].type3 = TYPE_MYSTERY;
        gBattlerPositions[i] = i;
        gBattlerByTurnOrder[i] = MAX_BATTLERS_COUNT - i - 1;
        gActionsByTurnOrder[i] = B_ACTION_SWITCH;
        gLastLandedMoves[i] = MOVE_TACKLE;
        gBattleStruct->ateBoost[i] = TRUE;
        gSpecialStatuses[i].gemBoost = TRUE;
        gSpecialStatuses[i].gemParam = 19;
        gProtectStructs[i].pranksterElevated = TRUE;
    }
    gBattlersCount = 2;
    gBattlerAttacker = 1; gBattlerTarget = 0;
    gCurrentMove = MOVE_SWORDS_DANCE;
    gCurrentTurnActionNumber = 3;
    gPotentialItemEffectBattler = 3;
    gBattleStruct->dynamicMoveType = TYPE_POISON | 0x80;
    gMoveResultFlags = 0xA5A5;
    gLastUsedAbility = ABILITY_ILLUSION;
    gBattleWeather = 0; gFieldStatuses = 0;
    gBattleTypeFlags = 0; gAbsentBattlerFlags = 0;
    gRngValue = 0x12345678; gRng2Value = 0xABCDEF01;
    sKnowledgeWrites = 0; sInverse = FALSE;
}

static void Check(const char *name, u16 move, u8 attacker, u8 target,
                  bool32 selecting, u8 expected, u8 expectedType)
{
    struct Snapshot before = Snapshot(), after;
    u8 type;
    u8 category = GetMoveEffectivenessPreview(move, attacker, target, selecting, &type);
    after = Snapshot();
    if (memcmp(&before, &after, sizeof(before)) || category != expected || type != expectedType)
    {
        fprintf(stderr, "%s: category=%u expected=%u type=%u expected=%u stateChanged=%d\n",
                name, category, expected, type, expectedType, memcmp(&before, &after, sizeof(before)) != 0);
        exit(1);
    }
    sChecks++;
}
#define CHECK(name, move, category, type) Check(name, move, 0, 1, FALSE, category, type)

int main(void)
{
    unsigned i;
    static const u16 electricAbsorbers[] = {ABILITY_VOLT_ABSORB, ABILITY_MOTOR_DRIVE, ABILITY_LIGHTNING_ROD};
    static const u16 waterAbsorbers[] = {ABILITY_WATER_ABSORB, ABILITY_DRY_SKIN, ABILITY_STORM_DRAIN};
    Reset(); CHECK("neutral", MOVE_TACKLE, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_GRASS;
    CHECK("super effective", MOVE_EMBER, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_FIRE);
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_WATER;
    CHECK("resisted", MOVE_EMBER, MOVE_PREVIEW_NOT_VERY_EFFECTIVE, TYPE_FIRE);
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_GHOST;
    CHECK("immune", MOVE_TACKLE, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    CHECK("status neutral", MOVE_GROWL, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    gBattleMons[0].ability = ABILITY_SCRAPPY;
    CHECK("Scrappy", MOVE_TACKLE, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    gBattleMons[0].ability = ABILITY_NONE;
    gBattleMons[1].item = ITEM_RING_TARGET;
    CHECK("Ring Target", MOVE_TACKLE, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    gFieldStatuses = STATUS_FIELD_MAGIC_ROOM;
    CHECK("Magic Room", MOVE_TACKLE, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    Reset(); gBattleMons[1].type1 = TYPE_WATER; gBattleMons[1].type2 = TYPE_FLYING;
    CHECK("4x", MOVE_THUNDERBOLT, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_ELECTRIC);
    CHECK("Freeze-Dry", MOVE_FREEZE_DRY, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_ICE);
    gBattleMons[1].type1 = TYPE_DRAGON;
    CHECK("quarter resistance", MOVE_VINE_WHIP, MOVE_PREVIEW_NOT_VERY_EFFECTIVE, TYPE_GRASS);
    Reset(); gBattleMons[1].ability = ABILITY_LEVITATE;
    CHECK("Levitate", MOVE_EARTHQUAKE, MOVE_PREVIEW_IMMUNE, TYPE_GROUND);
    gBattleMons[0].ability = ABILITY_MOLD_BREAKER;
    CHECK("Mold Breaker", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    gBattleMons[0].ability = ABILITY_NONE; gStatuses3[1] = STATUS3_GASTRO_ACID;
    CHECK("Gastro Acid", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    gStatuses3[1] = 0; gBattleMons[0].ability = ABILITY_NEUTRALIZING_GAS;
    CHECK("Neutralizing Gas", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    Reset(); gBattleMons[1].ability = ABILITY_WONDER_GUARD;
    CHECK("move ignores abilities", MOVE_SUNSTEEL_STRIKE, MOVE_PREVIEW_NEUTRAL, TYPE_STEEL);
    Reset(); gBattleMons[1].item = ITEM_AIR_BALLOON;
    CHECK("Air Balloon", MOVE_EARTHQUAKE, MOVE_PREVIEW_IMMUNE, TYPE_GROUND);
    gFieldStatuses = STATUS_FIELD_GRAVITY;
    CHECK("Gravity", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    gFieldStatuses = 0; gBattleMons[1].ability = ABILITY_KLUTZ;
    CHECK("Klutz", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    Reset(); gStatuses3[1] = STATUS3_MAGNET_RISE;
    CHECK("Magnet Rise", MOVE_EARTHQUAKE, MOVE_PREVIEW_IMMUNE, TYPE_GROUND);
    Reset(); gBattleMons[1].ability = ABILITY_WONDER_GUARD;
    CHECK("Wonder Guard", MOVE_EMBER, MOVE_PREVIEW_IMMUNE, TYPE_FIRE);
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_GRASS;
    CHECK("Wonder Guard weakness", MOVE_EMBER, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_FIRE);
    for (i = 0; i < ARRAY_COUNT(electricAbsorbers); i++)
    {
        Reset(); gBattleMons[1].ability = electricAbsorbers[i];
        CHECK("electric absorber", MOVE_THUNDERBOLT, MOVE_PREVIEW_IMMUNE, TYPE_ELECTRIC);
        gBattleMons[0].ability = ABILITY_MOLD_BREAKER;
        CHECK("ignored electric absorber", MOVE_THUNDERBOLT, MOVE_PREVIEW_NEUTRAL, TYPE_ELECTRIC);
    }
    for (i = 0; i < ARRAY_COUNT(waterAbsorbers); i++)
    {
        Reset(); gBattleMons[1].ability = waterAbsorbers[i];
        CHECK("water absorber", MOVE_WATER_GUN, MOVE_PREVIEW_IMMUNE, TYPE_WATER);
    }
    Reset(); gBattleMons[1].ability = ABILITY_SAP_SIPPER;
    CHECK("Sap Sipper", MOVE_VINE_WHIP, MOVE_PREVIEW_IMMUNE, TYPE_GRASS);
    gBattleMons[1].ability = ABILITY_FLASH_FIRE;
    CHECK("Flash Fire", MOVE_EMBER, MOVE_PREVIEW_IMMUNE, TYPE_FIRE);
    gBattleMons[1].status1 = STATUS1_FREEZE;
    CHECK("frozen Flash Fire", MOVE_EMBER, MOVE_PREVIEW_IMMUNE, TYPE_FIRE);
    Reset(); gBattleMons[1].ability = ABILITY_SOUNDPROOF;
    CHECK("Soundproof", MOVE_HYPER_VOICE, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    gBattleMons[1].ability = ABILITY_BULLETPROOF;
    CHECK("Bulletproof", MOVE_SHADOW_BALL, MOVE_PREVIEW_IMMUNE, TYPE_GHOST);
    Reset(); gBattleMons[1].ability = ABILITY_DAZZLING;
    CHECK("Dazzling", MOVE_QUICK_ATTACK, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    gBattleMons[1].ability = ABILITY_QUEENLY_MAJESTY;
    CHECK("Queenly Majesty", MOVE_QUICK_ATTACK, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    Reset(); gFieldStatuses = STATUS_FIELD_PSYCHIC_TERRAIN;
    CHECK("Psychic Terrain", MOVE_QUICK_ATTACK, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_FLYING;
    CHECK("ungrounded terrain target", MOVE_QUICK_ATTACK, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    Reset(); gBattleWeather = WEATHER_RAIN_PRIMAL;
    CHECK("primal rain", MOVE_EMBER, MOVE_PREVIEW_IMMUNE, TYPE_FIRE);
    gBattleMons[0].ability = ABILITY_AIR_LOCK;
    CHECK("Air Lock weather", MOVE_EMBER, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    Reset(); gBattleWeather = WEATHER_SUN_PRIMAL;
    CHECK("primal sun", MOVE_WATER_GUN, MOVE_PREVIEW_IMMUNE, TYPE_WATER);
    Reset(); gBattleMons[1].ability = ABILITY_DAMP;
    CHECK("Damp", MOVE_EXPLOSION, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    Reset(); gBattleWeather = WEATHER_RAIN_TEMPORARY;
    CHECK("Weather Ball", MOVE_WEATHER_BALL, MOVE_PREVIEW_NEUTRAL, TYPE_WATER);
    gBattleMons[0].item = ITEM_UTILITY_UMBRELLA;
    CHECK("umbrella Weather Ball", MOVE_WEATHER_BALL, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    Reset(); gBattleMons[0].ability = ABILITY_PIXILATE;
    CHECK("Pixilate type", MOVE_TACKLE, MOVE_PREVIEW_NEUTRAL, TYPE_FAIRY);
    Reset(); gBattleMons[0].ability = ABILITY_LIQUID_VOICE;
    CHECK("Liquid Voice type", MOVE_HYPER_VOICE, MOVE_PREVIEW_NEUTRAL, TYPE_WATER);
    Reset(); gBattleMons[0].ability = ABILITY_SAND_SONG;
    CHECK("Sand Song type", MOVE_HYPER_VOICE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    Reset(); gBattleMons[0].item = ITEM_FIRE_GEM;
    CHECK("gem state restored", MOVE_EMBER, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    gBattleMons[0].item = ITEM_FLAME_PLATE;
    CHECK("Judgment type", MOVE_JUDGMENT, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    Reset(); gBattleMons[0].item = ITEM_CHERI_BERRY;
    CHECK("Natural Gift type", MOVE_NATURAL_GIFT, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    Reset(); gBattleMons[0].hpIV = gBattleMons[0].attackIV = gBattleMons[0].defenseIV = 31;
    gBattleMons[0].speedIV = gBattleMons[0].spAttackIV = gBattleMons[0].spDefenseIV = 31;
    CHECK("Hidden Power type", MOVE_HIDDEN_POWER, MOVE_PREVIEW_NEUTRAL, TYPE_DARK);
    Reset(); gFieldStatuses = STATUS_FIELD_ELECTRIC_TERRAIN;
    CHECK("Terrain Pulse type", MOVE_TERRAIN_PULSE, MOVE_PREVIEW_NEUTRAL, TYPE_ELECTRIC);
    Reset(); CHECK("Struggle clears stale dynamic type", MOVE_STRUGGLE, MOVE_PREVIEW_NEUTRAL, TYPE_NORMAL);
    Reset(); gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_WATER; sInverse = TRUE;
    CHECK("inverse chart", MOVE_EMBER, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_FIRE);
    Reset(); gBattlersCount = 4;
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_GRASS;
    gBattleMons[3].type1 = TYPE_GRASS; gBattleMons[3].type2 = TYPE_BUG;
    CHECK("spread agrees 2x and 4x", MOVE_HEAT_WAVE, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_FIRE);
    gBattleMons[3].type1 = gBattleMons[3].type2 = TYPE_WATER;
    CHECK("mixed spread", MOVE_HEAT_WAVE, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    Check("selected left target", MOVE_EMBER, 0, 1, TRUE, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_FIRE);
    Check("selected right target", MOVE_EMBER, 0, 3, TRUE, MOVE_PREVIEW_NOT_VERY_EFFECTIVE, TYPE_FIRE);
    gAbsentBattlerFlags = gBitTable[1];
    CHECK("absent left fallback", MOVE_EMBER, MOVE_PREVIEW_NOT_VERY_EFFECTIVE, TYPE_FIRE);
    CHECK("absent spread target", MOVE_HEAT_WAVE, MOVE_PREVIEW_NOT_VERY_EFFECTIVE, TYPE_FIRE);
    Reset(); gBattlersCount = 4;
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_FIRE;
    gBattleMons[3].type1 = gBattleMons[3].type2 = TYPE_FIRE;
    gBattleMons[2].type1 = gBattleMons[2].type2 = TYPE_NORMAL;
    CHECK("spread includes ally", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    gBattleMons[2].type1 = gBattleMons[2].type2 = TYPE_FIRE;
    CHECK("spread ally agrees", MOVE_EARTHQUAKE, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_GROUND);
    gBattleMons[2].ability = ABILITY_TELEPATHY;
    CHECK("Telepathy ally makes mixed", MOVE_EARTHQUAKE, MOVE_PREVIEW_NEUTRAL, TYPE_GROUND);
    gBattleMons[2].ability = ABILITY_NONE; gBattleMons[3].ability = ABILITY_DAZZLING;
    Check("partner Dazzling", MOVE_QUICK_ATTACK, 0, 1, TRUE, MOVE_PREVIEW_IMMUNE, TYPE_NORMAL);
    Reset(); gBattleMons[1].hp = 0;
    CHECK("no living target", MOVE_EMBER, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    Reset(); gBattlersCount = 4;
    gBattlerPositions[1] = gBattlerPositions[3] = B_POSITION_PLAYER_LEFT;
    CHECK("no mapped opposing positions", MOVE_EMBER, MOVE_PREVIEW_NEUTRAL, TYPE_FIRE);
    Reset(); gBattlersCount = 4;
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_FAIRY;
    CHECK("random target mixed", MOVE_OUTRAGE, MOVE_PREVIEW_NEUTRAL, TYPE_DRAGON);
    gBattleMons[3].type1 = gBattleMons[3].type2 = TYPE_FAIRY;
    CHECK("random target immune", MOVE_OUTRAGE, MOVE_PREVIEW_IMMUNE, TYPE_DRAGON);
    Reset(); gBattlersCount = 4;
    gBattleMons[1].type1 = gBattleMons[1].type2 = TYPE_GRASS;
    Check("second player battler context", MOVE_EMBER, 2, 1, TRUE, MOVE_PREVIEW_SUPER_EFFECTIVE, TYPE_FIRE);
    // Exercise transformed/link position mappings, rather than assuming battler
    // IDs equal their on-screen positions when choosing the default opponent.
    gBattlerPositions[1] = B_POSITION_OPPONENT_RIGHT;
    gBattlerPositions[3] = B_POSITION_OPPONENT_LEFT;
    gBattleMons[3].type1 = gBattleMons[3].type2 = TYPE_WATER;
    CHECK("position-mapped default opponent", MOVE_EMBER, MOVE_PREVIEW_NOT_VERY_EFFECTIVE, TYPE_FIRE);
    Check("invalid move", MOVES_COUNT, 0, 1, FALSE, MOVE_PREVIEW_NEUTRAL, TYPE_MYSTERY);
    Check("invalid attacker", MOVE_EMBER, 4, 1, FALSE, MOVE_PREVIEW_NEUTRAL, TYPE_MYSTERY);
    printf("PASS: %u production-C effectiveness and complete-state preservation checks\n", sChecks);
    return 0;
}
