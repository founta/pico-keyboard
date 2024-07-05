
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

#include "tusb.h"
#include "usb_descriptors.h"

#define DEFAULT_POLL_INTERVAL_MS (1)
#define LONG_POLL_INTERVAL_MS (50)
uint16_t poll_interval_ms = DEFAULT_POLL_INTERVAL_MS;

#define POWER_LED (28)
#define CAPS_LED (22)

const char rows[] = {
  0,1,2,3,4,5
};
#define num_rows (6)

const char cols[] = {
  6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21
};
#define num_cols (16)

uint8_t modifier_flags = 0;

//you can only press 6 keys simultaneously. should be ok
#define MAX_NUM_PRESSES (6)
uint8_t key_codes[MAX_NUM_PRESSES] = {HID_KEY_NONE};
uint8_t num_keys_pressed = 0;
bool idle = false;

bool can_resume = false;

void modif(uint8_t m)
{
  modifier_flags |= m;
}
void press(uint8_t k)
{
  if (num_keys_pressed >= MAX_NUM_PRESSES)
    return;
  
  key_codes[num_keys_pressed++] = k;
}

// const char* kb[num_rows][num_cols] = {
//   {"esc"   , "f1" , "f2" , "f3"   , "f4", "f5", "f6", "f7", "f8" , "f9", "f10"  , "f11"   , "f12"   , "prtsc"    , "insert", "delete"},
//   {"~"     , "1"  , "2"  , "3"    , "4" , "5" , "6" , "7" , "8"  , "9" , "0"    , "-"     , "="     , "backspace", "macro1", "home"  },
//   {"tab"   , "q"  , "w"  , "e"    , "r" , "t" , "y" , "u" , "i"  , "o" , "p"    , "["     , "]"     , "\\"       , "macro2", "pg up" },
//   {"caps"  , "a"  , "s"  , "d"    , "f" , "g" , "h" , "j" , "k"  , "l" , ";"    , "'"     , "enter" , "macro3"   , "macro4", "pg dn" },
//   {"lshift", "z"  , "x"  , "c"    , "v" , "b" , "n" , "m" , ","  , "." , "/"    , "macro5", "rshift", "up"       , "macro6", "end"   },
//   {"lctrl" , "win", "alt", "space", "--", "--", "--", "--", "alt", "fn", "rctrl", "macro7", "left"  , "down"     , "macro8", "right" }
// };

//TODO Fn key support
void (*kb_func[num_rows][num_cols]) (uint8_t) = {
  {&press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press},
  {&press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, NULL  , &press},
  {&press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, NULL  , &press},
  {&press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, NULL  , NULL  , &press},
  {&modif, &press, &press, &press, &press, &press, &press, &press, &press, &press, &press, NULL  , &modif, &press, NULL  , &press},
  {&modif, &press, &modif, &press, NULL  , NULL  , NULL  , NULL  , &modif, NULL  , &modif, NULL  , &press, &press, NULL  , &press}
};
uint8_t kb_vals[num_rows][num_cols] = {
  {HID_KEY_ESCAPE, HID_KEY_F1, HID_KEY_F2, HID_KEY_F3, HID_KEY_F4, HID_KEY_F5, HID_KEY_F6, HID_KEY_F7, HID_KEY_F8, HID_KEY_F9, HID_KEY_F10, HID_KEY_F11, HID_KEY_F12, HID_KEY_PRINT_SCREEN, HID_KEY_INSERT, HID_KEY_DELETE},
  {HID_KEY_GRAVE, HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5, HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9, HID_KEY_0, HID_KEY_MINUS, HID_KEY_EQUAL, HID_KEY_BACKSPACE, 0, HID_KEY_HOME},
  {HID_KEY_TAB, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T, HID_KEY_Y, HID_KEY_U, HID_KEY_I, HID_KEY_O, HID_KEY_P, HID_KEY_BRACKET_LEFT, HID_KEY_BRACKET_RIGHT, HID_KEY_BACKSLASH, 0, HID_KEY_PAGE_UP},
  {HID_KEY_CAPS_LOCK, HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G, HID_KEY_H, HID_KEY_J, HID_KEY_K, HID_KEY_L, HID_KEY_SEMICOLON, HID_KEY_APOSTROPHE, HID_KEY_ENTER, 0, 0, HID_KEY_PAGE_DOWN},
  {KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, HID_KEY_N, HID_KEY_M, HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH, 0, KEYBOARD_MODIFIER_RIGHTSHIFT, HID_KEY_ARROW_UP, 0, HID_KEY_END},
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_APPLICATION, KEYBOARD_MODIFIER_LEFTALT, HID_KEY_SPACE, 0, 0, 0, 0, KEYBOARD_MODIFIER_RIGHTALT, 0, KEYBOARD_MODIFIER_RIGHTCTRL, 0, HID_KEY_ARROW_LEFT, HID_KEY_ARROW_DOWN, 0, HID_KEY_ARROW_RIGHT}
};
//note HID_KEY_GRAVE for ` and ~ . HID_KEY_APPLICATION == windows key

