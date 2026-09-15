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
    // The display atlas includes ascender space above visible capitals.
    label(u, 1, size, content.x, content.y - size * .25f, INK, text);
}
static bool mobile_button(UI *u, Rect r, const char *text, bool primary) {
    bool clicked = hit(u, r);
    rounded(u, r, 14, primary ? ORANGE : WHITE);
    center_label(u, 1, 30, r.x + r.w * .5f, r.y + (r.h - 36) * .5f, primary ? WHITE : INK, text);
    return clicked;
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
    // Ease out without moving hit targets or delaying navigation. A paper veil
    // fades the entire page, including the 3D preview, without a framebuffer copy.
    float remaining = (1 - progress) * (1 - progress) * (1 - progress);
    u->menu_fade = .92f * remaining;
    u->nav_position = lerpf(u->nav_from, (float)page, 1 - remaining);
}
static void mobile_header(UI *u, Game *g) {
    Rect b = mobile_bounds(u);
    if (u->menu_fade > 0)
        rect(u, (Rect){0, b.y + MOBILE_NAV_HEIGHT, u->width, u->height - b.y - MOBILE_NAV_HEIGHT},
             alpha(PAPER, u->menu_fade));
    for (int i = 0; i < 4; i++)
        rect(u, (Rect){0, b.y + MOBILE_NAV_HEIGHT + i * 2, u->width, 2},
             alpha(INK, .10f - i * .025f));
    rect(u, (Rect){0, 0, u->width, b.y + MOBILE_NAV_HEIGHT}, INK);
    checker(u, b.x, b.y + 25, 7, ORANGE, WHITE);
    label(u, 1, 30, b.x + 31, b.y + 14, WHITE, "TOYCARS");
    rect(u, (Rect){b.x + 194, b.y + 18, 1, 36}, alpha(WHITE, .18f));
    const char *names[] = {"RACE", "GARAGE", "HOW TO PLAY", "SETTINGS"};
    const Screen screens[] = {SCREEN_MENU, SCREEN_GARAGE, SCREEN_HELP, SCREEN_SETTINGS};
    float x = b.x + 218, width = (b.w - 218 - 36) / 4;
    Screen current = g->screen;
    for (int i = 0; i < 4; i++) {
        Rect tab = {x + i * (width + 12), b.y, width, MOBILE_NAV_HEIGHT};
        bool selected = current == screens[i];
        if (selected)
            rounded(u, (Rect){tab.x, b.y + 8, tab.w, 56}, 10, alpha(WHITE, .08f));
        center_label(u, 1, 26, tab.x + tab.w * .5f, b.y + 20, selected ? WHITE : alpha(WHITE, .68f),
                     names[i]);
        if (hit(u, tab))
            g->screen = screens[i];
    }
    rounded(u, (Rect){x + u->nav_position * (width + 12) + 24, b.y + 64, width - 48, 4}, 2, ORANGE);
}
static void mobile_menu(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    Track *t = &g->tracks[g->selected];
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool open = game_track_unlocked(&g->profile, g->selected);
    float left_w = b.w * .53f, cards_y = b.y + b.h - 120;
    // Opaque backing keeps text readable over the rotating diorama.
    rect(u, (Rect){b.x - 24, b.y - MOBILE_PAGE_GAP, left_w + 24, cards_y - b.y + MOBILE_PAGE_GAP},
         PAPER);
    if (mobile_button(u, (Rect){b.x, b.y, left_w * .60f - 6, 72}, "CHAMPIONSHIP", championship))
        game_set_mode(g, MODE_CHAMPIONSHIP);
    if (mobile_button(u, (Rect){b.x + left_w * .60f + 6, b.y, left_w * .40f - 6, 72}, "ARCADE",
                      !championship))
        game_set_mode(g, MODE_ARCADE);
    label(u, 1, 52, b.x, b.y + 80, INK, t->name);
    char text[128];
    if (!open) {
        label(u, 0, 24, b.x, b.y + 150, MUTED, "Unlock in Championship:");
        SDL_snprintf(text, sizeof text, "Complete %s first.", g->tracks[g->selected - 1].name);
    } else if (championship) {
        bool finished = g->profile.championship_stages == TRACK_COUNT;
        SDL_snprintf(text, sizeof text, "Overall P%d   /   %d points", game_championship_rank(g),
                     game_championship_points(g, 0));
        label(u, 0, 24, b.x, b.y + 150, MUTED, text);
        if (g->profile.championship_eliminated)
            SDL_snprintf(text, sizeof text, "Eliminated in stage %d. Try again!",
                         g->profile.championship_stages);
        else
            SDL_snprintf(text, sizeof text,
                         finished ? "All 3 stages complete."
                         : g->profile.championship_stages == TRACK_COUNT - 1
                             ? "Final stage. Overall top 3 wins a medal."
                             : "Stage %d of 3. Finish top 4 to advance.",
                         g->profile.championship_stages + 1);
    } else {
        label(u, 0, 24, b.x, b.y + 150, MUTED, "Two laps. Chase your personal best.");
        SDL_snprintf(text, sizeof text, "%.0f m loop   /   3 jumps", t->length);
    }
    label(u, 0, 24, b.x, b.y + 183, MUTED, text);
    float by = cards_y - 88;
    const char *start = !open                                           ? "UNLOCK IN CHAMPIONSHIP"
                        : !championship                                 ? "LET'S RACE"
                        : g->profile.championship_eliminated ||
                                  g->profile.championship_stages == TRACK_COUNT ? "NEW CHAMPIONSHIP"
                        : g->profile.championship_stages                ? "RACE NEXT STAGE"
                                                                        : "START CHAMPIONSHIP";
    if (mobile_button(u, (Rect){b.x, by, left_w, 72}, start, true)) {
        if (open) {
            game_start(g);
            ui_clear_input(u);
        } else
            game_set_mode(g, MODE_CHAMPIONSHIP);
    }
    float rx = b.x + left_w + 24, rw = b.w - left_w - 24;
    if (!championship && mobile_button(u, (Rect){rx, by, rw, 72}, "TRACK RECORDS", false)) {
        game_open_records(g);
        ui_clear_input(u);
    }
    rect(u, (Rect){0, cards_y - 8, u->width, u->height - cards_y + 8}, PAPER);
    float cw = (b.w - 24) / 3;
    for (int i = 0; i < TRACK_COUNT; i++) {
        Track *track = &g->tracks[i];
        bool selected = g->selected == i, unlocked = game_track_unlocked(&g->profile, i);
        Rect card = {b.x + i * (cw + 12), cards_y, cw, 120};
        rounded(u, card, 14, selected ? track->accent : WHITE);
        Color fg = selected ? WHITE : INK;
        label(u, 1, 34, card.x + 16, card.y + 10, fg, track->name);
        const char *status =
            !unlocked ? "LOCKED"
            : championship
                ? i < g->profile.championship_stages ? "STAGE SCORED" : "CHAMPIONSHIP STAGE"
                : "READY TO RACE";
        if (!championship && unlocked && g->profile.best[i] > 0) {
            char best[32];
            format_time(best, sizeof best, g->profile.best[i]);
            SDL_snprintf(text, sizeof text, "BEST %s", best);
            status = text;
        }
        label(u, 0, 22, card.x + 16, card.y + 67, fg, status);
        if (hit(u, card)) {
            g->selected = i;
            g->sound_event = 8;
        }
    }
}
static void mobile_garage(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    rect(u, (Rect){0, b.y - MOBILE_PAGE_GAP, b.x + b.w * .48f, u->height - b.y + MOBILE_PAGE_GAP},
         PAPER);
    mobile_title(u, b, 54, "Your Trailblazer.");
    label(u, 0, 26, b.x, b.y + 66, MUTED, "Pick a color. Make it yours.");
    float cy = b.y + b.h - 210;
    for (int i = 0; i < 6; i++) {
        float x = b.x + 40 + i * 84;
        circle(u, x, cy, 30, CAR_COLORS[i]);
        if (g->profile.color == i) {
            ring(u, x, cy, 36, 3, INK);
            circle(u, x, cy, 7, WHITE);
        }
        if (hit(u, (Rect){x - 38, cy - 38, 76, 76})) {
            g->profile.color = i;
            game_save_profile(g);
            ui_clear_input(u);
            g->sound_event = 8;
        }
    }
    label(u, 0, 26, b.x, cy + 54, INK, CAR_COLOR_NAMES[g->profile.color]);
    if (mobile_button(u, (Rect){b.x, b.y + b.h - 72, 400, 72}, "TAKE IT RACING", true))
        g->screen = SCREEN_MENU;
}
static void mobile_help(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    rect(u, (Rect){0, 0, u->width, u->height}, PAPER);
    mobile_title(u, b, 48, "Ready. Set. Tiny.");
    float cw = (b.w - 32) / 3, y = b.y + 74;
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
        float x = b.x + i * (cw + 16);
        rounded(u, (Rect){x, y, cw, 256}, 16, WHITE);
        label(u, 1, 34, x + 18, y + 14, ORANGE, titles[i]);
        for (int j = 0; j < 4; j++)
            label(u, 0, 24, x + 18, y + 76 + j * 39, INK, lines[i][j]);
    }
    if (mobile_button(u, (Rect){b.x + b.w - 300, b.y + b.h - 72, 300, 72}, "I'M READY", true))
        g->screen = SCREEN_MENU;
}
static void mobile_settings(UI *u, Game *g) {
    Rect b = mobile_content_bounds(u);
    rect(u, (Rect){0, 0, u->width, u->height}, PAPER);
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
        rounded(u, row, 14, WHITE);
        label(u, 1, 30, rx + 18, row.y + 18, INK, names[i]);
        Rect toggle = {rx + rw - 118, row.y + 14, 100, 44};
        rounded(u, toggle, 22, *values[i] ? ORANGE : MUTED);
        circle(u, toggle.x + (*values[i] ? 78 : 22), toggle.y + 22, 16, WHITE);
        label(u, 1, 20, toggle.x + (*values[i] ? 15 : 43), toggle.y + 9, WHITE,
              *values[i] ? "ON" : "OFF");
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
        label(u, 0, 24, b.x + 264, by + 20, MUTED, g->toast);
    if (mobile_button(u, (Rect){b.x + b.w - 260, by, 260, 72}, "ALL SET", true))
        g->screen = SCREEN_MENU;
}
static void mobile_records(UI *u, Game *g) {
    Rect b = mobile_bounds(u);
    rect(u, (Rect){0, 0, u->width, u->height}, PAPER);
    label(u, 1, 44, b.x, b.y + 12, INK, g->tracks[g->selected].name);
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
        rounded(u, (Rect){b.x, y, b.w, 72}, 10, current ? rgb(.97f, .85f, .73f) : WHITE);
        char text[160], race_time[32], lap[32] = "--:--.---";
        record_time(race_time, sizeof race_time, e->time);
        if (e->best_lap > 0)
            record_time(lap, sizeof lap, e->best_lap);
        SDL_snprintf(text, sizeof text, "%02d   %s", g->record_history ? count - index : index + 1,
                     e->initials);
        label(u, 1, 30, b.x + 16, y + 4, INK, text);
        SDL_snprintf(text, sizeof text, "%s    /    LAP %s", race_time, lap);
        label(u, 0, 24, b.x + 174, y + 5, INK, text);
        if (e->record) {
            if (e->improvement > 0)
                SDL_snprintf(text, sizeof text, "BEST  -%.3f S", e->improvement);
            else
                SDL_strlcpy(text, e->mode < 0 ? "IMPORTED BEST" : "FIRST RECORD", sizeof text);
            label(u, 0, 22, b.x + b.w - 268, y + 6, ORANGE, text);
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
        label(u, 0, 22, b.x + 174, y + 36, MUTED, text);
    }
    if (!count) {
        center_label(u, 1, 44, b.x + b.w * .5f, b.y + 245, INK, "YOUR FIRST RECORD STARTS HERE.");
        center_label(u, 0, 26, b.x + b.w * .5f, b.y + 312, MUTED,
                     "Finish a race on this track to set a time.");
    }
    float by = b.y + b.h - 92;
    if (mobile_button(u, (Rect){b.x, by, 300, 72},
                      g->records_return == SCREEN_RESULTS ? "BACK TO RESULTS" : "BACK TO TRACKS",
                      true))
        g->screen = g->records_return;
    char page[32];
    SDL_snprintf(page, sizeof page, "%d / %d", g->record_page + 1, pages);
    center_label(u, 0, 26, b.x + b.w - 178, by + 20, INK, page);
    if (g->record_page > 0 && mobile_button(u, (Rect){b.x + b.w - 340, by, 92, 72}, "<", false))
        g->record_page--;
    if (g->record_page + 1 < pages &&
        mobile_button(u, (Rect){b.x + b.w - 92, by, 92, 72}, ">", false))
        g->record_page++;
}
static void mobile_results(UI *u, Game *g) {
    Rect b = mobile_bounds(u);
    rect(u, (Rect){0, 0, u->width, u->height}, PAPER);
    bool championship = g->mode == MODE_CHAMPIONSHIP;
    bool completed = game_championship_finished(g);
    bool eliminated = championship && g->profile.championship_eliminated;
    int settled = 0;
    for (int i = 0; i < CAR_COUNT; i++)
        settled += g->cars[i].finished || g->cars[i].dnf;
    bool overall = championship && settled == CAR_COUNT;
    bool waiting = championship && !g->championship_scored && !g->test_mode;
    const char *title = eliminated       ? "Championship lost."
                        : championship  ? completed
                                               ? (game_championship_rank(g) <= 3 ? "Championship won!"
                                                                                 : "Championship lost.")
                                               : "Stage results"
                        : g->cars[0].dnf ? "Out of fuel."
                                         : "Race complete";
    label(u, 1, 48, b.x, b.y + 12, INK, title);
    char text[128];
    if (eliminated)
        SDL_snprintf(text, sizeof text, "Eliminated in stage %d / %s",
                     g->profile.championship_stages,
                     g->cars[0].dnf ? "Did not finish" : "Top 4 finish required");
    else if (championship)
        SDL_snprintf(text, sizeof text, "Stage %d / 3   /   %s", g->selected + 1,
                     waiting ? "Waiting for rivals to finish" : "All points counted");
    else
        SDL_snprintf(text, sizeof text, "%s   /   Two laps", g->tracks[g->selected].name);
    label(u, 0, 24, b.x, b.y + 78, MUTED, text);
    float lw = 320, top = b.y + 132, rx = b.x + lw + 28, rw = b.w - lw - 28;
    rounded(u, (Rect){b.x, top, lw, 328}, 16, WHITE);
    SDL_snprintf(text, sizeof text, "%s / %s", game_player_name(g),
                 championship ? "OVERALL" : "FINISH");
    label(u, 0, 24, b.x + 18, top + 14, MUTED, text);
    if (!championship && g->cars[0].dnf)
        SDL_strlcpy(text, "DNF", sizeof text);
    else
        SDL_snprintf(text, sizeof text, "P%d",
                     championship ? game_championship_rank(g) : g->finish_rank);
    label(u, 1, 68, b.x + 18, top + 45, ORANGE, text);
    label(u, 0, 22, b.x + 18, top + 143, MUTED, "RACE TIME");
    if (g->cars[0].dnf)
        SDL_strlcpy(text, "--:--.---", sizeof text);
    else
        record_time(text, sizeof text, g->cars[0].finish_time);
    label(u, 1, 36, b.x + 18, top + 171, INK, text);
    label(u, 0, 22, b.x + 18, top + 230, MUTED, championship ? "TOTAL POINTS" : "BEST LAP");
    if (championship)
        SDL_snprintf(text, sizeof text, "%d", game_championship_points(g, 0));
    else if (g->cars[0].best_lap > 0)
        record_time(text, sizeof text, g->cars[0].best_lap);
    else
        SDL_strlcpy(text, "--:--.---", sizeof text);
    label(u, 1, 36, b.x + 18, top + 258, INK, text);
    label(u, 0, 22, rx, top, MUTED, overall ? "OVERALL STANDINGS" : "CLASSIFICATION");
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
        if (i == 0)
            rounded(u, (Rect){rx - 8, y, rw + 8, 35}, 6, rgb(.97f, .85f, .73f));
        if (overall || (car->finished && !car->dnf))
            SDL_snprintf(text, sizeof text, "%02d", k + 1);
        else
            SDL_strlcpy(text, "--", sizeof text);
        label(u, 0, 24, rx, y + 1, INK, text);
        circle(u, rx + 53, y + 17, 7, car->color);
        label(u, 1, 28, rx + 76, y - 1, INK, i ? car->name : game_player_name(g));
        if (overall)
            SDL_snprintf(text, sizeof text, "%d PTS", game_championship_points(g, i));
        else if (car->dnf)
            SDL_strlcpy(text, "DNF", sizeof text);
        else if (car->finished)
            record_time(text, sizeof text, car->finish_time);
        else
            SDL_strlcpy(text, "RACING", sizeof text);
        label(u, 0, 24, rx + rw - text_width(u, 0, 24, text), y + 1, INK, text);
    }
    float by = b.y + b.h - 92, bw = (b.w - 36) / 4;
    int next = game_next_track(g);
    const char *primary = completed                   ? "BACK TO MENU"
                          : championship && next >= 0 ? "NEXT STAGE"
                                                      : "RACE AGAIN";
    if (waiting) {
        SDL_snprintf(text, sizeof text, "%d / 8 FINISHED", settled);
        label(u, 0, 24, b.x, by + 20, MUTED, text);
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
    if (!completed && !waiting &&
        mobile_button(u, (Rect){b.x + b.w - bw, by, bw, 72}, "BACK TO MENU", false)) {
        g->screen = SCREEN_MENU;
        ui_clear_input(u);
    }
}
