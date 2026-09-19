# Misclick

A lightweight C library for microcontroller button input debouncing and advanced click pattern detection.

## Features

- **Hardware debouncing** - Filters out electrical noise from button presses
- **Multiple click patterns** - Detects single, double, triple clicks
- **Long press support** - Recognizes long presses, long multi-clicks and clicks ending with a long or very long press
- **Custom gesture endings** - Combines clicks with events detected by the caller (a knock, a motion pattern, ...)
- **Configurable timing** - Customizable debounce, press, and cooldown intervals
- **Callback-based** - Non-blocking event-driven architecture
- **Memory efficient** - Low memory footprint
- **Timer agnostic** - Works with any timer implementation

## Supported Click Patterns

A gesture is up to three presses of one button. It is reported once, when it
is complete, as an `enum misclick_click_t` value:

| Gesture | Value |
|---|---|
| Single, double, triple click | `MISCLICK_CLICK_SINGLE`, `_DOUBLE`, `_TRIPLE` |
| Long press, optionally followed by one or two clicks | `MISCLICK_CLICK_LONG`, `_LONG_DOUBLE`, `_LONG_TRIPLE` |
| One or two clicks followed by a long press | `MISCLICK_CLICK_SINGLE_LONG`, `_DOUBLE_LONG` |
| Zero to two clicks followed by a very long press | `MISCLICK_CLICK_VERY_LONG`, `_SINGLE_VERY_LONG`, `_DOUBLE_VERY_LONG` |
| Zero to three clicks followed by a custom event | `MISCLICK_CLICK_CUSTOM(0, event)`, `MISCLICK_CLICK_CUSTOM(MISCLICK_CLICK_SINGLE, event)`, ... |
| More than three presses | `MISCLICK_CLICK_CANCELLED` |

A click value is composed of bit fields, so callbacks can also inspect a
gesture by parts instead of matching whole values:

| Field | Mask / accessor | Meaning |
|---|---|---|
| bits 0-1 | `MISCLICK_CLICK_COUNT_MASK` | Number of presses (1..3) |
| bit 2 | `MISCLICK_CLICK_LONG_FIRST` | The first press was long |
| bits 3-4 | `MISCLICK_CLICK_END_MASK` | How it ended: `MISCLICK_CLICK_END_RELEASE`, `_END_LONG`, `_END_VERY_LONG`, `_END_CUSTOM` |
| bits 5+ | `MISCLICK_CLICK_CUSTOM_EVENT(click)` | The custom event id, with `_END_CUSTOM` |

A press held for `long_press_time_us` is long, and one held for
`very_long_press_time_us` is very long. A long *first* press does not end the
gesture, since more clicks may follow; any later long press and any very long
press ends it. Gestures are always reported once the button is released
(`_END_LONG`, `_END_VERY_LONG`); the press events `MISCLICK_LONG_PRESS_START`
and `MISCLICK_VERY_LONG_PRESS_START` give early notice while it is still held.
Set `final_press_time_us` to report held gestures before the release instead.

## Usage

### Integration with CMake

Add this to your `CMakeLists.txt` to automatically download and build the library:

```cmake
include(FetchContent)

FetchContent_Declare(
  misclick
  GIT_REPOSITORY https://github.com/cyborgize/misclick.git
  GIT_TAG        main  # or a specific version tag like v1.0.0
)

FetchContent_MakeAvailable(misclick)

# Link to your target
target_link_libraries(your_target PRIVATE misclick)
```

### Implementation Example

This example shows integration with Zephyr RTOS, but the same patterns apply to other platforms.

### 1. Timer Implementation

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "misclick/misclick.h"

// Zephyr timers for the misclick library
static struct k_timer misclick_state_timer;
static struct k_timer misclick_click_timer;

// Timer callback handlers
static void misclick_state_timer_handler(struct k_timer *timer) {
    misclick_handle_state_timeout(k_uptime_get() * 1000); // Convert ms to us
}

static void misclick_click_timer_handler(struct k_timer *timer) {
    misclick_handle_click_timeout(k_uptime_get() * 1000); // Convert ms to us
}

// Timer interface functions for misclick library
static void misclick_stop_timer(void *handle) {
    struct k_timer *timer = (struct k_timer *)handle;
    k_timer_stop(timer);
}