void scan_kb_matrix()
{
  for (int i = 0; i < MAX_NUM_PRESSES; ++i)
    key_codes[i] = 0;
  num_keys_pressed = 0;
  modifier_flags = 0;

  for (int i = 0; i < num_rows; ++i)
  {
    char row = rows[i];
    gpio_put(row, true);
    sleep_us(10); //allow for previous row to discharge and current row to charge, otherwise we get false positives
    for (int j = 0; j < num_cols; ++j)
    {
      char col = cols[j];

      void (*func)(uint8_t) = kb_func[i][j];
      if (func && gpio_get(col))
      {
        func(kb_vals[i][j]);
      }
    }
    gpio_put(row, false);
  }
}

absolute_time_t last_update_start_time = {0}; //nil_time is {0}
void handle_hid()
{
  absolute_time_t now = get_absolute_time();
  int64_t time_diff_us = absolute_time_diff_us(last_update_start_time, now);
  if (!is_nil_time(last_update_start_time) && (time_diff_us < (poll_interval_ms * 1000))) //too early
  {
    sleep_us(time_diff_us / 2);
    return;
  }
  last_update_start_time = now;

  //scan the keyboard matrix (takes ~75us)
  scan_kb_matrix();

  //remote wakeup
  if (tud_suspended())
  {
    if (num_keys_pressed != 0 && can_resume)
    {
      tud_remote_wakeup();
    }
  }
  else
  {
    //send USB HID report
    if (tud_hid_ready())
    {
      if (num_keys_pressed != 0)
      {
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier_flags, key_codes);
        idle = false;
      }
      else
      {
        if (!idle) //send an empty key report
        {
          tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
          idle = true;
        }
      }
    }
  }
}

void keyboard_gpio_init()
{
  for (int row = 0; row < num_rows; ++row)
  {
    char pin = rows[row];
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, false);
  }
  for (int col = 0; col < num_cols; ++col)
  {
    char pin = cols[col];
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
  }
}
void led_init()
{
  gpio_init(POWER_LED);
  gpio_set_dir(POWER_LED, GPIO_OUT);
  gpio_put(POWER_LED, false);

  gpio_init(CAPS_LED);
  gpio_set_dir(CAPS_LED, GPIO_OUT);
  gpio_put(CAPS_LED, false);
}

int main()
{
  //board_init(); // TUSB's board init. don't do this. all it does for rp2040 without PIO defined is configure UART
  tusb_init();

  keyboard_gpio_init();
  led_init();

  while (true)
  {
    tud_task();
    handle_hid();
  }
}

// the below are borrowed and adapted from https://github.com/raspberrypi/pico-examples/blob/master/usb/device/dev_hid_composite/main.c


// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT) //output means that the host is sending us the report
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        //caps on
        gpio_put(CAPS_LED, true);
      }
      else
      {
        //caps off
        gpio_put(CAPS_LED, false);
      }
    }
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // not implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
// void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
// {
//   // not implemented
//   (void) instance;
//   (void) len;
//   (void) report;
// }

// Invoked when device is mounted
void tud_mount_cb(void)
{
  gpio_put(POWER_LED, true);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  gpio_put(POWER_LED, false);
  gpio_put(CAPS_LED, false);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  can_resume = remote_wakeup_en;

  poll_interval_ms = LONG_POLL_INTERVAL_MS;
  gpio_put(POWER_LED, false);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  poll_interval_ms = DEFAULT_POLL_INTERVAL_MS;
  gpio_put(POWER_LED, true);
}