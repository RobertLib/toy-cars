// Mobile compositions share the drawing and input primitives in ui.c. Coordinates
// use its safe canvas (at least 1120 x 576), never raw Retina pixels.
enum { MOBILE_NAV_HEIGHT = 72, MOBILE_PAGE_GAP = 24 };

static Rect mobile_bounds(const UI *u) {
    return (Rect){u->safe_left + 24, u->safe_top, u->width - u->safe_left - u->safe_right - 48,
                  u->height - u->safe_top - u->safe_bottom};
}
// Match the visible gap below the navbar to the gap at the screen's bottom,
// including the home-indicator inset and a little breathing room above it.
static Rect mobile_content_bounds(const UI *u) {
    Rect b = mobile_bounds(u);
    float gap = fmaxf(MOBILE_PAGE_GAP, u->safe_bottom + 12);
    b.y += MOBILE_NAV_HEIGHT + gap;
    b.h = u->height - gap - b.y;
    return b;
}
static void mobile_title(UI *u, Rect content, float size, const char *text) {
    title(u, content.x, content.y, size, text);
}
static bool mobile_button(UI *u, Rect r, const char *text, bool primary) {
    return styled_button(u, r, text, primary, 30);
}
static int mobile_page_index(Screen screen) {
    switch (screen) {
    case SCREEN_MENU:
        return 0;
    case SCREEN_GARAGE:
        return 1;
    case SCREEN_HELP:
        return 2;
    case SCREEN_SETTINGS:
        return 3;
    default:
        return -1;
    }
}
static void mobile_transition(UI *u, const Game *g, bool covered) {
    int page = mobile_page_index(g->screen);
    if (!u->mobile || page < 0 || covered || ui_input_blocked(u)) {
        u->menu_seen = false;
        u->menu_fade = 0;
        return;
    }
    if (!u->menu_seen) {
        u->menu_seen = true;
        u->menu_screen = g->screen;
        u->menu_transition_start = g->clock - .24f;
        u->nav_from = u->nav_position = (float)page;
    } else if (u->menu_screen != g->screen) {
        u->menu_screen = g->screen;
        u->menu_transition_start = g->clock;
        u->nav_from = u->nav_position;
    }
    float progress = clampf((g->clock - u->menu_transition_start) / .24f, 0, 1);
    // Ease out without moving hit targets or delaying navigation. A dark veil
    // fades the entire page, including the 3D preview, without a framebuffer copy.
    float remaining = (1 - progress) * (1 - progress) * (1 - progress);
    u->menu_fade = .92f * remaining;
    u->nav_position = lerpf(u->nav_from, (float)page, 1 - remaining);
}
static void mobile_header(UI *u, Game *g) {
    Rect b = mobile_bounds(u);
    // A translucent bar over the broadcast or the garage; pages fade in from dark.
    if (u->menu_fade > 0)
        rect(u, (Rect){0, b.y + MOBILE_NAV_HEIGHT, u->width, u->height - b.y - MOBILE_NAV_HEIGHT},
             alpha(NIGHT, u->menu_fade));
    for (int i = 0; i < 4; i++)
        rect(u, (Rect){0, b.y + MOBILE_NAV_HEIGHT + i * 2, u->width, 2},
             alpha(NIGHT, .2f - i * .05f));
    gradient(u, (Rect){0, 0, u->width, b.y + MOBILE_NAV_HEIGHT}, alpha(NIGHT, .9f),
             alpha(NIGHT, .66f), true);
    checker(u, b.x, b.y + 25, 7, ORANGE, WHITE);
    headline(u, 30, b.x + 29, b.y + 14, WHITE, "TOYCARS");
    rect(u, (Rect){b.x + 194, b.y + 18, 1, 36}, alpha(WHITE, .18f));
    const char *names[] = {"RACE", "GARAGE", "HOW TO PLAY", "SETTINGS"};
    const Screen screens[] = {SCREEN_MENU, SCREEN_GARAGE, SCREEN_HELP, SCREEN_SETTINGS};
    float x = b.x + 218, width = (b.w - 218 - 36) / 4;
    Screen current = g->screen;
    for (int i = 0; i < 4; i++) {
        Rect tab = {x + i * (width + 12), b.y, width, MOBILE_NAV_HEIGHT};
        bool selected = current == screens[i];
        if (selected)
            slant(u, (Rect){tab.x, b.y + 8, tab.w, 56}, alpha(WHITE, .08f));
        italic_center(u, 26, (Rect){tab.x, b.y, tab.w, MOBILE_NAV_HEIGHT},
                      selected ? WHITE : alpha(WHITE, .68f), names[i], false);
        if (hit(u, tab))
            g->screen = screens[i];
    }
    slant(u, (Rect){x + u->nav_position * (width + 12) + 24, b.y + 62, width - 48, 5}, ORANGE);
}
static void mobile_menu(UI *u, Game *g, Renderer *r) {
    Rect b = mobile_content_bounds(u);
    Track *t = &g->tracks[g->selected];
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool open = game_track_unlocked(&g->profile, g->selected);
    float card_h = 108, cards_y = b.y + b.h - card_h, lw = b.w * .54f;
    float bh = 86, by = cards_y - 18 - bh;
    r->hero_frame = .40f;
    menu_backdrop(u, b.x + lw + 140, cards_y);
    mode_switch(u, g, (Rect){b.x, b.y, 430, 64}, 28);
    // The cups take the place of the broadcast badge on phones.
    cup_tabs(u, g, b.x + 452, b.y, 64, 19);
    char name[48], text[128];
    upper(name, sizeof name, t->name);
    float big = 74, width = text_width(u, 1, big, name) + big * .3f;
    if (width > lw)
        big *= lw / width;
    float y = b.y + 64 + 26;
    headline(u, big, b.x - big * .05f, y - big * .3f, WHITE, name);
    y += big * .7f + 16;
    int c = game_cup(g);
    const CupInfo *cup = &CUPS[c];
    if (!open) {
        if (g->selected == cup->first)
            SDL_snprintf(text, sizeof text, "LOCKED  /  WIN A %s MEDAL", CUPS[c - 1].name);
        else
            SDL_snprintf(text, sizeof text, "LOCKED  /  CLEAR %s FIRST", g->tracks[g->selected - 1].name);
        upper(text, sizeof text, text);
    } else if (championship) {
        bool fresh = game_cup_fresh(g, c);
        SDL_snprintf(text, sizeof text, "P%d OVERALL   /   %d PTS   /   STAGE %d OF %d",
                     fresh ? 1 : game_championship_rank(g),
                     fresh ? 0 : game_championship_points(g, 0), g->selected - cup->first + 1, cup->count);
    } else {
        char best[32] = "--:--";
        if (g->profile.best[g->selected] > 0)
            format_time(best, sizeof best, g->profile.best[g->selected]);
        SDL_snprintf(text, sizeof text, "BEST %s   /   %s", best,
                     g->profile.medals[g->selected] ? medal_name(g->profile.medals[g->selected])
                                                    : "TWO LAPS");
    }
    label(u, 0, 24, b.x, y, tint(t->accent, .45f), text);
    const char *detail = !open ? "Unlock it in Championship."
                         : !championship ? t->description
                         : g->profile.championship_eliminated[c] ? "Eliminated last time. Go again!"
                         : g->profile.championship_stages[c] == cup->count
                             ? "Cup complete. Go again!"
                         : g->profile.championship_stages[c] == cup->count - 1
                             ? "Final stage. Overall top 3 wins a medal."
                             : "Finish top 4 to advance.";
    label(u, 0, 22, b.x, y + 36, alpha(WHITE, .78f), detail);
    Rect start = {b.x, by, lw, bh};
    u->menu_layout.start = start;
    u->menu_layout.secondary = (Rect){0};
    if (open) {
        if (race_button(u, start, start_label(g), 40, g->clock)) {
            game_start(g);
            ui_clear_input(u);
        }
    } else if (glass_button(u, start, "UNLOCK IN CHAMPIONSHIP", 32))
        game_set_mode(g, MODE_CHAMPIONSHIP);
    float rx = b.x + lw + 18, rw = b.x + b.w - rx, ry = by + (bh - 72) * .5f;
    if (championship)
        time_switch(u, g, (Rect){b.x + b.w - 270, ry, 270, 72}, 28);
    else {
        float tw = 270;
        Rect records = {rx, ry, rw - tw - 12, 72};
        u->menu_layout.secondary = records;
        if (glass_button(u, records, "RECORDS", 28)) {
            game_open_records(g);
            ui_clear_input(u);
        }
        time_switch(u, g, (Rect){b.x + b.w - tw, ry, tw, 72}, 28);
    }
    track_cards(u, g, (Rect){b.x, cards_y, b.w, card_h}, 32, 19);
    player_tag(u, g, r, b.x + lw + 60, b.y + 70, ry - 12, 22);
}
static void mobile_garage(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    float top = b.y - MOBILE_PAGE_GAP;
    gradient(u, (Rect){0, top, b.x + b.w * .62f, u->height - top}, alpha(NIGHT, .85f),
             alpha(NIGHT, 0), false);
    mobile_title(u, b, 56, "Your Trailblazer.");
    label(u, 0, 26, b.x, b.y + 62, alpha(WHITE, .78f), "Pick a color. Make it yours.");
    float cy = b.y + b.h - 210;
    for (int i = 0; i < 6; i++) {
        float x = b.x + 40 + i * 84;
        if (g->profile.color == i)
            slant_outline(u, (Rect){x - 40, cy - 33, 80, 66}, 3, WHITE);
        slant(u, (Rect){x - 33, cy - 26, 66, 52}, CAR_COLORS[i]);
        if (hit(u, (Rect){x - 38, cy - 38, 76, 76})) {
            g->profile.color = i;
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
    italic(u, 1, 30, b.x, cy + 44, WHITE, CAR_COLOR_NAMES[g->profile.color]);
    if (race_button(u, (Rect){b.x, b.y + b.h - 76, 420, 76}, "TAKE IT RACING", 38, g->clock))
        g->screen = SCREEN_MENU;
}
static void mobile_help(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    page_backdrop(u);
    mobile_title(u, b, 48, "Ready. Set. Tiny.");
    float cw = (b.w - 32) / 3, y = b.y + 66;
    const char *titles[] = {"01 / DRIVE", "02 / REFUEL", "03 / RACE"};
    char fuel[60];
    SDL_snprintf(fuel, sizeof fuel, "Each can restores %d%%.", FUEL_PICKUP_AMOUNT);
    const char *lines[3][4] = {
        {g->profile.auto_accel ? "The gas is automatic." : "Hold GAS to accelerate.",
         "Turn the wheel to steer.", "BRAKE before tight turns.",
         g->profile.auto_accel ? "Hold DRIFT to slide." : "Release GAS to coast."},
        {"Collect green fuel cans.", fuel, "Empty tank? Race over.", "Keep an eye on your fuel."},
        {"Finish two laps per track.", "Take ramps at speed.", "Stuck? Tap RECOVER.",
         "Tap pause for a break."}};
    for (int i = 0; i < 3; i++) {
        Rect card = {b.x + i * (cw + 16), y, cw, 264};
        panel(u, card, 18);
        headline(u, 34, skew_left(card, 18, y + 36) + 18, y + 8, ORANGE, titles[i]);
        float lx = skew_left(card, 18, y + card.h) + 20;
        for (int j = 0; j < 4; j++)
            label(u, 0, 24, lx, y + 80 + j * 39, alpha(WHITE, .86f), lines[i][j]);
    }
    if (mobile_button(u, (Rect){b.x + b.w - 300, b.y + b.h - 72, 300, 72}, "I'M READY", true))
        g->screen = SCREEN_MENU;
}
static void mobile_settings(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    page_backdrop(u);
    float lw = b.w * .43f, rx = b.x + lw + 32, rw = b.w - lw - 32;
    mobile_title(u, b, 40, "Rival difficulty");
    const char *levels[] = {"SUNDAY DRIVE", "CLUB RACE", "FULL SEND"};
    for (int i = 0; i < 3; i++) {
        if (mobile_button(u, (Rect){b.x, b.y + 66 + i * 88, lw, 72}, levels[i],
                          g->profile.difficulty == i)) {
            g->profile.difficulty = i;
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
    bool *values[] = {&g->profile.auto_accel, &g->profile.touch, &g->profile.sound,
                      &g->profile.music};
    const char *names[] = {"AUTO ACCELERATE", "TOUCH CONTROLS", "SOUND EFFECTS", "MUSIC"};
    for (int i = 0; i < 4; i++) {
        Rect row = {rx, b.y + i * 84, rw, 72};
        panel(u, row, row.h * LEAN);
        italic(u, 1, 30, rx + 36, row.y + 18, WHITE, names[i]);
        Rect switch_rect = {rx + rw - 140, row.y + 16, 104, 40};
        label(u, 0, 20, switch_rect.x - 50, row.y + 23, alpha(WHITE, .6f),
              *values[i] ? "ON" : "OFF");
        toggle(u, switch_rect, *values[i]);
        if (hit(u, row)) {
            *values[i] = !*values[i];
            game_save_profile(g);
            ui_clear_input(u);
        }
    }
    float by = b.y + b.h - 72;
    if (mobile_button(u, (Rect){b.x, by, 240, 72}, "RESET DATA", false)) {
        u->reset_failed = false;
        g->screen = SCREEN_RESET_DATA;
    }
    if (g->toast_time > 0)
        label(u, 0, 24, b.x + 264, by + 20, alpha(WHITE, .7f), g->toast);
    if (mobile_button(u, (Rect){b.x + b.w - 260, by, 260, 72}, "ALL SET", true))
        g->screen = SCREEN_MENU;
}
static void mobile_records(UI *u, Game *g) {
    Rect b = mobile_bounds(u);
    page_backdrop(u);
    title(u, b.x, b.y + 25, 44, g->tracks[g->selected].name);
    if (g->records_return == SCREEN_MENU) {
        if (mobile_button(u, (Rect){b.x + b.w - 316, b.y + 12, 152, 72}, "< TRACK", false)) {
            g->selected = (g->selected + TRACK_COUNT - 1) % TRACK_COUNT;
            g->record_page = 0;
        }
        if (mobile_button(u, (Rect){b.x + b.w - 152, b.y + 12, 152, 72}, "TRACK >", false)) {
            g->selected = (g->selected + 1) % TRACK_COUNT;
            g->record_page = 0;
        }
    }
    if (mobile_button(u, (Rect){b.x, b.y + 94, 200, 72}, "TOP 10", !g->record_history)) {
        g->record_history = false;
        g->record_page = 0;
    }
    if (mobile_button(u, (Rect){b.x + 212, b.y + 94, 280, 72}, "RECENT HISTORY",
                      g->record_history)) {
        g->record_history = true;
        g->record_page = 0;
    }
    TrackRecords *table = &g->profile.records[g->selected];
    const RaceRecord *entries = g->record_history ? table->history : table->top;
    int count = g->record_history ? table->history_count : table->top_count;
    int pages = count ? (count + 3) / 4 : 1;
    g->record_page = (int)clampf(g->record_page, 0, pages - 1);
    for (int row = 0; row < 4 && g->record_page * 4 + row < count; row++) {
        int index = g->record_page * 4 + row;
        const RaceRecord *e = &entries[index];
        float y = b.y + 178 + row * 74;
        bool current = g->records_return == SCREEN_RESULTS && e->id == g->result_id;
        Rect band = {b.x, y, b.w, 72};
        skew(u, band, 14, current ? alpha(ORANGE, .28f) : alpha(NIGHT, .72f));
        if (current)
            skew(u, (Rect){band.x, band.y, 24, band.h}, 14, ORANGE);
        char text[160], race_time[32], lap[32] = "--:--.---";
        record_time(race_time, sizeof race_time, e->time);
        if (e->best_lap > 0)
            record_time(lap, sizeof lap, e->best_lap);
        SDL_snprintf(text, sizeof text, "%02d   %s", g->record_history ? count - index : index + 1,
                     e->initials);
        italic(u, 1, 30, b.x + 24, y + 4, WHITE, text);
        SDL_snprintf(text, sizeof text, "%s    /    LAP %s", race_time, lap);
        label(u, 0, 24, b.x + 174, y + 5, WHITE, text);
        if (e->record) {
            if (e->improvement > 0)
                SDL_snprintf(text, sizeof text, "BEST  -%.3f S", e->improvement);
            else
                SDL_strlcpy(text, e->mode < 0 ? "IMPORTED BEST" : "FIRST RECORD", sizeof text);
            label(u, 0, 22, b.x + b.w - 268, y + 6, tint(ORANGE, .25f), text);
        }
        char date[32] = "Unknown date";
        time_t stamp = (time_t)e->date;
        struct tm *local = e->date ? localtime(&stamp) : NULL;
        if (local)
            strftime(date, sizeof date, "%Y-%m-%d %H:%M", local);
        const char *levels[] = {"Easy", "Normal", "Hard"};
        if (e->mode < 0 || e->difficulty < 0)
            SDL_snprintf(text, sizeof text, "Legacy record   /   %s", date);
        else
            SDL_snprintf(text, sizeof text, "%s   /   %s   /   P%d   /   %s",
                         e->mode == MODE_ARCADE ? "Arcade" : "Championship", levels[e->difficulty],
                         e->rank, date);
        label(u, 0, 22, b.x + 174, y + 36, alpha(WHITE, .6f), text);
    }
    if (!count) {
        italic_center(u, 44, (Rect){b.x, b.y + 245, b.w, 54}, WHITE,
                      "YOUR FIRST RECORD STARTS HERE.", true);
        center_label(u, 0, 26, b.x + b.w * .5f, b.y + 312, alpha(WHITE, .6f),
                     "Finish a race on this track to set a time.");
    }
    float by = b.y + b.h - 92;
    if (mobile_button(u, (Rect){b.x, by, 300, 72},
                      g->records_return == SCREEN_RESULTS ? "BACK TO RESULTS" : "BACK TO TRACKS",
                      true))
        g->screen = g->records_return;
    char page[32];
    SDL_snprintf(page, sizeof page, "%d / %d", g->record_page + 1, pages);
    center_label(u, 0, 26, b.x + b.w - 178, by + 20, WHITE, page);
    if (g->record_page > 0 && mobile_button(u, (Rect){b.x + b.w - 340, by, 92, 72}, "<", false))
        g->record_page--;
    if (g->record_page + 1 < pages &&
        mobile_button(u, (Rect){b.x + b.w - 92, by, 92, 72}, ">", false))
        g->record_page++;
}
static void mobile_results(UI *u, Game *g) {
    Rect b = mobile_bounds(u);
    rect(u, (Rect){0, 0, u->width, u->height}, alpha(NIGHT, .9f));
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool completed = game_championship_finished(g);
    const CupInfo *cup = &CUPS[game_cup(g)];
    bool eliminated = championship && g->profile.championship_eliminated[game_cup(g)];
    int settled = 0;
    for (int i = 0; i < CAR_COUNT; i++)
        settled += g->cars[i].finished || g->cars[i].dnf;
    bool overall = championship && settled == CAR_COUNT;
    bool waiting = championship && !g->championship_scored && !g->test_mode;
    const char *heading = eliminated       ? "Cup lost."
                        : championship  ? completed
                                               ? (game_championship_rank(g) <= 3 ? "Cup won!"
                                                                                 : "Cup lost.")
                                               : "Stage results"
                        : g->cars[0].dnf ? "Out of fuel."
                                         : "Race complete";
    title(u, b.x, b.y + 26, 48, heading);
    char text[128];
    if (eliminated)
        SDL_snprintf(text, sizeof text, "Eliminated in stage %d / %s",
                     g->profile.championship_stages[game_cup(g)],
                     g->cars[0].dnf ? "Did not finish" : "Top 4 finish required");
    else if (championship)
        SDL_snprintf(text, sizeof text, "%s stage %d / %d   /   %s", cup->name,
                     g->selected - cup->first + 1, cup->count,
                     waiting ? "Waiting for rivals to finish" : "All points counted");
    else
        SDL_snprintf(text, sizeof text, "%s   /   Two laps", g->tracks[g->selected].name);
    label(u, 0, 24, b.x, b.y + 78, alpha(WHITE, .62f), text);
    float lw = 320, top = b.y + 132, rx = b.x + lw + 28, rw = b.w - lw - 28;
    panel(u, (Rect){b.x, top, lw, 328}, 18);
    SDL_snprintf(text, sizeof text, "%s / %s", game_player_name(g),
                 championship ? "OVERALL" : "FINISH");
    label(u, 0, 24, b.x + 30, top + 14, alpha(WHITE, .6f), text);
    if (!championship && g->cars[0].dnf)
        SDL_strlcpy(text, "DNF", sizeof text);
    else
        SDL_snprintf(text, sizeof text, "P%d",
                     championship ? game_championship_rank(g) : g->finish_rank);
    headline(u, 68, b.x + 24, top + 45, ORANGE, text);
    label(u, 0, 22, b.x + 22, top + 143, alpha(WHITE, .6f), "RACE TIME");
    if (g->cars[0].dnf)
        SDL_strlcpy(text, "--:--.---", sizeof text);
    else
        record_time(text, sizeof text, g->cars[0].finish_time);
    italic(u, 1, 36, b.x + 20, top + 171, WHITE, text);
    label(u, 0, 22, b.x + 16, top + 230, alpha(WHITE, .6f),
          championship ? "TOTAL POINTS" : "BEST LAP");
    if (championship)
        SDL_snprintf(text, sizeof text, "%d", game_championship_points(g, 0));
    else if (g->cars[0].best_lap > 0)
        record_time(text, sizeof text, g->cars[0].best_lap);
    else
        SDL_strlcpy(text, "--:--.---", sizeof text);
    italic(u, 1, 36, b.x + 14, top + 258, WHITE, text);
    label(u, 0, 22, rx, top, alpha(WHITE, .55f), overall ? "OVERALL STANDINGS" : "CLASSIFICATION");
    int order[CAR_COUNT];
    for (int i = 0; i < CAR_COUNT; i++)
        order[i] = i;
    for (int i = 0; i < CAR_COUNT; i++)
        for (int j = i + 1; j < CAR_COUNT; j++)
            if (overall ? game_championship_precedes(g, order[j], order[i])
                        : game_car_precedes(g, order[j], order[i])) {
                int temp = order[i];
                order[i] = order[j];
                order[j] = temp;
            }
    for (int k = 0; k < CAR_COUNT; k++) {
        int i = order[k];
        Car *car = &g->cars[i];
        float y = top + 38 + k * 36;
        if (i == 0) {
            skew(u, (Rect){rx - 12, y, rw + 12, 35}, 8, alpha(ORANGE, .28f));
            skew(u, (Rect){rx - 12, y, 10, 35}, 8, ORANGE);
        }
        if (overall || (car->finished && !car->dnf))
            SDL_snprintf(text, sizeof text, "%02d", k + 1);
        else
            SDL_strlcpy(text, "--", sizeof text);
        label(u, 0, 24, rx + 6, y + 1, WHITE, text);
        slant(u, (Rect){rx + 44, y + 10, 18, 15}, car->color);
        italic(u, 1, 28, rx + 76, y - 1, WHITE, i ? car->name : game_player_name(g));
        if (overall)
            SDL_snprintf(text, sizeof text, "%d PTS", game_championship_points(g, i));
        else if (car->dnf)
            SDL_strlcpy(text, "DNF", sizeof text);
        else if (car->finished)
            record_time(text, sizeof text, car->finish_time);
        else
            SDL_strlcpy(text, "RACING", sizeof text);
        label(u, 0, 24, rx + rw - text_width(u, 0, 24, text), y + 1, alpha(WHITE, .75f), text);
    }
    float by = b.y + b.h - 92, bw = (b.w - 36) / 4;
    int next = game_next_track(g);
    const char *primary = completed                   ? "BACK TO MENU"
                          : championship && next >= 0 ? "NEXT STAGE"
                                                      : "RACE AGAIN";
    if (waiting) {
        SDL_snprintf(text, sizeof text, "%d / 8 FINISHED", settled);
        label(u, 0, 24, b.x, by + 20, alpha(WHITE, .6f), text);
    } else if (mobile_button(u, (Rect){b.x, by, championship ? 340 : bw, 72}, primary, true)) {
        game_continue(g);
        ui_clear_input(u);
    }
    if (!championship && next >= 0 &&
        mobile_button(u, (Rect){b.x + bw + 12, by, bw, 72}, "NEXT TRACK", false)) {
        g->selected = next;
        game_start(g);
        ui_clear_input(u);
    }
    if (!championship &&
        mobile_button(u, (Rect){b.x + 2 * (bw + 12), by, bw, 72}, "RECORDS", false))
        game_open_records(g);
    if (!completed && mobile_button(u, (Rect){b.x + b.w - bw, by, bw, 72}, "BACK TO MENU", false)) {
        game_leave_results(g);
        ui_clear_input(u);
    }
}
