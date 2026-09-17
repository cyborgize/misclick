/* Scenario tests for misclick, driven with simulated time.
 *
 * An input script is a list of tokens separated by spaces. A number toggles
 * the button (starting from released) and advances time by that many
 * milliseconds. The expected trace lists "<ms> <event>" entries separated by ";". */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "misclick/misclick.h"

static int64_t now;
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
    "COOLDOWN_COMPLETE", "COOLDOWN_PRESSED_COMPLETE", "LONG_PRESS_START", "FINAL_PRESS",
  };
  return names[p];
}

static void click_name(enum misclick_click_t c, char *buf) {
  static const char *base[] = {"CANCELLED", "SINGLE", "DOUBLE", "TRIPLE", "?", "LONG", "LONG_DOUBLE", "LONG_TRIPLE"};
  strcpy(buf, base[c & (MISCLICK_CLICK_COUNT_MASK | MISCLICK_CLICK_LONG_FIRST)]);
}

static void on_click(void *arg, int button, enum misclick_click_t click, int64_t ts) {
  char buf[64]; (void)arg; (void)button; (void)ts;
  click_name(click, buf);
  emit(buf);
}
static void on_press(void *arg, int button, enum misclick_press_t press, int64_t ts) {
  (void)arg; (void)button; (void)ts;
  emit(press_name(press));
}

struct scenario {
  const char *name;
  int64_t debounce_us;     /* per-button override: 0 = global, <0 = none */
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
    .final_press_time_us = DEFAULT_MISCLICK_FINAL_PRESS_TIME_US,
    .cooldown_time_us = DEFAULT_MISCLICK_COOLDOWN_TIME_US,
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
    sample = !sample;
    misclick_handle_input_event(button, sample, now);
    run_timers_until(now + atoll(tok) * 1000);
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
  {"single click", -1, "100",
   "0 DOWN_START;100 UP;600 SINGLE;1600 COOLDOWN_COMPLETE"},
  {"double click", -1, "100 200 100",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;900 DOUBLE;1900 COOLDOWN_COMPLETE"},
  {"triple click", -1, "100 200 100 200 100",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 DOWN;700 UP;1200 TRIPLE;2200 COOLDOWN_COMPLETE"},
  {"four clicks cancel", -1, "100 200 100 200 100 200 100",
   "0 DOWN_START;100 UP;300 DOWN;400 UP;600 DOWN;700 UP;900 DOWN;900 CANCELLED;1000 COOLDOWN_UP;1900 COOLDOWN_COMPLETE"},
  {"long press released", -1, "800",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1300 LONG;2300 COOLDOWN_COMPLETE"},
  {"long press held", -1, "2000",
   "0 DOWN_START;600 LONG_PRESS_START;1100 FINAL_PRESS;1100 LONG;2000 COOLDOWN_UP;2100 COOLDOWN_COMPLETE"},
  {"long press held through cooldown", -1, "3000",
   "0 DOWN_START;600 LONG_PRESS_START;1100 FINAL_PRESS;1100 LONG;2100 COOLDOWN_PRESSED_COMPLETE;3000 IGNORE_UP"},
  {"long double click", -1, "800 200 100",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1100 UP;1600 LONG_DOUBLE;2600 COOLDOWN_COMPLETE"},
  {"long triple click", -1, "800 200 100 200 100",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1100 UP;1300 DOWN;1400 UP;1900 LONG_TRIPLE;2900 COOLDOWN_COMPLETE"},
  {"relatively long first press", -1, "300 200 100",
   "0 DOWN_START;300 UP;500 DOWN;600 UP;1100 LONG_DOUBLE;2100 COOLDOWN_COMPLETE"},
  {"press during cooldown", -1, "100 700 100",
   "0 DOWN_START;100 UP;600 SINGLE;800 COOLDOWN_DOWN;900 COOLDOWN_UP;1600 COOLDOWN_COMPLETE"},
  {"debounce", 40000, "100 20 100",
   "40 DOWN_START;260 UP;760 SINGLE;1760 COOLDOWN_COMPLETE"},
  {"click then hold", -1, "100 200 2000",
   "0 DOWN_START;100 UP;300 DOWN;800 FINAL_PRESS;800 DOUBLE;1800 COOLDOWN_PRESSED_COMPLETE;2300 IGNORE_UP"},
  {"long click then hold", -1, "800 200 2000",
   "0 DOWN_START;600 LONG_PRESS_START;800 UP;1000 DOWN;1500 FINAL_PRESS;1500 LONG_DOUBLE;2500 COOLDOWN_PRESSED_COMPLETE;3000 IGNORE_UP"},
};

int main(void) {
  int failed = 0;
  for (size_t i = 0; i < sizeof scenarios / sizeof *scenarios; i++) {
    failed += run(&scenarios[i]);
  }
  printf("%d scenario(s) failed\n", failed);
  return failed != 0;
}
