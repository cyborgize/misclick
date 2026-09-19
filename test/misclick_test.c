/* Scenario tests for misclick, driven with simulated time.
 *
 * An input script is a list of tokens separated by spaces. A number toggles
 * the button (starting from released) and advances time by that many
 * milliseconds; "k<id>" feeds custom event <id> at the current time.
 * The expected trace lists "<ms> <event>" entries separated by ";". */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "misclick/misclick.h"

static int64_t now;
static int64_t press_timestamp;
struct timer { int64_t deadline; };
static struct timer state_timer, click_timer;
static void stop_timer(void *h) { ((struct timer *)h)->deadline = INT64_MAX; }
static void start_timer(void *h, int64_t timeout) { ((struct timer *)h)->deadline = now + timeout; }

static char trace[4096];
static void emit(const char *what) {
  size_t n = strlen(trace);
  snprintf(trace + n, sizeof trace - n, "%s%lld %s", n ? ";" : "", (long long)now / 1000, what);
}

static const char *press_name(enum misclick_press_t p) {
  static const char *names[] = {
    "DOWN_START", "DOWN", "UP", "IGNORE_UP", "IGNORE_COMPLETE", "COOLDOWN_DOWN", "COOLDOWN_UP",
    "COOLDOWN_COMPLETE", "COOLDOWN_PRESSED_COMPLETE", "LONG_PRESS_START", "VERY_LONG_PRESS_START",
  };
  return names[p];
}

static void click_name(enum misclick_click_t c, char *buf) {
  static const char *base[] = {"NONE", "SINGLE", "DOUBLE", "TRIPLE", "?", "LONG", "LONG_DOUBLE", "LONG_TRIPLE"};
  if (c == MISCLICK_CLICK_CANCELLED) {
    strcpy(buf, "CANCELLED");
    return;
  }
  strcpy(buf, base[c & (MISCLICK_CLICK_COUNT_MASK | MISCLICK_CLICK_LONG_FIRST)]);
  switch (c & MISCLICK_CLICK_END_MASK) {
    case MISCLICK_CLICK_END_LONG: strcat(buf, "+END_LONG"); break;
    case MISCLICK_CLICK_END_VERY_LONG: strcat(buf, "+END_VERY_LONG"); break;
    case MISCLICK_CLICK_END_CUSTOM: sprintf(buf + strlen(buf), "+CUSTOM(%u)", MISCLICK_CLICK_CUSTOM_EVENT(c)); break;
  }
}

static void on_click(void *arg, int button, enum misclick_click_t click, int64_t ts) {
  char buf[64]; (void)arg; (void)button; (void)ts;
  click_name(click, buf);
  emit(buf);
}
static void on_press(void *arg, int button, enum misclick_press_t press, int64_t ts) {
  (void)arg; (void)button; (void)ts;
  if (press == MISCLICK_DOWN_START || press == MISCLICK_DOWN) press_timestamp = ts;
  emit(press_name(press));
}

struct scenario {
  const char *name;
  int64_t debounce_us;     /* per-button override: 0 = global, <0 = none */
  int64_t final_us;        /* final_press_time_us: <0 waits for release */
  int64_t very_long_us;
  const char *input;
  const char *expect;
};

static void run_timers_until(int64_t t) {
  for (;;) {
    int64_t next = state_timer.deadline < click_timer.deadline ? state_timer.deadline : click_timer.deadline;
    if (next > t) break;
    now = next;
    if (state_timer.deadline == next) {
      state_timer.deadline = INT64_MAX;
      misclick_handle_state_timeout(now);
    } else {
      click_timer.deadline = INT64_MAX;
      misclick_handle_click_timeout(now);
    }
  }
  now = t;
}

