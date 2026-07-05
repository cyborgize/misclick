#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "misclick/misclick.h"

#define TAG "misclick"

static struct misclick_config_t misclick_config;

enum misclick_click_state_t {
  MISCLICK_CLICK_STATE_NONE,
  MISCLICK_CLICK_STATE_COOLDOWN,
  MISCLICK_CLICK_STATE_COOLDOWN_PRESSED,
  MISCLICK_CLICK_STATE_SHORT_CIRCUIT,
  MISCLICK_CLICK_STATE_SINGLE,
  MISCLICK_CLICK_STATE_SINGLE_WAIT,
  MISCLICK_CLICK_STATE_DOUBLE,
  MISCLICK_CLICK_STATE_DOUBLE_WAIT,
  MISCLICK_CLICK_STATE_TRIPLE,
  MISCLICK_CLICK_STATE_TRIPLE_WAIT,
  MISCLICK_CLICK_STATE_LONG,
  MISCLICK_CLICK_STATE_LONG_WAIT,
  MISCLICK_CLICK_STATE_LONG_DOUBLE,
  MISCLICK_CLICK_STATE_LONG_DOUBLE_WAIT,
  MISCLICK_CLICK_STATE_LONG_TRIPLE,
  MISCLICK_CLICK_STATE_LONG_TRIPLE_WAIT,
};

struct misclick_t {
  struct misclick_params_t params;
  uint8_t state : 1;
  enum misclick_click_state_t click;
  int64_t state_tick;
  int64_t click_tick;
  union {
    int64_t first_click_ts;
    int32_t first_click_dur;
  };
  int64_t second_click_ts;
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

static void misclick_clicked(struct misclick_t *button, enum misclick_click_t click, int64_t timestamp) {
  if (button->params.click_callback) {
    button->params.click_callback(button->params.callback_arg, button->params.button_id, click, timestamp);
  }
}

static void misclick_pressed(struct misclick_t *button, enum misclick_press_t press, int64_t timestamp) {
  if (button->params.press_callback) {
    button->params.press_callback(button->params.callback_arg, button->params.button_id, press, timestamp);
  }
}

static void misclick_state_changed(struct misclick_t *button, int64_t cur_time);

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

static void misclick_state_changed(struct misclick_t *button, int64_t cur_time) {
  switch (button->click) {
    case MISCLICK_CLICK_STATE_COOLDOWN:
      misclick_pressed(button, MISCLICK_COOLDOWN_DOWN, cur_time);
      break;
    case MISCLICK_CLICK_STATE_COOLDOWN_PRESSED:
      misclick_pressed(button, MISCLICK_COOLDOWN_UP, cur_time);
      break;
    case MISCLICK_CLICK_STATE_SHORT_CIRCUIT:
      misclick_pressed(button, MISCLICK_IGNORE_UP, cur_time);
      break;
    case MISCLICK_CLICK_STATE_NONE:
      misclick_pressed(button, MISCLICK_DOWN_START, cur_time);
      break;
    default:
      misclick_pressed(button, button->state ? MISCLICK_UP : MISCLICK_DOWN, cur_time);
      break;
  }
  switch (button->click) {
    case MISCLICK_CLICK_STATE_NONE:
      if (!button->state) {
        // button press = single click start
        button->click = MISCLICK_CLICK_STATE_SINGLE;
        button->click_tick = cur_time + misclick_config.long_press_time_us;
        button->first_click_ts = cur_time;
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_COOLDOWN:
      if (!button->state) {
        // button press = ignore
        button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
        // button->click_tick = INT64_MAX;
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_COOLDOWN_PRESSED:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = ignore
        button->click = MISCLICK_CLICK_STATE_COOLDOWN;
        // button->click_tick = INT64_MAX;
      }
      break;
    case MISCLICK_CLICK_STATE_SHORT_CIRCUIT:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = reset
        button->click = MISCLICK_CLICK_STATE_NONE;
        button->click_tick = INT64_MAX;
      }
      break;
    case MISCLICK_CLICK_STATE_SINGLE:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = single click, wait for more
        button->click = MISCLICK_CLICK_STATE_SINGLE_WAIT;
        button->click_tick = cur_time + misclick_config.next_press_time_us;
        button->first_click_dur = cur_time - button->first_click_ts;
      }
      break;
    case MISCLICK_CLICK_STATE_SINGLE_WAIT:
      if (!button->state) {
        // button press = double click start
        button->click = MISCLICK_CLICK_STATE_DOUBLE;
        button->click_tick = cur_time + misclick_config.final_press_time_us;
        button->second_click_ts = cur_time;
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_DOUBLE:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = double click, wait for more
        int32_t second_click_dur = cur_time - button->second_click_ts;
        if (second_click_dur <= button->first_click_dur / 2) {
          button->click = MISCLICK_CLICK_STATE_LONG_DOUBLE_WAIT;
        } else {
          button->click = MISCLICK_CLICK_STATE_DOUBLE_WAIT;
        }
        button->click_tick = cur_time + misclick_config.next_press_time_us;
      }
      break;
    case MISCLICK_CLICK_STATE_DOUBLE_WAIT:
      if (!button->state) {
        // button press = triple click start
        button->click = MISCLICK_CLICK_STATE_TRIPLE;
        button->click_tick = cur_time + misclick_config.final_press_time_us;
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_TRIPLE:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = triple click, wait for more
        button->click = MISCLICK_CLICK_STATE_TRIPLE_WAIT;
        button->click_tick = cur_time + misclick_config.next_press_time_us;
      }
      break;
    case MISCLICK_CLICK_STATE_TRIPLE_WAIT:
      if (!button->state) {
        // button press = more than three clicks = reset
        button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
        button->click_tick = cur_time + misclick_config.cooldown_time_us;
        misclick_clicked(button, MISCLICK_CLICK_CANCELLED, cur_time);
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_LONG:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = long click, wait for more
        button->click = MISCLICK_CLICK_STATE_LONG_WAIT;
        button->click_tick = cur_time + misclick_config.next_press_time_us;
        // doesn't matter anymore since the first click is definitely long
        // button->first_click_dur = cur_time - button->first_click_ts;
      }
      break;
    case MISCLICK_CLICK_STATE_LONG_WAIT:
      if (!button->state) {
        // button press = long double click start
        button->click = MISCLICK_CLICK_STATE_LONG_DOUBLE;
        button->click_tick = cur_time + misclick_config.final_press_time_us;
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_LONG_DOUBLE:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = long double click, wait for more
        button->click = MISCLICK_CLICK_STATE_LONG_DOUBLE_WAIT;
        button->click_tick = cur_time + misclick_config.next_press_time_us;
      }
      break;
    case MISCLICK_CLICK_STATE_LONG_DOUBLE_WAIT:
      if (!button->state) {
        // button press = long triple click start
        button->click = MISCLICK_CLICK_STATE_LONG_TRIPLE;
        button->click_tick = cur_time + misclick_config.final_press_time_us;
      } else {
        // button release = impossible
        assert(false);
      }
      break;
    case MISCLICK_CLICK_STATE_LONG_TRIPLE:
      if (!button->state) {
        // button press = impossible
        assert(false);
      } else {
        // button release = long triple click, wait for more
        button->click = MISCLICK_CLICK_STATE_LONG_TRIPLE_WAIT;
        button->click_tick = cur_time + misclick_config.next_press_time_us;
      }
      break;
    case MISCLICK_CLICK_STATE_LONG_TRIPLE_WAIT:
      if (!button->state) {
        // button press = more than three click = reset
        button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
        button->click_tick = cur_time + misclick_config.cooldown_time_us;
        misclick_clicked(button, MISCLICK_CLICK_CANCELLED, cur_time);
      } else {
        // button release = impossible
        assert(false);
      }
      break;
  }
}

