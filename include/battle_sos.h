#ifndef GUARD_BATTLE_SOS_H
#define GUARD_BATTLE_SOS_H

bool32 IsSosBattle(void);
bool32 IsSoSAllyPresent(void);
void CB2_SosCall(void);
bool32 TryInitSosCall(void);
u32 GetSosBattleShinyRolls(void);
bool32 IsDoubleBattleOnSide(u8 battler);

#endif // GUARD_BATTLE_SOS_H