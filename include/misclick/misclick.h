#ifndef __MISCLICK_H__
#define __MISCLICK_H__

struct misclick_t;

#define DEFAULT_MISCLICK_DEBOUNCE_TIME_US      40000
#define DEFAULT_MISCLICK_LONG_PRESS_TIME_US   600000
#define DEFAULT_MISCLICK_NEXT_PRESS_TIME_US   500000
#define DEFAULT_MISCLICK_FINAL_PRESS_TIME_US  500000
#define DEFAULT_MISCLICK_COOLDOWN_TIME_US    1000000

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
  MISCLICK_FINAL_PRESS,
};

enum misclick_click_t {
  MISCLICK_CLICK_SINGLE,
  MISCLICK_CLICK_DOUBLE,
  MISCLICK_CLICK_TRIPLE,
  MISCLICK_CLICK_LONG,
  MISCLICK_CLICK_LONG_DOUBLE,
  MISCLICK_CLICK_LONG_TRIPLE,
  MISCLICK_CLICK_CANCELLED,
};

typedef void (*misclick_click_callback_t)(void *callback_arg, int button, enum misclick_click_t click, int64_t timestamp);
typedef void (*misclick_press_callback_t)(void *callback_arg, int button, enum misclick_press_t press, int64_t timestamp);

struct misclick_params_t {
  int button_id;
  void *callback_arg;
  misclick_click_callback_t click_callback;
  misclick_press_callback_t press_callback;
};

struct misclick_config_t {
  void *state_timer_handle;
  void *click_timer_handle;
  void (*stop_timer)(void *handle);
  void (*start_timer)(void *handle, int64_t timeout_us);
  int64_t debounce_time_us;
  int64_t long_press_time_us;
  int64_t next_press_time_us;
  int64_t final_press_time_us;
  int64_t cooldown_time_us;
};

void misclick_init(const struct misclick_config_t *config);
struct misclick_t *misclick_add(const struct misclick_params_t *params);
void misclick_handle_input_event(struct misclick_t *button, uint8_t sample, int64_t cur_time);
void misclick_handle_state_timeout(int64_t cur_time);
void misclick_handle_click_timeout(int64_t cur_time);
void misclick_deinit();

#endif // __MISCLICK_H__
