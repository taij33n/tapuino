#include <avr/io.h>
#include <inttypes.h>
#include "tapuino.h"
#include "config.h"
#include "comms.h"
#include <Arduino.h>

volatile uint8_t g_cur_command = COMMAND_IDLE;

#ifdef USE_ROTARY_ENCODER

#define BTN_RELEASED 0
#define BTN_PRESSED 1

#define  pressDelay   256
#define  releaseDelay 64

unsigned char rotaryState;
unsigned char oldRotaryState;
unsigned long pressTime = 0;

unsigned char button;
unsigned char oldButton = 0;
unsigned char buttonState = BTN_RELEASED;
#else

// repeat needed on all keys for short/long on select/abort and repeat on prev/next
#define REPEAT_MASK     (_BV(KEY_SELECT_PIN) | _BV(KEY_ABORT_PIN) | _BV(KEY_PREV_PIN) | _BV(KEY_NEXT_PIN))
volatile unsigned char  key_press = 0;
volatile unsigned char  key_state = 0;
volatile unsigned char  key_rpt = 0;

#endif



#ifdef USE_ROTARY_ENCODER
void handle_encoder() {

  rotaryState = digitalRead(rotaryCLK); 
  // states differ => pulse occured
  if (rotaryState != oldRotaryState) {
    // If the rotaryDT state is different to the rotaryCLK state, that means the encoder is rotating clockwise
    // DT != CLK => rotating clockwise
    if (digitalRead(rotaryDT) != rotaryState) {
      g_cur_command = COMMAND_NEXT;
    } else {
      g_cur_command = COMMAND_PREVIOUS;
    }
  }
  oldRotaryState = rotaryState; 

  button = digitalRead(rotarySwitch);
  if (oldButton != button) {

    // is button pressed ?
    if (button == LOW) {
      pressTime = millis();
      buttonState = BTN_PRESSED;
    }
  } else {
    
    if (buttonState == BTN_PRESSED) {
      // is button pressed ?
      if (button == LOW) {
        if ((millis() - pressTime) > pressDelay) {
          g_cur_command = COMMAND_ABORT;
          buttonState = BTN_RELEASED;
        }
      } else {
        // button released
        if ((millis() - pressTime) > releaseDelay) {
          g_cur_command = COMMAND_SELECT;
          buttonState = BTN_RELEASED;
        }
      }
    }
  }
  oldButton = button;

}
#else

/*--------------------------------------------------------------------------
  FUNC: 8/1/11 - Used to read debounced button presses
  PARAMS: A keymask corresponding to the pin for the button you with to poll
  RETURNS: A keymask where any high bits represent a button press
  --------------------------------------------------------------------------*/
unsigned char get_key_press( unsigned char key_mask ) {
  //  cli();			// read and clear atomic !
  key_mask &= key_press;	// read key(s)
  key_press ^= key_mask;	// clear key(s)
  //  sei();
  return key_mask;
}

/*--------------------------------------------------------------------------
  FUNC: 8/1/11 - Used to check for debounced buttons that are held down
  PARAMS: A keymask corresponding to the pin for the button you with to poll
  RETURNS: A keymask where any high bits is a button held long enough for
		its input to be repeated
  --------------------------------------------------------------------------*/
unsigned char get_key_rpt( unsigned char key_mask ) {
  //  cli();               // read and clear atomic !
  key_mask &= key_rpt;                           // read key(s)
  key_rpt ^= key_mask;                           // clear key(s)
  //  sei();
  return key_mask;
}

/*--------------------------------------------------------------------------
  FUNC: 8/1/11 - Used to read debounced button released after a short press
  PARAMS: A keymask corresponding to the pin for the button you with to poll
  RETURNS: A keymask where any high bits represent a quick press and release
  --------------------------------------------------------------------------*/
unsigned char get_key_short( unsigned char key_mask )
{
  //cli();         // read key state and key press atomic !
  return get_key_press( ~key_state & key_mask );
}

/*--------------------------------------------------------------------------
  FUNC: 8/1/11 - Used to read debounced button held for REPEAT_START amount
	of time.
  PARAMS: A keymask corresponding to the pin for the button you with to poll
  RETURNS: A keymask where any high bits represent a long button press
  --------------------------------------------------------------------------*/
unsigned char get_key_long( unsigned char key_mask )
{
  return get_key_press( get_key_rpt( key_mask ));
}

void player_handleInputKeys() {
  if (get_key_short(_BV(KEY_SELECT_PIN))) {
    g_cur_command = COMMAND_SELECT;
  }

  if (get_key_short(_BV(KEY_ABORT_PIN))) {
    g_cur_command = COMMAND_ABORT;
  }

  if (get_key_long(_BV(KEY_SELECT_PIN))) {
    g_cur_command = COMMAND_SELECT_LONG;
  }

  if (get_key_long(_BV(KEY_ABORT_PIN))) {
    g_cur_command = COMMAND_ABORT_LONG;
  }

  if (get_key_press(_BV(KEY_PREV_PIN)) || get_key_rpt(_BV(KEY_PREV_PIN))) {
    g_cur_command = COMMAND_PREVIOUS;
  }

  if (get_key_press(_BV(KEY_NEXT_PIN)) || get_key_rpt(_BV(KEY_NEXT_PIN))) {
    g_cur_command = COMMAND_NEXT;
  }
}
#endif

void input_callback()
{

#ifdef USE_ROTARY_ENCODER
  handle_encoder();
#else
  static unsigned char ct0, ct1, rpt;
  unsigned char i;

  // key changed ?
#ifdef KEYS_INPUT_PULLUP
  i = key_state ^ ~KEYS_READ_PINS;  // HW V2.0 for internal pullup the reading is inverted
#else
  i = key_state ^ KEYS_READ_PINS;   // HW V1.0 normal read
#endif

  ct0 = ~( ct0 & i );          // reset or count ct0
  ct1 = ct0 ^ (ct1 & i);       // reset or count ct1
  i &= ct0 & ct1;              // count until roll over ?
  key_state ^= i;              // then toggle debounced state
  key_press |= key_state & i;  // 0->1: key press detect

  if ( (key_state & REPEAT_MASK) == 0 )  // check repeat function
    rpt = (KEY_REPEAT_START / 10);           // start delay
  if ( --rpt == 0 ) {
    rpt = g_key_repeat_next;             // repeat delay
    key_rpt |= key_state & REPEAT_MASK;
  }
  player_handleInputKeys();
#endif
}

void comms_setup() {
#ifdef USE_ROTARY_ENCODER
  pinMode (rotaryCLK, INPUT);
  pinMode (rotaryDT, INPUT);
  pinMode (rotarySwitch, INPUT_PULLUP);
  g_cur_command = COMMAND_IDLE;

#else
  // keys are all inputs, activate pullups
  KEYS_READ_DDR &= ~_BV(KEY_SELECT_PIN);
  KEYS_READ_PORT |= _BV(KEY_SELECT_PIN);

  KEYS_READ_DDR &= ~_BV(KEY_ABORT_PIN);
  KEYS_READ_PORT |= _BV(KEY_ABORT_PIN);

  KEYS_READ_DDR &= ~_BV(KEY_PREV_PIN);
  KEYS_READ_PORT |= _BV(KEY_PREV_PIN);

  KEYS_READ_DDR &= ~_BV(KEY_NEXT_PIN);
  KEYS_READ_PORT |= _BV(KEY_NEXT_PIN);
#endif
}
