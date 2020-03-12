/*  Mosquito I
 *   by Tim D...
 *   analog.sketchbook.101@gmail.com
*/

#include <MozziGuts.h>
#include <Oscil.h>

#include <tables/saw8192_int8.h>
#include <tables/smoothsquare8192_int8.h>

#include <EventDelay.h>
#include <mozzi_midi.h>
#include <LowPassFilter.h>

#define CONTROL_RATE 128 // powers of 2 please

Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> oscOne(SAW8192_DATA);       //audio oscillator
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> oscTwo(SMOOTHSQUARE8192_DATA); //audio oscillator
LowPassFilter lpf;                                                               //low pass filter

// for sequencing gain
EventDelay kGainChangeDelay;
char gain = 1;

// analog pins (for control pots)
// Note: Pin assignment is a bit arbitrary. This order worked fine for the way I laid out the pots
//       on my front panel, but you might find you need a different order to help with wire routing,
//       panel layout etc
const int OSC_ONE_PIN = 2;    // pitch
const int OSC_TWO_PIN=3;      // phase
const int MODA_PIN=0;         // rate
const int MODB_PIN=1;         // legato
const int MODC_PIN=4;         // filter

// digital pins
const int ledPin=4;           // LED
const int upButtonPin = 2;    // up push button
const int downButtonPin = 3;  // down push button

// global vars to handle push button functionality and debouncing
int upButtonState;
int lastUpState = LOW;
int downButtonState;
int lastDownState = LOW;
unsigned long lastUpDebounceTime = 0;    
unsigned long lastDownDebounceTime = 0;  
unsigned long debounceDelay = 50;        // the debounce time; increase if the output flickers

// division constants
// We're doing some common division operations ahead of time so that we can avoid division in the main loops 
// which will slow things down when Mozzi is trying to do its thing. Doing the division here allows us to use
// multiplication in the main loop which is much faster
const float DIV1023 = 1.0 / 1023.0; // division constant for pot value range
const float DIV10 = 1.0 / 10.0;     // division constant for division by 10

// global vars for the arpeggiator stuff
int sequence = 0;               // current sequence playing
int note = 0;                   // current note playing
const int numSequences = 21;    // total number of sequences (must match number of sequences in NOTES array)
const int numNotes = 8;         // total number of notes in each sequence (must all be the same and match length of each sequence in NOTES)

// sequences of midi notes to play back
// add new sequences or modify existing ones to suit
// just make sure you update the numSequences and numNotes variables above to match what you've done
const int NOTES[numSequences][numNotes] = {
                        {36,48,36,48,36,48,36,48},                    // up on two
                        {36,36,36,36,36,36,36,36},                    // flat quarters
                        {36,36,36,48,36,36,36,48},                    // up on four
                        {36,36,48,42,36,36,36,48},                    // dum da dum
                        {48,48,36,48,48,36,48,36},                    // two up one down
                        {36,48,36,48,36,48,36,48},                    // up down
                        {36,36,48,36,36,48,36,48},                    // up on three
                        {36,37,38,39,40,41,42,43},                    // chromatic up
                        {43,42,41,40,39,38,37,36},                    // chromatic down
                        {36,38,39,41,43,44,46,48},                    // major up
                        {48,46,44,43,41,39,38,36},                    // major down
                        {36,38,40,43,45,48,50,52},                    // natural minor up
                        {52,50,48,45,43,40,38,36},                    // natural minor down
                        {36,39,41,42,43,46,48,51},                    // major pentatonic up
                        {51,48,46,43,42,41,39,36},                    // major pentatonic down
                        {36,39,41,42,43,46,48,51},                    // blues up
                        {51,48,46,43,42,41,39,36},                    // blues down
                        {36,40,34,45,48,39,38,46},                    // random one
                        {36,38,35,45,37,43,50,41},                    // random two
                        {36,46,44,37,48,35,42,45},                    // random three
                        {36,44,48,38,39,43,44,38}};                   // random four               
                        
// global control params
int OSC_ONE_OFFSET = 12;      // amount to offset the original midi note (12 is one octave)
int OSC_TWO_OFFSET = 2;       // amount to offset the second midi note from the osc one's midi note (5 is a fifth, two is a second, etc)
int ARP_RATE_MIN = 32;        // minimum arpeggiator rate (in millisecs)
int ARP_RATE_MAX = 1024;      // maximum arpeggiator rate (in millisecs)
int LEGATO_MIN = 32;          // minimum note length (capping at 32 to avoid rollover artifacts)
int LEGATO_MAX = 1024;        // maximum note length 
int LPF_CUTOFF_MIN = 10;      // low pass filter min cutoff frequency
int LPF_CUTOFF_MAX = 245;     // low pass filter max cutoff frequency

