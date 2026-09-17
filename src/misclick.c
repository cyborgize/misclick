#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "misclick/misclick.h"

static struct misclick_config_t misclick_config;

enum misclick_phase_t {
  MISCLICK_PHASE_NONE,             // idle
  MISCLICK_PHASE_PRESSED,          // a press is in progress
  MISCLICK_PHASE_PRESSED_LONG,     // a press is in progress and became long
  MISCLICK_PHASE_PRESSED_VERY_LONG, // a press is in progress and became very long
  MISCLICK_PHASE_WAIT,             // released, waiting for the next press
  MISCLICK_PHASE_COOLDOWN,
  MISCLICK_PHASE_COOLDOWN_PRESSED,
  MISCLICK_PHASE_SHORT_CIRCUIT,
};

struct misclick_t {
  struct misclick_params_t params;
  uint8_t state : 1;
  uint8_t phase : 3;
  uint8_t count : 2;               // presses so far in the current gesture
  uint8_t long_first : 1;          // the first press of the gesture was long
  int64_t state_tick;
  int64_t click_tick;
  int64_t press_ts;                // start of the current press
  int32_t first_dur;               // duration of the first press
  struct misclick_t *next;
};

static struct misclick_t *misclick_head = NULL;

static void misclick_reschedule_timer(void *timer_handle, int64_t (*get_tick)(struct misclick_t*), uint64_t cur_time)
{
  misclick_config.stop_timer(timer_handle);
  int64_t min_tick = INT64_MAX;
  for (struct misclick_t *button = misclick_head; button != NULL; button = button->next) {
    int64_t tick = get_tick(button);
    if (tick < min_tick) {
      min_tick = tick;
    }
  }
  if (min_tick < INT64_MAX) {
    int64_t timeout = min_tick - cur_time;
    misclick_config.start_timer(timer_handle, timeout >= 0 ? timeout : 0);
  }
}

static int64_t get_state_tick(struct misclick_t *button)
{
  return button->state_tick;
}

static int64_t get_click_tick(struct misclick_t *button)
{
  return button->click_tick;
}

static void misclick_reschedule_state_timer(uint64_t cur_time)
{
  misclick_reschedule_timer(misclick_config.state_timer_handle, &get_state_tick, cur_time);
}

static void misclick_reschedule_click_timer(uint64_t cur_time)
{
  misclick_reschedule_timer(misclick_config.click_timer_handle, &get_click_tick, cur_time);
}

static void misclick_pressed(struct misclick_t *button, enum misclick_press_t press, int64_t timestamp)
{
  if (button->params.press_callback) {
    button->params.press_callback(button->params.callback_arg, button->params.button_id, press, timestamp);
  }
}

// The gesture ends: report it and enter cooldown (pressed or not).
static void misclick_end(struct misclick_t *button, enum misclick_click_t click, int64_t cur_time)
{
  button->phase = button->state ? MISCLICK_PHASE_COOLDOWN : MISCLICK_PHASE_COOLDOWN_PRESSED;
  button->click_tick = cur_time + misclick_config.cooldown_time_us;
  if (button->params.click_callback) {
    button->params.click_callback(button->params.callback_arg, button->params.button_id, click, cur_time);
  }
}

// When a held press can no longer change the gesture: the tick to report
// the gesture without waiting for the release, if configured.
static int64_t misclick_final_tick(int64_t cur_time)
{
  int64_t final = misclick_config.final_press_time_us;
  return final < 0 ? INT64_MAX : cur_time + final;
}

// The gesture so far, as a click value.
static enum misclick_click_t misclick_gesture(struct misclick_t *button)
{
  return (button->long_first ? MISCLICK_CLICK_LONG_FIRST : 0) | button->count;
}

// A new press of the gesture starts.
static void misclick_press(struct misclick_t *button, int64_t cur_time)
{
  button->count++;
  button->phase = MISCLICK_PHASE_PRESSED;
  button->press_ts = cur_time;
  button->click_tick = cur_time + misclick_config.long_press_time_us;
}

