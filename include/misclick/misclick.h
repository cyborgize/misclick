#ifndef __MISCLICK_H__
#define __MISCLICK_H__

#include <stdint.h>
#include <stdbool.h>

struct misclick_t;

#define DEFAULT_MISCLICK_DEBOUNCE_TIME_US      40000
#define DEFAULT_MISCLICK_LONG_PRESS_TIME_US   600000
#define DEFAULT_MISCLICK_NEXT_PRESS_TIME_US   500000
#define DEFAULT_MISCLICK_FINAL_PRESS_TIME_US  500000
#define DEFAULT_MISCLICK_COOLDOWN_TIME_US    1000000
#define DEFAULT_MISCLICK_VERY_LONG_PRESS_TIME_US 3000000
#define DEFAULT_MISCLICK_MAX_PRESSES               3

enum misclick_press_t {
  MISCLICK_DOWN_START,
  MISCLICK_DOWN,
  MISCLICK_UP,
  MISCLICK_IGNORE_UP,
  MISCLICK_IGNORE_COMPLETE,
  MISCLICK_COOLDOWN_DOWN,
  MISCLICK_COOLDOWN_UP,
  MISCLICK_COOLDOWN_COMPLETE,
  MISCLICK_COOLDOWN_PRESSED_COMPLETE,
  MISCLICK_LONG_PRESS_START,
  MISCLICK_VERY_LONG_PRESS_START,
  MISCLICK_MANY_DOWN,              // a press beyond max_presses, instead of MISCLICK_DOWN
};

/* A click value describes a whole gesture and is composed of bit fields:
 *   bits 0-2  number of presses in the gesture (1..max_presses)
 *   bit  3    the first press was long
 *   bit  4    more than max_presses presses (the count and long-first
 *             fields are then zero)
 *   bits 5-6  how the gesture ended
 *   bits 7+   the custom event id, when ended by a custom event
 * A zero press count means a standalone custom event, or many presses.
 *
 * A gesture with more presses than max_presses is one the application
 * does not act on, e.g. a way to cancel a click sequence under way by
 * clicking on: each press beyond the limit reports MISCLICK_MANY_DOWN,
 * and the gesture goes on as usual until it is reported as
 * MISCLICK_CLICK_MANY (with the usual ending bits).
 *
 * A gesture is reported after its last press is released: either when no
 * further press follows within next_press_time_us
 * (MISCLICK_CLICK_END_RELEASE), or at once when the last press was long
 * (MISCLICK_CLICK_END_LONG) or very long (MISCLICK_CLICK_END_VERY_LONG).
 * A long first press does not end the gesture by itself (more presses may
 * follow); only a very long one does. The press events
 * MISCLICK_LONG_PRESS_START and MISCLICK_VERY_LONG_PRESS_START give early
 * notice while the button is still held. With final_press_time_us set,
 * a held gesture is reported before the release, see misclick_config_t.
 *
 * With very_long_press_time_us unset (0), a very long press is the same as
 * a long press and END_VERY_LONG is never reported.
 *
 * A gesture waiting for the next press can also be ended by a custom event
 * supplied by the caller through misclick_handle_custom_event(), e.g. a
 * knock or a motion detected by other means; the click value then carries
 * MISCLICK_CLICK_END_CUSTOM and the event id. A custom event on its own is
 * a gesture with no presses. */
enum misclick_click_t {
  MISCLICK_CLICK_COUNT_MASK   = 0x07,
  MISCLICK_CLICK_LONG_FIRST   = 0x08,
  MISCLICK_CLICK_MANY         = 0x10,
  MISCLICK_CLICK_END_MASK     = 0x60,
  MISCLICK_CLICK_END_RELEASE  = 0x00,
  MISCLICK_CLICK_END_LONG     = 0x20,
  MISCLICK_CLICK_END_VERY_LONG = 0x40,
  MISCLICK_CLICK_END_CUSTOM   = 0x60,
  MISCLICK_CLICK_CUSTOM_SHIFT = 7,

  MISCLICK_CLICK_SINGLE       = 1,
  MISCLICK_CLICK_DOUBLE       = 2,
  MISCLICK_CLICK_TRIPLE       = 3,
  MISCLICK_CLICK_LONG         = MISCLICK_CLICK_LONG_FIRST | 1,
  MISCLICK_CLICK_LONG_DOUBLE  = MISCLICK_CLICK_LONG_FIRST | 2,
  MISCLICK_CLICK_LONG_TRIPLE  = MISCLICK_CLICK_LONG_FIRST | 3,
  /* short press(es) followed by a long press */
  MISCLICK_CLICK_SINGLE_LONG  = MISCLICK_CLICK_END_LONG | 2,
  MISCLICK_CLICK_DOUBLE_LONG  = MISCLICK_CLICK_END_LONG | 3,
  /* short press(es) followed by a very long press */
  MISCLICK_CLICK_VERY_LONG    = MISCLICK_CLICK_END_VERY_LONG | MISCLICK_CLICK_LONG_FIRST | 1,
  MISCLICK_CLICK_SINGLE_VERY_LONG = MISCLICK_CLICK_END_VERY_LONG | 2,
  MISCLICK_CLICK_DOUBLE_VERY_LONG = MISCLICK_CLICK_END_VERY_LONG | 3,
};