static void misclick_start_timer(void *handle, int64_t timeout_us) {
    struct k_timer *timer = (struct k_timer *)handle;
    k_timer_start(timer, K_USEC(timeout_us), K_NO_WAIT);
}
```

### 2. Button Event Callbacks

```c
#define KNOCK_EVENT 1  // custom event id, see step 5

// Handle different click patterns
static void button_click_callback(void *callback_arg, int button_id,
                                  enum misclick_click_t click, int64_t timestamp) {
    switch (click) {
        case MISCLICK_CLICK_SINGLE:
            printk("Single click detected\\n");
            // Trigger action for single click
            break;
        case MISCLICK_CLICK_DOUBLE:
            printk("Double click detected\\n");
            // Trigger action for double click
            break;
        case MISCLICK_CLICK_TRIPLE:
            printk("Triple click detected\\n");
            // Trigger action for triple click
            break;
        case MISCLICK_CLICK_LONG:
            printk("Long press detected\\n");
            // Trigger action for long press
            break;
        case MISCLICK_CLICK_LONG_DOUBLE:
            printk("Long double click detected\\n");
            break;
        case MISCLICK_CLICK_LONG_TRIPLE:
            printk("Long triple click detected\\n");
            break;
        case MISCLICK_CLICK_SINGLE_LONG:
            printk("Click then long press detected\\n");
            break;
        case MISCLICK_CLICK_VERY_LONG:
            printk("Very long press detected\\n");
            break;
        case MISCLICK_CLICK_CUSTOM(0, KNOCK_EVENT):
            printk("Knock detected\\n");
            break;
        case MISCLICK_CLICK_CUSTOM(MISCLICK_CLICK_SINGLE, KNOCK_EVENT):
            printk("Click then knock detected\\n");
            break;
        case MISCLICK_CLICK_CANCELLED:
            printk("Click sequence cancelled\\n");
            break;
        default:
            // Or inspect the gesture by parts
            if ((click & MISCLICK_CLICK_END_MASK) == MISCLICK_CLICK_END_VERY_LONG) {
                printk("%d press(es) ending with a very long press\\n", click & MISCLICK_CLICK_COUNT_MASK);
            }
            break;
    }

    printk("Button %d click: %d at %lld\\n", button_id, click, timestamp);
}

// Optional: Handle individual press/release events
static void button_press_callback(void *callback_arg, int button_id,
                                  enum misclick_press_t press, int64_t timestamp) {
    printk("Button %d press: %d at %lld\\n", button_id, press, timestamp);
}
```

### 3. Library Initialization

```c
static struct misclick_t *main_button = NULL;