static int run(const struct scenario *sc) {
  struct misclick_config_t cfg = {
    .state_timer_handle = &state_timer,
    .click_timer_handle = &click_timer,
    .stop_timer = stop_timer,
    .start_timer = start_timer,
    .debounce_time_us = DEFAULT_MISCLICK_DEBOUNCE_TIME_US,
    .long_press_time_us = DEFAULT_MISCLICK_LONG_PRESS_TIME_US,
    .next_press_time_us = DEFAULT_MISCLICK_NEXT_PRESS_TIME_US,
    .cooldown_time_us = DEFAULT_MISCLICK_COOLDOWN_TIME_US,
    .very_long_press_time_us = sc->very_long_us,
    .final_press_time_us = sc->final_us,
  };
  state_timer.deadline = click_timer.deadline = INT64_MAX;
  now = 0;
  trace[0] = 0;
  misclick_init(&cfg);
  struct misclick_params_t params = {
    .button_id = 0,
    .click_callback = on_click,
    .press_callback = on_press,
    .debounce_time_us = sc->debounce_us,
  };
  struct misclick_t *button = misclick_add(&params);

  char script[256];
  strcpy(script, sc->input);
  int sample = 1;
  for (char *tok = strtok(script, " "); tok; tok = strtok(NULL, " ")) {
    if (tok[0] == 'w') {
      run_timers_until(now + atoll(tok + 1) * 1000);
    } else if (tok[0] == 'h' || tok[0] == 's') {
      unsigned id = atoi(tok + 1);
      char buf[32];
      sprintf(buf, "FINISH(%u)", id);
      emit(buf);
      bool done = misclick_finish_held_gesture(button, id,
          tok[0] == 's' ? press_timestamp - 1 : press_timestamp, now);
      emit(done ? "ACCEPTED" : "IGNORED");
    } else if (tok[0] == 'k') {
      unsigned id = atoi(tok + 1);
      char buf[32];
      sprintf(buf, "CUSTOM(%u)", id);
      emit(buf);
      misclick_handle_custom_event(button, id, now);
    } else {
      sample = !sample;
      misclick_handle_input_event(button, sample, now);
      run_timers_until(now + atoll(tok) * 1000);
    }
  }
  if (!sample) {
    misclick_handle_input_event(button, 1, now);
  }
  run_timers_until(INT64_MAX - 1);

  if (strcmp(trace, sc->expect) == 0) {
    printf("ok   %s\n", sc->name);
    return 0;
  }
  printf("FAIL %s\n  expected: %s\n  actual:   %s\n", sc->name, sc->expect, trace);
  return 1;
}