// The debounced button state has changed.
static void misclick_state_changed(struct misclick_t *button, int64_t cur_time)
{
  switch (button->phase) {
    case MISCLICK_PHASE_NONE:
      // button press = gesture start
      assert(!button->state);
      misclick_pressed(button, MISCLICK_DOWN_START, cur_time);
      button->count = 0;
      button->long_first = 0;
      misclick_press(button, cur_time);
      break;
    case MISCLICK_PHASE_PRESSED:
    case MISCLICK_PHASE_PRESSED_LONG:
    case MISCLICK_PHASE_PRESSED_VERY_LONG:
      // button release = wait for more, unless the press ended the gesture
      assert(button->state);
      misclick_pressed(button, MISCLICK_UP, cur_time);
      if (button->phase == MISCLICK_PHASE_PRESSED_VERY_LONG) {
        misclick_end(button, misclick_gesture(button) | MISCLICK_CLICK_END_VERY_LONG, cur_time);
        break;
      }
      if (button->phase == MISCLICK_PHASE_PRESSED_LONG && button->count > 1) {
        misclick_end(button, misclick_gesture(button) | MISCLICK_CLICK_END_LONG, cur_time);
        break;
      }
      int32_t dur = cur_time - button->press_ts;
      if (button->count == 1) {
        button->first_dur = dur;
      } else if (button->count == 2 && dur <= button->first_dur / 2) {
        // a second press much shorter than the first: treat the first as long
        button->long_first = 1;
      }
      button->phase = MISCLICK_PHASE_WAIT;
      button->click_tick = cur_time + misclick_config.next_press_time_us;
      break;
    case MISCLICK_PHASE_WAIT:
      // button press = next press of the gesture, or too many presses
      assert(!button->state);
      misclick_pressed(button, MISCLICK_DOWN, cur_time);
      if (button->count == 3) {
        misclick_end(button, MISCLICK_CLICK_CANCELLED, cur_time);
      } else {
        misclick_press(button, cur_time);
      }
      break;
    case MISCLICK_PHASE_COOLDOWN:
      // button press = ignore
      assert(!button->state);
      misclick_pressed(button, MISCLICK_COOLDOWN_DOWN, cur_time);
      button->phase = MISCLICK_PHASE_COOLDOWN_PRESSED;
      break;
    case MISCLICK_PHASE_COOLDOWN_PRESSED:
      // button release = ignore
      assert(button->state);
      misclick_pressed(button, MISCLICK_COOLDOWN_UP, cur_time);
      button->phase = MISCLICK_PHASE_COOLDOWN;
      break;
    case MISCLICK_PHASE_SHORT_CIRCUIT:
      // button release = reset
      assert(button->state);
      misclick_pressed(button, MISCLICK_IGNORE_UP, cur_time);
      button->phase = MISCLICK_PHASE_NONE;
      button->click_tick = INT64_MAX;
      break;
  }
}

void misclick_handle_state_timeout(int64_t cur_time)
{
  for (struct misclick_t *button = misclick_head; button != NULL; button = button->next) {
    if (button->state_tick <= cur_time) {
      button->state_tick = INT64_MAX;
      button->state = !button->state;
      misclick_state_changed(button, cur_time);
    }
  }
  misclick_reschedule_state_timer(cur_time);
  misclick_reschedule_click_timer(cur_time);
}

void misclick_handle_click_timeout(int64_t cur_time)
{
  for (struct misclick_t *button = misclick_head; button != NULL; button = button->next) {
    if (button->click_tick <= cur_time) {
      switch (button->phase) {
        case MISCLICK_PHASE_NONE:
        case MISCLICK_PHASE_SHORT_CIRCUIT:
          // impossible
          assert(false);
          break;
        case MISCLICK_PHASE_COOLDOWN:
          button->phase = MISCLICK_PHASE_NONE;
          button->click_tick = INT64_MAX;
          misclick_pressed(button, MISCLICK_COOLDOWN_COMPLETE, cur_time);
          break;
        case MISCLICK_PHASE_COOLDOWN_PRESSED:
          button->phase = MISCLICK_PHASE_SHORT_CIRCUIT;
          button->click_tick = INT64_MAX;
          misclick_pressed(button, MISCLICK_COOLDOWN_PRESSED_COMPLETE, cur_time);
          break;
        case MISCLICK_PHASE_PRESSED:
          // the press is long; it may still become very long
          button->phase = MISCLICK_PHASE_PRESSED_LONG;
          if (button->count == 1) {
            button->long_first = 1;
          }
          button->click_tick = misclick_config.very_long_press_time_us
            ? button->press_ts + misclick_config.very_long_press_time_us
            : misclick_final_tick(cur_time);
          misclick_pressed(button, MISCLICK_LONG_PRESS_START, cur_time);
          break;
        case MISCLICK_PHASE_PRESSED_LONG:
          if (misclick_config.very_long_press_time_us) {
            // the press is very long
            button->phase = MISCLICK_PHASE_PRESSED_VERY_LONG;
            button->click_tick = misclick_final_tick(cur_time);
            misclick_pressed(button, MISCLICK_VERY_LONG_PRESS_START, cur_time);
            break;
          }
          // the final press is held: report the gesture before the release
          misclick_end(button, misclick_gesture(button) | MISCLICK_CLICK_END_LONG, cur_time);
          break;
        case MISCLICK_PHASE_PRESSED_VERY_LONG:
          misclick_end(button, misclick_gesture(button) | MISCLICK_CLICK_END_VERY_LONG, cur_time);
          break;
        case MISCLICK_PHASE_WAIT:
          misclick_end(button, misclick_gesture(button), cur_time);
          break;
      }
    }
  }
  misclick_reschedule_click_timer(cur_time);
}

void misclick_init(const struct misclick_config_t *config)
{
  misclick_config = *config;
}

struct misclick_t *misclick_add(const struct misclick_params_t *params)
{
  struct misclick_t *button = malloc(sizeof(struct misclick_t));
  if (button == NULL) {
    return NULL;
  }
  button->params = *params;
  button->state = 1;
  button->phase = MISCLICK_PHASE_NONE;
  button->state_tick = INT64_MAX;
  button->click_tick = INT64_MAX;
  button->next = misclick_head;
  misclick_head = button;

  return button;
}

void misclick_handle_input_event(struct misclick_t *button, uint8_t sample, int64_t cur_time)
{
  int64_t db = button->params.debounce_time_us;
  int64_t debounce = db > 0 ? db : (db < 0 ? 0 : misclick_config.debounce_time_us);
  button->state_tick = sample != button->state ? cur_time + debounce : INT64_MAX;
  misclick_reschedule_state_timer(cur_time);
}

void misclick_deinit()
{
}
