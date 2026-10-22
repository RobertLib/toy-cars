#include "game.h"

// The garage, in the order the player meets it. Every car starts slower than
// the reference car and passes it once upgraded; each one leans its own way.
const CarInfo CAR_INFO[CAR_MODEL_COUNT] = {
    {.name = "Trailblazer", .kind = "RALLY", .tagline = "A little rally car with a lot of heart.",
     .price = 0, .markup = 100,
     .base = {-.10f, -.08f, -.08f, 0}, .step = {.04f, .03f, .028f, .05f}},
    {.name = "Sprite", .kind = "HATCH", .tagline = "Light, eager and glued to the bends.",
     .price = 3000, .markup = 115,
     .base = {-.06f, -.11f, -.03f, .10f}, .step = {.042f, .028f, .034f, .05f}},
    {.name = "Bruiser", .kind = "PICKUP", .tagline = "Big tires, big tank. Grass? What grass?",
     .price = 4500, .markup = 125,
     .base = {-.12f, -.06f, -.07f, .15f}, .step = {.04f, .034f, .03f, .04f}, .grit = .55f},
    {.name = "Comet", .kind = "GT", .tagline = "Long hood, low roof, no patience.",
     .price = 9000, .markup = 150,
     .base = {-.02f, -.02f, -.05f, -.12f}, .step = {.044f, .036f, .03f, .04f}},
};
const char *UPGRADE_NAMES[UPGRADE_COUNT] = {"ENGINE", "TOP SPEED", "TIRES", "FUEL"};

CarTune car_tune(CarModel model, const int levels[UPGRADE_COUNT]) {
    const CarInfo *info = &CAR_INFO[model];
    float v[UPGRADE_COUNT];
    for (int i = 0; i < UPGRADE_COUNT; i++)
        v[i] = info->base[i] + info->step[i] * (levels ? levels[i] : 0);
    return (CarTune){.power = v[UPGRADE_ENGINE], .speed = v[UPGRADE_SPEED], .grip = v[UPGRADE_TIRES],
                     .economy = v[UPGRADE_FUEL], .grit = info->grit};
}

// The seven rivals after the player's slot: the car each one drives, how much
// quicker than the field it is tuned and how much closer to the tire limit
// its driver corners. Juno and Ace lead the field; Rusty and Bean trail it.
static const struct {
    CarModel model;
    float tune, skill;
} RIVALS[CAR_COUNT] = {
    {CAR_TRAILBLAZER, 0, 0},
    {CAR_SPRITE, .025f, .02f},       // JUNO
    {CAR_COMET, .02f, 0},            // DASH
    {CAR_TRAILBLAZER, 0, -.01f},     // PIP
    {CAR_COMET, .01f, .01f},         // NOVA
    {CAR_TRAILBLAZER, .035f, .025f}, // ACE
    {CAR_BRUISER, -.015f, -.02f},    // RUSTY
    {CAR_SPRITE, -.025f, -.03f},     // BEAN
};

CarModel rival_model(int rival) {
    return RIVALS[rival].model;
}

float rival_skill(int rival) {
    return RIVALS[rival].skill;
}

// What a player who finishes third on normal has bought before a circuit's
// Championship stage: every earlier stage's prize and each earlier cup's
// bronze, spent level by level on engine, top speed and tires.
void expected_upgrades(int track, int levels[UPGRADE_COUNT]) {
    int coins = 0;
    for (int k = 0; k < track; k++)
        coins += race_prize(k, 1, MODE_CHAMPIONSHIP, 3);
    for (int cup = 0; cup < track_cup(track); cup++)
        coins += cup_prize(cup, 1, 1);
    for (int kind = 0; kind < UPGRADE_COUNT; kind++)
        levels[kind] = 0;
    for (bool bought = true; bought;) {
        bought = false;
        for (int kind = UPGRADE_ENGINE; kind <= UPGRADE_TIRES; kind++) {
            int price = upgrade_price(CAR_TRAILBLAZER, kind, levels[kind]);
            if (price && price <= coins) {
                coins -= price;
                levels[kind]++;
                bought = true;
            }
        }
    }
}