static const struct scenario scenarios[] = {
  {"single click", -1, -1, 0, "100",
   "0 DOWN_START;100 UP;600 SINGLE;1600 COOLDOWN_COMPLETE"},
  {"double click", -1, -1, 0, "100 200 100",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;900 DOUBLE;1900 COOLDOWN_COMPLETE"},
  {"triple click", -1, -1, 0, "100 200 100 200 100",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 DOWN;700 UP;1200 TRIPLE;2200 COOLDOWN_COMPLETE"},
  {"four clicks cancel", -1, -1, 0, "100 200 100 200 100 200 100",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 DOWN;700 UP;900 DOWN;900 CANCELLED;1000 COOLDOWN_UP;1900 COOLDOWN_COMPLETE"},
  {"long press released", -1, -1, 0, "800",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1300 LONG;2300 COOLDOWN_COMPLETE"},
  {"long press held", -1, -1, 0, "2000",
   "0 DOWN_START;600 LONG_PRESS_START;2000 UP;2500 LONG;3500 COOLDOWN_COMPLETE"},
  {"press held through cooldown", -1, -1, 0, "100 700 2000",
   "0 DOWN_START;100 UP;600 SINGLE;800 COOLDOWN_DOWN;1600 COOLDOWN_PRESSED_COMPLETE;2800 IGNORE_UP"},
  {"long double click", -1, -1, 0, "800 200 100",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1100 UP;1600 LONG_DOUBLE;2600 COOLDOWN_COMPLETE"},
  {"long triple click", -1, -1, 0, "800 200 100 200 100",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1100 UP;1300 DOWN;1400 UP;1900 LONG_TRIPLE;2900 COOLDOWN_COMPLETE"},
  {"relatively long first press", -1, -1, 0, "300 200 100",
   "0 DOWN_START;300 UP;500 DOWN;600 UP;1100 LONG_DOUBLE;2100 COOLDOWN_COMPLETE"},
  {"press during cooldown", -1, -1, 0, "100 700 100",
   "0 DOWN_START;100 UP;600 SINGLE;800 COOLDOWN_DOWN;900 COOLDOWN_UP;1600 COOLDOWN_COMPLETE"},
  {"debounce", 40000, -1, 0, "100 20 100",
   "40 DOWN_START;260 UP;760 SINGLE;1760 COOLDOWN_COMPLETE"},
  {"click then hold", -1, -1, 0, "100 200 2000",
   "0 DOWN_START;100 UP;300 DOWN;900 LONG_PRESS_START;2300 UP;2300 DOUBLE+END_LONG;3300 COOLDOWN_COMPLETE"},
  {"double click then hold", -1, -1, 0, "100 200 100 200 2000",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 DOWN;1200 LONG_PRESS_START;2600 UP;2600 TRIPLE+END_LONG;3600 COOLDOWN_COMPLETE"},
  {"click then medium press", -1, -1, 0, "100 200 550",
   "0 DOWN_START;100 UP;300 DOWN;850 UP;1350 DOUBLE;2350 COOLDOWN_COMPLETE"},
  {"long click then hold", -1, -1, 0, "800 200 2000",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1600 LONG_PRESS_START;3000 UP;3000 LONG_DOUBLE+END_LONG;4000 COOLDOWN_COMPLETE"},
  /* very long presses enabled (3 s) */
  {"very long press", -1, -1, 3000000, "4500",
   "0 DOWN_START;600 LONG_PRESS_START;3000 VERY_LONG_PRESS_START;4500 UP;4500 LONG+END_VERY_LONG;5500 COOLDOWN_COMPLETE"},
  {"long press held, not very long", -1, -1, 3000000, "2000",
   "0 DOWN_START;600 LONG_PRESS_START;2000 UP;2500 LONG;3500 COOLDOWN_COMPLETE"},
  {"click then very long", -1, -1, 3000000, "100 200 4500",
   "0 DOWN_START;100 UP;300 DOWN;900 LONG_PRESS_START;3300 VERY_LONG_PRESS_START;4800 UP;4800 DOUBLE+END_VERY_LONG;5800 COOLDOWN_COMPLETE"},
  {"click then long, released", -1, -1, 3000000, "100 200 1000",
   "0 DOWN_START;100 UP;300 DOWN;900 LONG_PRESS_START;1300 UP;1300 DOUBLE+END_LONG;2300 COOLDOWN_COMPLETE"},
  {"double click then very long", -1, -1, 3000000, "100 200 100 200 4500",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 DOWN;1200 LONG_PRESS_START;3600 VERY_LONG_PRESS_START;5100 UP;5100 TRIPLE+END_VERY_LONG;6100 COOLDOWN_COMPLETE"},
  {"long click then very long", -1, -1, 3000000, "800 200 4500",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1600 LONG_PRESS_START;4000 VERY_LONG_PRESS_START;5500 UP;5500 LONG_DOUBLE+END_VERY_LONG;6500 COOLDOWN_COMPLETE"},
  {"long double click, very long enabled", -1, -1, 3000000, "800 200 100",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1100 UP;1600 LONG_DOUBLE;2600 COOLDOWN_COMPLETE"},
  /* custom events */
  {"click then knock", -1, -1, 0, "100 200 k7",
   "0 DOWN_START;100 UP;300 CUSTOM(7);300 SINGLE+CUSTOM(7);1300 COOLDOWN_COMPLETE"},
  {"double click then knock", -1, -1, 0, "100 200 100 200 k7",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 CUSTOM(7);600 DOUBLE+CUSTOM(7);1600 COOLDOWN_COMPLETE"},
  {"long click then knock", -1, -1, 0, "800 200 k3",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 CUSTOM(3);1000 LONG+CUSTOM(3);2000 COOLDOWN_COMPLETE"},
  {"knock alone", -1, -1, 0, "k7",
   "0 CUSTOM(7);0 NONE+CUSTOM(7);1000 COOLDOWN_COMPLETE"},
  {"knock then click", -1, -1, 0, "k7 300 100",
   "0 CUSTOM(7);0 NONE+CUSTOM(7);0 COOLDOWN_DOWN;300 COOLDOWN_UP;1000 COOLDOWN_COMPLETE"},
  {"knock during and after cooldown", -1, -1, 0, "100 200 100 1100 k7 200 400 k7",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;900 DOUBLE;1500 CUSTOM(7);1500 COOLDOWN_DOWN;1700 COOLDOWN_UP;1900 COOLDOWN_COMPLETE;2100 CUSTOM(7);2100 NONE+CUSTOM(7);3100 COOLDOWN_COMPLETE"},
  {"knock while pressed", -1, -1, 0, "100 k7 100",
   "0 DOWN_START;100 CUSTOM(7);100 UP;600 SINGLE;1600 COOLDOWN_COMPLETE"},
  {"knock too late", -1, -1, 0, "100 600 k7",
   "0 DOWN_START;100 UP;600 SINGLE;700 CUSTOM(7);1600 COOLDOWN_COMPLETE"},
  {"click during cooldown after knock", -1, -1, 0, "100 200 k7 100 100",
   "0 DOWN_START;100 UP;300 CUSTOM(7);300 SINGLE+CUSTOM(7);300 COOLDOWN_DOWN;400 COOLDOWN_UP;1300 COOLDOWN_COMPLETE"},
  /* Explicit completion is distinct from ordinary custom events. */
  {"complete held press", -1, -1, 0, "5000 h1",
   "0 DOWN_START;600 LONG_PRESS_START;5000 FINISH(1);5000 LONG+CUSTOM(1);5000 ACCEPTED;5000 COOLDOWN_UP;6000 COOLDOWN_COMPLETE"},
  {"complete short hold", -1, -1, 0, "100 h1",
   "0 DOWN_START;100 FINISH(1);100 SINGLE+CUSTOM(1);100 ACCEPTED;100 COOLDOWN_UP;1100 COOLDOWN_COMPLETE"},
  {"complete very long hold", -1, -1, 5000000, "10000 h2",
   "0 DOWN_START;600 LONG_PRESS_START;5000 VERY_LONG_PRESS_START;10000 FINISH(2);10000 LONG+CUSTOM(2);10000 ACCEPTED;10000 COOLDOWN_UP;11000 COOLDOWN_COMPLETE"},
  {"finish released gesture ignored", -1, -1, 0, "800 100 h1",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;900 FINISH(1);900 IGNORED;1300 LONG;2300 COOLDOWN_COMPLETE"},
  {"stale completion ignored", -1, -1, 0, "800 100 100 s1",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;900 DOWN;1000 FINISH(1);1000 IGNORED;1000 UP;1500 LONG_DOUBLE;2500 COOLDOWN_COMPLETE"},
  {"repeated completion ignored", -1, -1, 0, "5000 h1 h2",
   "0 DOWN_START;600 LONG_PRESS_START;5000 FINISH(1);5000 LONG+CUSTOM(1);5000 ACCEPTED;5000 FINISH(2);5000 IGNORED;5000 COOLDOWN_UP;6000 COOLDOWN_COMPLETE"},
  {"finish idle gesture ignored", -1, -1, 0, "h1",
   "0 FINISH(1);0 IGNORED"},
  {"completion during debounce ignored", 40000, -1, 0, "20 h1",
   "20 FINISH(1);20 IGNORED"},
  {"completed hold survives cooldown and ignores repeat", -1, -1, 0, "5000 h1 w2000 h2",
   "0 DOWN_START;600 LONG_PRESS_START;5000 FINISH(1);5000 LONG+CUSTOM(1);5000 ACCEPTED;6000 COOLDOWN_PRESSED_COMPLETE;7000 FINISH(2);7000 IGNORED;7000 IGNORE_UP"},
  {"fresh click after completed hold", -1, -1, 0, "5000 h1 w2000 100 100",
   "0 DOWN_START;600 LONG_PRESS_START;5000 FINISH(1);5000 LONG+CUSTOM(1);5000 ACCEPTED;6000 COOLDOWN_PRESSED_COMPLETE;7000 IGNORE_UP;7100 DOWN_START;7200 UP;7700 SINGLE;8700 COOLDOWN_COMPLETE"},
  {"complete multi-press hold", -1, -1, 0, "800 100 700 h3",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;900 DOWN;1500 LONG_PRESS_START;1600 FINISH(3);1600 LONG_DOUBLE+CUSTOM(3);1600 ACCEPTED;1600 COOLDOWN_UP;2600 COOLDOWN_COMPLETE"},
  /* early report of held gestures */
  {"long press held, early", -1, 500000, 0, "2000",
   "0 DOWN_START;600 LONG_PRESS_START;1100 LONG+END_LONG;2000 COOLDOWN_UP;2100 COOLDOWN_COMPLETE"},
  {"long press held through cooldown, early", -1, 500000, 0, "3000",
   "0 DOWN_START;600 LONG_PRESS_START;1100 LONG+END_LONG;2100 COOLDOWN_PRESSED_COMPLETE;3000 IGNORE_UP"},
  {"click then hold, early", -1, 500000, 0, "100 200 2000",
   "0 DOWN_START;100 UP;300 DOWN;900 LONG_PRESS_START;1400 DOUBLE+END_LONG;2300 COOLDOWN_UP;2400 COOLDOWN_COMPLETE"},
  {"click then hold, at once", -1, 0, 0, "100 200 2000",
   "0 DOWN_START;100 UP;300 DOWN;900 LONG_PRESS_START;900 DOUBLE+END_LONG;1900 COOLDOWN_PRESSED_COMPLETE;2300 IGNORE_UP"},
  {"long press released before early report", -1, 500000, 0, "800",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1300 LONG;2300 COOLDOWN_COMPLETE"},
  {"very long press, early", -1, 500000, 3000000, "4200",
   "0 DOWN_START;600 LONG_PRESS_START;3000 VERY_LONG_PRESS_START;3500 LONG+END_VERY_LONG;4200 COOLDOWN_UP;4500 COOLDOWN_COMPLETE"},
  {"long press with very long enabled, early", -1, 500000, 3000000, "2000",
   "0 DOWN_START;600 LONG_PRESS_START;2000 UP;2500 LONG;3500 COOLDOWN_COMPLETE"},
  {"click then very long, early", -1, 500000, 3000000, "100 200 4200",
   "0 DOWN_START;100 UP;300 DOWN;900 LONG_PRESS_START;3300 VERY_LONG_PRESS_START;3800 DOUBLE+END_VERY_LONG;4500 COOLDOWN_UP;4800 COOLDOWN_COMPLETE"},
};

int main(void) {
  int failed = 0;
  for (size_t i = 0; i < sizeof scenarios / sizeof *scenarios; i++) {
    failed += run(&scenarios[i]);
  }
  printf("%d scenario(s) failed\n", failed);
  return failed != 0;
}