void misclick_handle_click_timeout(int64_t cur_time)
{
  for (struct misclick_t *button = misclick_head; button != NULL; button = button->next) {
    if (button->click_tick <= cur_time) {
      switch (button->click) {
        case MISCLICK_CLICK_STATE_NONE:
        case MISCLICK_CLICK_STATE_SHORT_CIRCUIT:
          // impossible
          assert(false);
        case MISCLICK_CLICK_STATE_COOLDOWN:
          button->click = MISCLICK_CLICK_STATE_NONE;
          button->click_tick = INT64_MAX;
          misclick_pressed(button, MISCLICK_COOLDOWN_COMPLETE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_COOLDOWN_PRESSED:
          button->click = MISCLICK_CLICK_STATE_SHORT_CIRCUIT;
          button->click_tick = INT64_MAX;
          misclick_pressed(button, MISCLICK_COOLDOWN_PRESSED_COMPLETE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_SINGLE:
          button->click = MISCLICK_CLICK_STATE_LONG;
          button->click_tick = cur_time + misclick_config.final_press_time_us;
          misclick_pressed(button, MISCLICK_LONG_PRESS_START, cur_time);
          break;
        case MISCLICK_CLICK_STATE_SINGLE_WAIT:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_clicked(button, MISCLICK_CLICK_SINGLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_DOUBLE:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_pressed(button, MISCLICK_FINAL_PRESS, cur_time);
          misclick_clicked(button, MISCLICK_CLICK_DOUBLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_DOUBLE_WAIT:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_clicked(button, MISCLICK_CLICK_DOUBLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_TRIPLE:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_pressed(button, MISCLICK_FINAL_PRESS, cur_time);
          misclick_clicked(button, MISCLICK_CLICK_TRIPLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_TRIPLE_WAIT:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_clicked(button, MISCLICK_CLICK_TRIPLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_LONG:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_pressed(button, MISCLICK_FINAL_PRESS, cur_time);
          misclick_clicked(button, MISCLICK_CLICK_LONG, cur_time);
          break;
        case MISCLICK_CLICK_STATE_LONG_WAIT:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_clicked(button, MISCLICK_CLICK_LONG, cur_time);
          break;
        case MISCLICK_CLICK_STATE_LONG_DOUBLE:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_pressed(button, MISCLICK_FINAL_PRESS, cur_time);
          misclick_clicked(button, MISCLICK_CLICK_LONG_DOUBLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_LONG_DOUBLE_WAIT:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_clicked(button, MISCLICK_CLICK_LONG_DOUBLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_LONG_TRIPLE:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN_PRESSED;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_pressed(button, MISCLICK_FINAL_PRESS, cur_time);
          misclick_clicked(button, MISCLICK_CLICK_LONG_TRIPLE, cur_time);
          break;
        case MISCLICK_CLICK_STATE_LONG_TRIPLE_WAIT:
          button->click = MISCLICK_CLICK_STATE_COOLDOWN;
          button->click_tick = cur_time + misclick_config.cooldown_time_us;
          misclick_clicked(button, MISCLICK_CLICK_LONG_TRIPLE, cur_time);
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
  button->click = MISCLICK_CLICK_STATE_NONE;
  button->state_tick = INT64_MAX;
  button->click_tick = INT64_MAX;
  button->first_click_ts = 0;
  button->second_click_ts = 0;
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
