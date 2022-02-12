#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_scripts.h"
#include "battle_ai_util.h"
#include "main.h"
#include "menu.h"
#include "menu_helpers.h"
#include "scanline_effect.h"
#include "palette.h"
#include "party_menu.h"
#include "sprite.h"
#include "item.h"
#include "task.h"
#include "bg.h"
#include "gpu_regs.h"
#include "window.h"
#include "text.h"
#include "random.h"
#include "trainer_pokemon_sprites.h"
#include "malloc.h"
#include "string_util.h"
#include "util.h"
#include "data.h"
#include "reshow_battle_screen.h"
#include "constants/abilities.h"
#include "constants/battle_string_ids.h"

/* 

 * If the player's Pokémon currently in battle has the Ability Synchronize when the ally is called, there is a 50% chance that the ally will have the same Nature as that Pokémon (however, due to a bug, if the Pokémon with Synchronize was not originally in the first slot of the party and moved to it during the battle, the called Pokémon will synchronize to the Nature of the Pokémon that was originally in the first slot, instead of the Nature of the Pokémon with Synchronize that replaced it). 
 

  * Allies:
    * Most wild Pokémon only call for allies of the same species as themselves
    * https://bulbapedia.bulbagarden.net/wiki/SOS_Battle#SOS_Battle_allies
    * Weather-dependent allies
*/

// Functions
bool32 IsSosBattle(void)
{
    return (gBattleTypeFlags & BATTLE_TYPE_SOS);
}

bool32 IsSoSAllyPresent(void)
{
    return (gBattleStruct->sos.allyPresent);
}

bool32 IsDoubleBattleOnSide(u8 battler)
{
    if (IsSosBattle())
    {
        if (IsSoSAllyPresent() && GetBattlerSide(battler) == B_SIDE_OPPONENT)
            return TRUE;
        return FALSE;
    }
    
    return (gBattleTypeFlags & BATTLE_TYPE_DOUBLE);
}

static void MainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

// Turns screen black and returns to field via ReshowBattleScreenAfterMenu
void CB2_SosCall(void)
{
    switch (gMain.state)
    {
    default:
    case 0:
        SetVBlankCallback(NULL);
        gMain.state++;
        break;
    case 1:
        ResetVramOamAndBgCntRegs();
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        ResetBgsAndClearDma3BusyFlags(0);
        ResetAllBgsCoordinates();
        FreeAllWindowBuffers();
        DeactivateAllTextPrinters();
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        ScanlineEffect_Stop();
        ResetTasks();
        ResetSpriteData();
        gMain.state++;
        break;
    case 3:
        BeginNormalPaletteFade(-1, 0, 0x10, 0, 0);
        SetVBlankCallback(VBlankCB);
        SetMainCallback2(ReshowBattleScreenAfterMenu);
        return;
    }
}

u32 GetSosBattleShinyRolls(void)
{
    /*  Chain Length     Shiny Rolls
        5 	            1
        10 	            1
        11 	            5
        20 	            5
        21 	            9
        30 	            9
        31 	            13        */
        
    u8 calls = gBattleStruct->sos.calls;
    
    if (calls < 5)
        return 0;
    else if (calls < 11)
        return 1;
    else if (calls < 21)
        return 5;
    else if (calls < 31)
        return 9;
    
    return 13;  // max call rate
}

static bool32 IsNotUniqueIV(u8 index, u8 *stat)
{
    u32 i;
    
    for (i = 0; i < NUM_STATS; i++)
    {
        if (index == i)
            continue;
        if (stat[index] == stat[i])
            return TRUE;
    }
    
    return FALSE;
}

