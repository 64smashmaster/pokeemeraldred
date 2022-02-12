#define ABILITY_SET_END     {0xFF, 0}

struct AbilitySetter
{
    u16 ability;
    u8 level;
};

static const struct AbilitySetter sBulbasaurAbilitySet[] = 
{
    {ABILITY_OVERGROW, 1},
    {ABILITY_CHLOROPHYLL, 10},
    ABILITY_SET_END
};

//etc...

static const struct AbilitySetter *const sAbilitySetterLearnsets[NUM_SPECIES] = 
{
    [SPECIES_BULBASAUR] = sBulbasaurAbilitySet,
    //etc...
};
