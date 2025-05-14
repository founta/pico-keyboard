
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
  0,1,2,3,4
};
#define num_rows (5)

const char cols[] = {
  5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22
};
#define num_cols (18)

uint8_t modifier_flags = 0;

//you can only press 6 keys simultaneously. should be ok
#define MAX_NUM_PRESSES (6)
uint8_t key_codes[MAX_NUM_PRESSES] = {0};//{HID_KEY_NONE};
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

//Two function buttons
// one function pressed will convert numbers to F1, F2, etc and also some
// keys to arrow keys
// both functions pressed will turn the arrow key keys into home, end, pgup, pgdown respectively to their direction
#define FN_KEY (0)
bool fn_pressed = false, fn_lock = false; //press both shift and a FN key to toggle locking it
const int fn_row = 2, fn_col = 6;

#define DOUBLE_FN (0)
bool double_fn_pressed = false, double_fn_lock = false;
const int double_fn_row = 1, double_fn_col = 6;


// const char* kb[num_rows][num_cols] = {
//{"esc" , "9"   , "0"   , "1"   , "5"   , "7"   , ""    , "6"   , "2"   , "3"   , "8"   , "4"   , "del" , ""    , ""    , ""    , ""    , "" },
//{"caps", "j"   , "f"   , "m"   , "p"   , "v"   ,"prnts", ";"   , "`"   , "z"   , "/"   , "'"   , "\\"   , ""    , ""    , ""    , ""    , "" },
//{"alt" , "r"   , "s"   , "n"   , "d"   , "b"   ,"fn"   , "-"   , "a"   , "e"   , "i"   , "h"   , "q"   , ""    , ""    , ""    , ""    , "" },
//{""    , ""    , "g"   , "l"   , "c"   , "w"   ,"bkspc", "="   , "u"   , "o"   , "y"   , ""    , ""    , ""    , ""    , ""    , ""    , "" },
//{"ctrl", "x"   , "<"   , ">"   ,"shift", "tab" , "t"   ,"space","enter", "["   , "]"   , "k"   , "code", ""    , ""    , ""    , ""    , "" },
// };

//TODO Fn key support
//TODO macros
void (*kb_func[num_rows][num_cols]) (uint8_t) = {
  {&press, &press, &press, &press, &press, &press, NULL  , &press, &press, &press, &press, &press, &press, NULL  , NULL  , NULL  , NULL  , NULL},
  {&press, &press, &press, &press, &press, &press, NULL  , &press, &press, &press, &press, &press, &press, NULL  , NULL  , NULL  , NULL  , NULL},
  {&modif, &press, &press, &press, &press, &press, NULL  , &press, &press, &press, &press, &press, &press, NULL  , NULL  , NULL  , NULL  , NULL},
  {NULL  , NULL  , &press, &press, &press, &press, &press, &press, &press, &press, &press, NULL  , NULL  , NULL  , NULL  , NULL  , NULL  , NULL},
  {&modif, &press, &press, &press, &modif, &press, &press, &press, &press, &press, &press, &press, &press, NULL  , NULL  , NULL  , NULL  , NULL},
};

#define KB_MATRIX { \
  {HID_KEY_ESCAPE            , HID_KEY_9, HID_KEY_0    , HID_KEY_1     , HID_KEY_5                  , HID_KEY_7    , 0                   , HID_KEY_6        , HID_KEY_2        , HID_KEY_3           , HID_KEY_8            , HID_KEY_4         , HID_KEY_DELETE   , 0, 0, 0, 0, 0}, \
  {HID_KEY_CAPS_LOCK         , HID_KEY_J, HID_KEY_F    , HID_KEY_M     , HID_KEY_P                  , HID_KEY_V    , DOUBLE_FN           , HID_KEY_SEMICOLON, HID_KEY_GRAVE    , HID_KEY_Z           , HID_KEY_SLASH        , HID_KEY_APOSTROPHE, HID_KEY_BACKSLASH, 0, 0, 0, 0, 0}, \
  {KEYBOARD_MODIFIER_LEFTALT , HID_KEY_R, HID_KEY_S    , HID_KEY_N     , HID_KEY_D                  , HID_KEY_B    , FN_KEY              , HID_KEY_MINUS    , HID_KEY_A        , HID_KEY_E           , HID_KEY_I            , HID_KEY_H         , HID_KEY_Q        , 0, 0, 0, 0, 0}, \
  {0                         , 0        , HID_KEY_G    , HID_KEY_L     , HID_KEY_C                  , HID_KEY_W    , HID_KEY_BACKSPACE   , HID_KEY_EQUAL    , HID_KEY_U        , HID_KEY_O           , HID_KEY_Y            , 0                 , 0                , 0, 0, 0, 0, 0}, \
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_X, HID_KEY_COMMA, HID_KEY_PERIOD, KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_SPACE, HID_KEY_TAB         , HID_KEY_T        , HID_KEY_ENTER    , HID_KEY_BRACKET_LEFT, HID_KEY_BRACKET_RIGHT, HID_KEY_K         , HID_KEY_GUI_LEFT , 0, 0, 0, 0, 0}  \
}
//note HID_KEY_GRAVE for ` and ~ . HID_KEY_GUI_LEFT == windows key