static void SetSosAllyIVs(struct Pokemon *mon)
{
    u8 stat[NUM_STATS] = {NUM_STATS};
    u8 calls = gBattleStruct->sos.calls;
    u8 nPerfect = 0;
    u32 i;
    u32 perfectIv = MAX_PER_STAT_IVS;
    
    /*  Chain length 	Number of Perfect IVs
        5 	            1
        10 	            2
        11 	            2
        20 	            3
        21 	            3
        30 	            4
        31 	            4       */
        
    if (calls < 5)
        return;
    
    if (calls < 10)
        nPerfect = 1;
    else if (calls < 20)
        nPerfect = 2;
    else if (calls < 30)
        nPerfect = 3;
    else
        nPerfect = 4;
    
    for (i = 0; i < nPerfect; i++)
    {
        do {
            stat[i] = Random() % NUM_STATS;
        } while (IsNotUniqueIV(i, stat));
        
        if (stat[i] != NUM_STATS)
            SetMonData(mon, MON_DATA_HP_IV + stat[i], &perfectIv);
    }
}

static void CreateSoSAlly(u8 caller, u8 ally)
{
    struct Pokemon *mon = &gEnemyParty[gBattlerPartyIndexes[ally]];
    u16 species = SPECIES_GROWLITHE;    // TODO gBattleMons[caller].species; 
    u8 level = gBattleMons[caller].level - 5 + Random() % 10;   // level += 5   TODO better system
    u32 val;
    
    CreateMon(mon, species, level, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
    
    #ifdef POKEMON_EXPANSION
    // Try give hidden ability - 5% increase every 10 calls
    if (gBaseStats[species].abilities[2] != ABILITY_NONE
      && (Random() % 100 < ((gBattleStruct->sos.calls / 10) * 5)))
    {
        val = 2;
        SetMonData(mon, MON_DATA_ABILITY_NUM, &val);
    }
    #endif
    
    // Set number of perfect IVs
    SetSosAllyIVs(mon);
}

static u32 GetCallRate(u8 caller)
{
    /* Base call rates determine probabilty of calling for help
     * https://bulbapedia.bulbagarden.net/wiki/List_of_Pok%C3%A9mon_by_call_rate */
    u16 callRate = 100; // TODO base call rate
    u8 hpPercent = GetHealthPercentage(caller);
    
    if (callRate == 0)
        return callRate;    // Never call for help
    
    // Totem mons always call after first turn
    if (gBattleStruct->sos.totemBattle && gBattleResults.battleTurnCounter == 0)
        return 100;
    
    // 3x if the calling Pokémon's remaining HP is between 20% and 50% of its maximum HP
    if (hpPercent >= 20 && hpPercent <= 50)
        callRate *= 3;
    else if (hpPercent < 20)
        callRate *= 5;  // 5x if the calling Pokémon's remaining HP is below 20%
    
    // 2x if an Adrenaline Orb has been used in the battle (one per battle)
    if (gBattleStruct->sos.usedAdrenalineOrb)
        callRate *= 2;
    
    return callRate;
}

static u16 GetAnswerRate(u8 caller, u32 callRate)
{
    u16 answerRate = 4 * callRate;  // 4x the call rate by default
    
    // Totem mon calls always answered -> assumes totem mon always on left
    if (gBattleStruct->sos.totemBattle
      && caller == B_POSITION_OPPONENT_LEFT)
        return 100;
    
    // 1.2x if the player's leading Pokémon has Intimidate, Unnerve or Pressure.
    if (GetBattlerAbility(B_POSITION_PLAYER_LEFT) == ABILITY_INTIMIDATE
      || GetBattlerAbility(B_POSITION_PLAYER_LEFT) == ABILITY_PRESSURE
      || IsUnnerveAbilityOnOpposingSide(caller))
        MulModifier(&answerRate, UQ_4_12(1.2));
    
    // 1.5x if the wild Pokémon called for help on the previous turn and is calling again consecutively, regardless of whether the previous call was answered or not.
    if (gBattleStruct->sos.lastCallBattler == caller)
        MulModifier(&answerRate, UQ_4_12(1.5));
    
    // 3x if the Pokémon's most recent call for help was not answered.
    if (gBattleStruct->sos.lastCallFailed)
    {
        gBattleStruct->sos.lastCallFailed = FALSE;
        answerRate *= 3;
    }
    
    /* 2x if the calling Pokémon was hit by, and survived, a super effective move earlier in the same turn that it makes the call.
     * Whether any other Pokémon was hit by a super effective move, and possibly fainted as a result, does not affect eligibility for this factor.           */
    if (gSpecialStatuses[caller].hitBySuperEffective)
        answerRate *= 2;
    
    return answerRate;
}

bool32 TryInitSosCall(void)
{
    u8 caller, ally;
    u16 callRate, answerRate;
    
    gBattleStruct->sos.triedToCallAlly = TRUE;
        
    if (!IsSosBattle())
        return FALSE;
    if (IsSoSAllyPresent())
        return FALSE;
    if (IS_WHOLE_SIDE_ALIVE(B_POSITION_OPPONENT_LEFT))
        return FALSE;
    if (gBattleOutcome != 0)
        return FALSE;   // battle will end
    
    // In USUM, wild Pokémon will only call for help once per battle unless an Adrenaline Orb is used
    #if B_USUM_SOS_CALL_LIMIT == TRUE
    if (gBattleStruct->sos.calls >= 1 && !gBattleStruct->sos.usedAdrenalineOrb)
        return FALSE;
    #endif
    
    if (gBattleMons[B_POSITION_OPPONENT_RIGHT].hp == 0)
        caller = B_POSITION_OPPONENT_LEFT;  // Right flank dead -> Left calls for help
    else if (gBattleMons[B_POSITION_OPPONENT_LEFT].hp == 0)
        caller = B_POSITION_OPPONENT_RIGHT; // Left flank dead -> right calls for help
    else
        return FALSE;   // failsafe, both wild mons are dead -> battle should end
    
    if (gBattleMons[caller].status1 & STATUS1_ANY || gStatuses3[caller] & STATUS3_SEMI_INVULNERABLE)
        return FALSE;
    
    ally = caller ^ BIT_FLANK;
    callRate = GetCallRate(caller);
    if (Random() % 100 > callRate)
        return FALSE;   // Mon won't try to call
    
    gBattleStruct->sos.lastCallBattler = caller;
    answerRate = GetAnswerRate(caller, callRate);
    gEffectBattler = caller;
    gBattleScripting.battler = gBattlerFainted = ally;  // For SetDmgHazardsBattlescript
    if ((Random() % 100) > answerRate)
    {
        gBattleStruct->sos.lastCallFailed = TRUE;
        BattleScriptExecute(BattleScript_CallForHelpFailed);
        return TRUE;
    }

    if (gBattleStruct->sos.calls == 0)
    {
        // Set some quasi-double battle params on the first call
        gBattlerPositions[B_POSITION_PLAYER_RIGHT] = 0xFF;
        gBattlerControllerFuncs[2] = SetControllerToPlayer;
        gBattlerPositions[2] = B_POSITION_PLAYER_RIGHT;
        gBattlerControllerFuncs[3] = SetControllerToOpponent;
        gBattlerPositions[3] = B_POSITION_OPPONENT_RIGHT;
        gBattlerPartyIndexes[ally] = 1;
        gBattleTypeFlags |= BATTLE_TYPE_DOUBLE; // If not already set
        gBattlersCount = MAX_BATTLERS_COUNT;
    }
    
    // Increment number of answered calls. Technically goes to 255 (and rolled over is SM), but I see no reason to mimic that
    if (gBattleStruct->sos.calls < B_SOS_MAX_CHAIN)
        gBattleStruct->sos.calls++;
    
    CreateSoSAlly(caller, ally);
    gBattleStruct->sos.allyPresent = TRUE;
    
    CopyEnemyPartyMonToBattleData(ally,
        GetPartyIdFromBattlePartyId(gBattlerPartyIndexes[ally]));
    
    *(gBattleStruct->monToSwitchIntoId + ally) = gBattlerPartyIndexes[ally];
    gBattleCommunication[MULTISTRING_CHOOSER] = (gBattleStruct->sos.totemBattle) ? B_MSG_SOS_CALL_TOTEM : B_MSG_SOS_CALL_NORMAL;   // See gSosBattleCallStringIds
    BattleScriptExecute(BattleScript_CallForHelp);
    return TRUE;
}