static void init_buttons(void) {
    // Initialize timers
    k_timer_init(&misclick_state_timer, misclick_state_timer_handler, NULL);
    k_timer_init(&misclick_click_timer, misclick_click_timer_handler, NULL);

    // Configure the misclick library
    struct misclick_config_t config = {
        .state_timer_handle = &misclick_state_timer,
        .click_timer_handle = &misclick_click_timer,
        .stop_timer = misclick_stop_timer,
        .start_timer = misclick_start_timer,
        .debounce_time_us = DEFAULT_MISCLICK_DEBOUNCE_TIME_US,
        .long_press_time_us = DEFAULT_MISCLICK_LONG_PRESS_TIME_US,
        .next_press_time_us = DEFAULT_MISCLICK_NEXT_PRESS_TIME_US,
        .final_press_time_us = DEFAULT_MISCLICK_FINAL_PRESS_TIME_US, // <0 reports held gestures on release
        .cooldown_time_us = DEFAULT_MISCLICK_COOLDOWN_TIME_US,
        .very_long_press_time_us = DEFAULT_MISCLICK_VERY_LONG_PRESS_TIME_US, // 0 disables
    };
    misclick_init(&config);

    // Add main button
    struct misclick_params_t button_params = {
        .button_id = 0,
        .callback_arg = NULL,
        .click_callback = button_click_callback,
        .press_callback = button_press_callback,
    };
    main_button = misclick_add(&button_params);
}
```

### 4. GPIO Interrupt Handler

```c
// GPIO interrupt callback (called from ISR context)
static void button_gpio_callback(const struct device *dev,
                                struct gpio_callback *cb, uint32_t pins) {
    if (!main_button) {
        return;
    }

    // Read current button state (inverted since button is active low)
    int button_state = !gpio_pin_get_dt(&button_gpio);

    // Send button event to misclick library
    // Convert milliseconds to microseconds for timestamp
    misclick_handle_input_event(main_button, button_state, k_uptime_get() * 1000);
}
```

### 5. Custom Gesture Endings (optional)

Events detected by other means, such as a knock from an accelerometer, are
gestures too: on their own, or ending a click sequence. Like presses, they are
ignored during the cooldown after a gesture, and also while the button is held.

```c
static void knock_detected(void) {
    misclick_handle_custom_event(main_button, KNOCK_EVENT, k_uptime_get() * 1000);
}
```

#### Completing a held gesture from an external timer

For state-dependent system holds, set `final_press_time_us = -1` to keep a
held gesture unresolved. Save the timestamp supplied with `DOWN_START` or
`DOWN`, then start your application timer. On expiry call
`misclick_finish_held_gesture(button, event_id, press_timestamp, now)` outside
library callbacks, using the same synchronization as other library calls.
It returns `true` only for that still-active press, emits one custom ending,
and consumes the trailing release through cooldown. Early release or a stale
timer returns `false`. Stop the application timer on release.

This does not change `misclick_handle_custom_event`: ordinary events such as
knocks remain ignored while held. A very-long threshold is optional; with it
disabled, early release retains the ordinary long/multi-click classification.

### 6. Main Application Setup

```c
int main(void) {
    // Initialize GPIO for button input
    if (!device_is_ready(button_gpio.port)) {
        printk("Error: button device not ready\\n");
        return -1;
    }

    // Configure button pin as input with interrupt
    gpio_pin_configure_dt(&button_gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button_gpio, GPIO_INT_EDGE_BOTH);

    // Set up GPIO callback
    gpio_init_callback(&button_cb_data, button_gpio_callback, BIT(button_gpio.pin));
    gpio_add_callback(button_gpio.port, &button_cb_data);

    // Initialize misclick library
    init_buttons();

    // Application main loop
    while (1) {
        // Your application logic here
        k_sleep(K_MSEC(100));
    }

    return 0;
}
```

## Configuration

### Default Timing Values

```c
#define DEFAULT_MISCLICK_DEBOUNCE_TIME_US      40000   // 40ms debounce
#define DEFAULT_MISCLICK_LONG_PRESS_TIME_US   600000   // 600ms for long press
#define DEFAULT_MISCLICK_NEXT_PRESS_TIME_US   500000   // 500ms between clicks
#define DEFAULT_MISCLICK_FINAL_PRESS_TIME_US  500000   // report held gestures 500ms early; <0 waits for release
#define DEFAULT_MISCLICK_COOLDOWN_TIME_US    1000000   // 1s cooldown after complex patterns
#define DEFAULT_MISCLICK_VERY_LONG_PRESS_TIME_US 3000000 // 3s for a very long press
```

- `long_press_time_us` - hold time for a press to count as long, in any position of the gesture
- `very_long_press_time_us` - hold time, from the start of the press, for a very long press; must exceed `long_press_time_us`; 0 disables very long presses (a very long press is then the same as a long press)
- `final_press_time_us` - once a held press can no longer change the gesture (long, or very long when enabled), report the gesture without waiting for the release: at once (0) or after this much more hold (> 0); negative waits for the release
- `next_press_time_us` - maximum gap between a release and the next press (or custom event) of the same gesture
- `cooldown_time_us` - presses are ignored for this long after a gesture

### Timer Integration

The library requires two timers with microsecond precision:

- **State timer**: Used for input debouncing
- **Click timer**: Used for click pattern detection timeouts

Implement the timer interface functions according to your platform:

```c
void my_start_timer(void *handle, int64_t timeout_us) {
    // Cast handle to your timer instance and configure for one-shot operation
    // Set the timeout period and start the timer
    // Timer should fire once after timeout_us microseconds
}

void my_stop_timer(void *handle) {
    // Stop and reset the specified timer instance
}
```

**Requirements:**

- Create two separate timer instances (for state and click timers)
- Configure timers for **one-shot mode** (fire once, then stop)
- Implement microsecond-precision timing
- Call the library timeout handlers from your timer interrupt callbacks

## Memory Usage

- Per button: 64 bytes (32-bit target)
- Global state: 68 bytes (32-bit target)
- No dynamic allocation after initialization

## Tests

Scenario tests run with simulated time and need no hardware:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Thread Safety

This library is **not** thread-safe. If using in a multi-threaded environment, provide your own synchronization.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the full license text.