uint8_t kb_vals[num_rows][num_cols] = KB_MATRIX;

// by default, keys act the same as usual while the function key is pressed
uint8_t fn_vals[num_rows][num_cols] = KB_MATRIX;
uint8_t double_fn_vals[num_rows][num_cols] = KB_MATRIX;

void init_fn_vals()
{
  #define SET_BOTH_FN_VALS(I,J, VAL) \
  do {                               \
    fn_vals[I][J] = VAL;             \
    double_fn_vals[I][J] = VAL;      \
  } while (0)

  //number keys turn into corresponding function keys
  SET_BOTH_FN_VALS(0,1,  HID_KEY_F9);
  SET_BOTH_FN_VALS(0,2,  HID_KEY_F10);
  SET_BOTH_FN_VALS(0,3,  HID_KEY_F1);
  SET_BOTH_FN_VALS(0,4,  HID_KEY_F5);
  SET_BOTH_FN_VALS(0,5,  HID_KEY_F7);
  SET_BOTH_FN_VALS(0,7,  HID_KEY_F6);
  SET_BOTH_FN_VALS(0,8,  HID_KEY_F2);
  SET_BOTH_FN_VALS(0,9,  HID_KEY_F3);
  SET_BOTH_FN_VALS(0,10, HID_KEY_F8);
  SET_BOTH_FN_VALS(0,11, HID_KEY_F4);
  //escape becomes f11
  SET_BOTH_FN_VALS(0,0,  HID_KEY_F11);
  //del becomes F12
  SET_BOTH_FN_VALS(0,12, HID_KEY_F12);

  //FN(P) becomes printscreen
  SET_BOTH_FN_VALS(1,4,  HID_KEY_PRINT_SCREEN);

  //[ and ] become volume down and volume up, respectively
  SET_BOTH_FN_VALS(4,9,  HID_KEY_VOLUME_UP);
  SET_BOTH_FN_VALS(4,10, HID_KEY_VOLUME_DOWN);


  //arrow keys -- < > C and L become 
  // left, down, right, and up arrows, respectively,
  // when one function key is pressed
  fn_vals[4][2] = HID_KEY_ARROW_LEFT;
  fn_vals[4][3] = HID_KEY_ARROW_DOWN;
  fn_vals[3][4] = HID_KEY_ARROW_RIGHT;
  fn_vals[3][3] = HID_KEY_ARROW_UP;

  //if pressing both function keys, the same keys as above turn into 
  // home, pg down, end, and page up,
  // respectively, when both functions are pressed
  double_fn_vals[4][2] = HID_KEY_HOME;
  double_fn_vals[4][3] = HID_KEY_PAGE_DOWN;
  double_fn_vals[3][4] = HID_KEY_END;
  double_fn_vals[3][3] = HID_KEY_PAGE_UP;
}

uint8_t kb_buf[num_rows][num_cols] = {0};

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

      //8 ms debouncing
      // this rejects spurious key up and does not introduce a delay in reporting the keypress
      kb_buf[i][j] = kb_buf[i][j] << 1;
      kb_buf[i][j] |= gpio_get(col);
      if (kb_buf[i][j])
      {
        void (*func)(uint8_t) = kb_func[i][j];
        if (func)
        {
          uint8_t val = kb_vals[i][j];
          if ((double_fn_lock && fn_lock) || (double_fn_pressed && fn_pressed))
            val = double_fn_vals[i][j];
          else if (fn_lock || double_fn_lock || double_fn_pressed || fn_pressed)
            val = fn_vals[i][j];

          func(val);
        }
      }

    }
    gpio_put(row, false);
  }
}

void check_function_keys()
{
  bool shift_pressed = kb_buf[4][4];
  if (shift_pressed && 
      kb_buf[fn_row][fn_col] && !fn_pressed) //on key down
    fn_lock = !fn_lock;
  if (shift_pressed && 
      kb_buf[double_fn_row][double_fn_col] && !double_fn_pressed) //on key down
    double_fn_lock = !double_fn_lock;

  fn_pressed = kb_buf[fn_row][fn_col];
  double_fn_pressed = kb_buf[double_fn_row][double_fn_col];
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
  check_function_keys();

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
      if (num_keys_pressed != 0 || modifier_flags != 0)
      {
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier_flags, (num_keys_pressed != 0) ? key_codes : NULL);
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
  init_fn_vals();
  keyboard_gpio_init();
  //led_init();

  //board_init(); // TUSB's board init. don't do this. all it does for rp2040 without PIO defined is configure UART
  tusb_init();

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
  poll_interval_ms = DEFAULT_POLL_INTERVAL_MS;
  gpio_put(POWER_LED, true);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  poll_interval_ms = LONG_POLL_INTERVAL_MS;

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
  gpio_put(CAPS_LED, false);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  poll_interval_ms = DEFAULT_POLL_INTERVAL_MS;
  gpio_put(POWER_LED, true);
}