/* The click value of a gesture ended by custom event <event>, e.g.
 * MISCLICK_CLICK_CUSTOM(MISCLICK_CLICK_SINGLE, KNOCK) for click-then-knock
 * and MISCLICK_CLICK_CUSTOM(0, KNOCK) for a knock on its own. */
#define MISCLICK_CLICK_CUSTOM(click, event) \
  ((enum misclick_click_t)((click) | MISCLICK_CLICK_END_CUSTOM | ((event) << MISCLICK_CLICK_CUSTOM_SHIFT)))
/* The custom event id of a click value with MISCLICK_CLICK_END_CUSTOM. */
#define MISCLICK_CLICK_CUSTOM_EVENT(click) ((unsigned)(click) >> MISCLICK_CLICK_CUSTOM_SHIFT)

typedef void (*misclick_click_callback_t)(void *callback_arg, int button, enum misclick_click_t click, int64_t timestamp);
typedef void (*misclick_press_callback_t)(void *callback_arg, int button, enum misclick_press_t press, int64_t timestamp);

struct misclick_params_t {
  int button_id;
  void *callback_arg;
  misclick_click_callback_t click_callback;
  misclick_press_callback_t press_callback;
  /* Per-button debounce override. 0 (the default / zero-init) inherits the
   * global debounce_time_us; a positive value sets this button's own debounce
   * time; a NEGATIVE value disables debounce (0) — e.g. for inputs already
   * debounced in hardware (the nPM1304 GPIO / SHPHLD buttons). Backward
   * compatible: 0 keeps the old global behaviour. */
  int64_t debounce_time_us;
};

struct misclick_config_t {
  void *state_timer_handle;
  void *click_timer_handle;
  void (*stop_timer)(void *handle);
  void (*start_timer)(void *handle, int64_t timeout_us);
  int64_t debounce_time_us;
  int64_t long_press_time_us;
  int64_t next_press_time_us;
  /* Once a held press can no longer change the gesture (it is long, or very
   * long when very_long_press_time_us is set), report the gesture without
   * waiting for the release: at once (0) or after this much more hold (> 0).
   * A NEGATIVE value waits for the release. A long first press held this
   * long is reported as MISCLICK_CLICK_LONG | MISCLICK_CLICK_END_LONG,
   * forgoing the long double and triple clicks. */
  int64_t final_press_time_us;
  int64_t cooldown_time_us;
  /* A press held this long (from its start) ends the gesture as a very long
   * press. Must exceed long_press_time_us. 0 disables very long presses. */
  int64_t very_long_press_time_us;
  /* The most presses a gesture is told apart by, 1..7; beyond that it is
   * reported as MISCLICK_CLICK_MANY. 0 selects 3. */
  uint8_t max_presses;
};

void misclick_init(const struct misclick_config_t *config);
struct misclick_t *misclick_add(const struct misclick_params_t *params);
void misclick_handle_input_event(struct misclick_t *button, uint8_t sample, int64_t cur_time);
void misclick_handle_state_timeout(int64_t cur_time);
void misclick_handle_click_timeout(int64_t cur_time);
/* Feed a caller-defined event into the gesture detection. When the button
 * is idle or waiting for its next press, the event completes a gesture and
 * the click callback is invoked with MISCLICK_CLICK_END_CUSTOM, followed by
 * the usual cooldown. Like a press, the event is ignored during cooldown;
 * it is also ignored while the button is held. */
void misclick_handle_custom_event(struct misclick_t *button, unsigned event, int64_t cur_time);
/* End the currently held gesture with a custom event, entering cooldown.
 * press_timestamp must be the timestamp of its latest DOWN_START/DOWN callback;
 * a stale timer, released button, or gesture already in cooldown returns false
 * without emitting anything. Returns true when the click callback was invoked.
 * Ordinary custom events keep ignoring held buttons. Call this outside library
 * callbacks, with the same external synchronization as the other entry points.
 */
bool misclick_finish_held_gesture(struct misclick_t *button, unsigned event,
                                 int64_t press_timestamp, int64_t cur_time);
void misclick_deinit();

#endif // __MISCLICK_H__