void setup(){
  //initialize buttons
  pinMode(upButtonPin, INPUT);
  pinMode(downButtonPin, INPUT);
  pinMode(ledPin,OUTPUT);

  //initialize Mozzi objects
  startMozzi(CONTROL_RATE);
  oscOne.setFreq(48);
  oscTwo.setFreq(51);
  lpf.setResonance(128u);
  kGainChangeDelay.set(1000);
  //Serial.begin(9600); //Serial output can cause audio artifacting so this is commented out by default. Can be uncommented for debugging purposes.
}

void updateControl(){

  // read sequence selector buttons and debounce them
  // using mozziMicros() instead of millis() for timing calls because Mozzi disables millis() timer
  int upreading = digitalRead(upButtonPin);
  int downreading = digitalRead(downButtonPin);
  if (upreading != lastUpState){
    lastUpState = mozziMicros(); 
  }
  if ((mozziMicros() - lastUpDebounceTime) > debounceDelay){
    if (upreading != upButtonState){
      upButtonState = upreading;
      if (upButtonState == HIGH){
        sequence += 1;
        if (sequence >= numSequences){
          // if we get to the end of the sequences we'll rollover back to the first sequence
          sequence = 0;
        }
      }
    }
  }
  if (downreading != lastDownState){
    lastDownState = mozziMicros();
  }
  if ((mozziMicros() - lastDownDebounceTime) > debounceDelay){
    if (downreading != downButtonState){
      downButtonState = downreading;
      if (downButtonState == HIGH){
        sequence -= 1;
        if (sequence < 0){
          // if we get to the first sequence we'll rollover to the last sequence
          sequence = numSequences - 1;
        }
      }
    }
  }
  
  // read pot values
  int oscOne_val = mozziAnalogRead(OSC_ONE_PIN);
  int oscTwo_val = mozziAnalogRead(OSC_TWO_PIN);
  int modA_val = mozziAnalogRead(MODA_PIN);
  int modB_val = mozziAnalogRead(MODB_PIN);
  int modC_val = mozziAnalogRead(MODC_PIN);

  // map pot vals
  // These formulas set the range of values coming from the pots to value ranges that work well with the various control functionality.
  // You'll probably only need to mess with these if you want to expand or offset the ranges to suit your own project's needs
  int oscOne_offset = (OSC_ONE_OFFSET*2) * ((oscOne_val * DIV1023)-0.5);                  // offset of original midi note number +/- 1 octave
  float oscTwo_offset = ((oscTwo_val * DIV1023) * DIV10) * OSC_TWO_OFFSET;                // frequency offset for second oscillator +/- 0.2 oscOne freq
  float modA_freq = ARP_RATE_MIN + (ARP_RATE_MAX * (1-(modA_val * DIV1023)));             // arpeggiator rate from 32 millisecs to ~= 1 sec
  float modB_freq = 1-(modB_val * DIV1023);                                               // legato from 32 millisecs to full on (1 sec)
  int modC_freq = LPF_CUTOFF_MIN + (LPF_CUTOFF_MAX *(modC_val * DIV1023));                // lo pass filter cutoff freq ~=100Hz-8k

  // using an EventDelay to cycle through the sequence and play each note
  kGainChangeDelay.set(modA_freq);                                        // set the delay frequency                                           
  if(kGainChangeDelay.ready()){                                           
      if(gain==0){                                                        // we'll make changes to the oscillator freq when the note is off
        if(note >= numNotes){                                             // if we've reached the end of the sequence, loop back to the beginning
            note = 0;
        }
        // turn on the LED on first note of sequence only
        if (note==0){
          digitalWrite(ledPin,HIGH);  
        }else{
          digitalWrite(ledPin,LOW);
        }
        // set oscillator notes based on current note in sequence
        float noteOne = mtof(NOTES[sequence][note] + oscOne_offset);      // osc one's freq = note plus any offset from user
        float noteTwo = noteOne + oscTwo_offset;                          // osc two's freq = osc one's freq plus user offset
        oscOne.setFreq(noteOne);                                          
        oscTwo.setFreq(noteTwo);
        note += 1;
        
        // setting length of note
        gain = 1;
        kGainChangeDelay.set(modA_freq*(1-modB_freq));                    // set length that note is on based on user legato settings
      }
      else{
          gain = 0;
          kGainChangeDelay.set(modA_freq*modB_freq);                      // set length that note is off based on user legato settings
      }
    kGainChangeDelay.start();                                             // execute the delay specified above
  }
  // setting lo pass cutoff freq
  lpf.setCutoffFreq(modC_freq);                                           // set the lo pass filter cutoff freq per user settings
  
}

int updateAudio(){
  // calculating the output audio signal as follows:
  // 1. Summing the waveforms from the two oscillators
  // 2. Shifting their bits by 1 to keep them in a valid output range
  // 3. Multiplying the combined waveform by the gain (volume)
  // 4. Passing the signal through the low pass filter
  return (char)lpf.next((((oscOne.next() + oscTwo.next())>>2) * gain));
}

void loop(){
  audioHook();
}