// Rivals arrive at each circuit in about the car a player who spent their
// winnings would have by then, tuned a little better: upgrades keep the
// player in the fight rather than ahead of it. Difficulty moves the field.
CarTune rival_tune(int track, int difficulty, int rival) {
    int levels[UPGRADE_COUNT];
    expected_upgrades(track, levels);
    CarTune t = car_tune(CAR_TRAILBLAZER, levels);
    // The first races start from the back of a short lap; later ones leave
    // more room to come through the field.
    static const float first[3] = {-.10f, -.035f, -.01f}, last[3] = {-.04f, .025f, .06f};
    float f = (float)track / (TRACK_COUNT - 1);
    float shift = lerpf(first[difficulty], last[difficulty], f) + RIVALS[rival].tune;
    t.power += shift;
    t.speed += shift;
    t.grip += shift;
    t.economy = 0;
    // Each car keeps its character: a Comet trades grip for speed, a Sprite
    // the reverse. The balance of the three stays the field's.
    const CarInfo *own = &CAR_INFO[RIVALS[rival].model], *base = &CAR_INFO[CAR_TRAILBLAZER];
    float d[3], mean = 0;
    for (int i = 0; i < 3; i++)
        mean += (d[i] = own->base[i] - base->base[i]) / 3;
    t.power += d[UPGRADE_ENGINE] - mean;
    t.speed += d[UPGRADE_SPEED] - mean;
    t.grip += d[UPGRADE_TIRES] - mean;
    t.grit = own->grit;
    return t;
}

// Weighted toward top speed and acceleration, the things a lap time feels most.
int car_rating(const CarTune *t) {
    float pace = .4f * t->speed + .35f * t->power + .25f * t->grip;
    return (int)lroundf(clampf(50 + pace * 250, 0, 100));
}

int upgrade_price(CarModel model, UpgradeKind kind, int level) {
    static const int prices[UPGRADE_LEVELS] = {250, 500, 800, 1200, 1700};
    (void)kind;
    if (level < 0 || level >= UPGRADE_LEVELS)
        return 0;
    return (prices[level] * CAR_INFO[model].markup / 100 + 5) / 10 * 10;
}

// Later cups and harder rivals pay more; an Arcade race pays half.
static float prize_scale(int cup, int difficulty, RaceMode mode) {
    static const float cups[CUP_COUNT] = {1, 1.6f, 2.4f}, levels[3] = {.75f, 1, 1.3f};
    return cups[cup] * levels[difficulty] * (mode == MODE_ARCADE ? .5f : 1);
}

int race_prize(int track, int difficulty, RaceMode mode, int place) {
    static const int prizes[CAR_COUNT] = {600, 450, 350, 280, 220, 170, 130, 100};
    if (place < 1 || place > CAR_COUNT)
        return 0;
    return (int)lroundf(prizes[place - 1] * prize_scale(track_cup(track), difficulty, mode) / 10) * 10;
}

int cup_prize(int cup, int difficulty, int medal) {
    static const int bonus[4] = {0, 500, 800, 1200};
    if (medal < 1 || medal > 3)
        return 0;
    return (int)lroundf(bonus[medal] * prize_scale(cup, difficulty, MODE_CHAMPIONSHIP) / 10) * 10;
}

bool game_buy_upgrade(Game *g, UpgradeKind kind) {
    Profile *p = &g->profile;
    CarModel car = g->garage_car;
    int price = upgrade_price(car, kind, p->upgrades[car][kind]);
    if (game_save_warning(g) || !p->owned[car] || !price || p->coins < price)
        return false;
    p->coins -= price;
    p->upgrades[car][kind]++;
    game_save_profile(g);
    return true;
}

bool game_buy_car(Game *g, CarModel model) {
    Profile *p = &g->profile;
    if (game_save_warning(g) || p->owned[model] || p->coins < CAR_INFO[model].price)
        return false;
    p->coins -= CAR_INFO[model].price;
    p->owned[model] = true;
    p->car = g->garage_car = model;
    game_save_profile(g);
    return true;
}

// The turntable shows any car; an owned one also becomes the player's ride.
void game_browse_car(Game *g, CarModel model) {
    g->garage_car = (CarModel)(((int)model + CAR_MODEL_COUNT) % CAR_MODEL_COUNT);
    if (g->profile.owned[g->garage_car] && g->profile.car != g->garage_car &&
        !game_save_warning(g)) {
        g->profile.car = g->garage_car;
        game_save_profile(g);
    }
}
