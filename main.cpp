#include "ThisThread.h"
#include "mbed.h"
#include <chrono>
#include <cstdio>
#include "N5110.h"
#include <string> 
#include "Joystick.h" 

#define NOTE_C2  119  
#define NOTE_D2  106
#define NOTE_E2  95
#define NOTE_F2  89
#define NOTE_G2  80
#define NOTE_A2  71
#define NOTE_B2  63
#define NOTE_C1  60

const int C_major[] = {NOTE_C2, NOTE_D2, NOTE_E2, NOTE_F2, NOTE_G2, NOTE_A2, NOTE_B2, NOTE_C1};

N5110 lcd(PC_7, PA_9, PB_10, PB_5, PB_3, PA_10);
Joystick joystick(PA_1, PA_0);

PwmOut speaker(PB_0);
AnalogIn sine_volume(PC_0);
AnalogIn square_volume(PC_1);
AnalogIn playback_speed(PA_4);
DigitalIn joystick_button(PC_3);

//waveform counter
volatile int i = 0;

//input and update variables - state based
float square_volume_in;
float square_volume_current = 0.0;
float sine_volume_in;
float sine_volume_current = 0.0;
float playback_speed_in;
float playback_speed_current = 0.0;
bool joystick_button_in = 0;
bool joystick_button_current = 1;
Direction joystick_direction_in;
Direction joystick_direction_current;

//current position of cursor
int cursor_position_x = 0;
int cursor_position_y = 0;

//waveform lookup arrays
volatile float sine_wave_values[128];
volatile float Square_wave_values[128];

int programmed_note_array[8];

//graphics boolean
bool dirty = false;

//note flag for ISR, note value, note position
bool next_note = false;
int current_note_value;
int current_note_counter_step = 0;

//function definitions
void timer_interrput();
void play_note_interrupt();
void update_screen();
void init_screen();
void calculate_wavetables(); 


void init_screen() {
    lcd.init(LPH7366_1);        
    lcd.setContrast(0.4);      
    lcd.setBrightness(0.3);     
    lcd.clear();
    lcd.refresh();
}

void update_screen(float sine_vol,  float square_vol, float playback_speed) {

    //scale potentiometer values 
    int sine_vol_bar = round((sine_vol/1.0)* 38);
    int square_vol_bar = round((square_vol/1.0)* 38);
    int playback_bar = 76 - (round(playback_speed * 76));

    lcd.clear();

    //draw volume bars & speed indicator
    lcd.drawRect(0, 8 + (38-sine_vol_bar), 2, sine_vol_bar, FILL_BLACK); 
    lcd.drawRect(82, 8 + (38-square_vol_bar), 82, square_vol_bar, FILL_BLACK);
    lcd.drawRect(4, 44, playback_bar, 5, FILL_BLACK);

    //draw joystick cursor
    lcd.drawRect(6 + (9 * cursor_position_x), 5 + (4 * cursor_position_y) , 8, 3, FILL_TRANSPARENT);

    //draw empty note grid
    for(int i = 0; i < 8; i ++) {
        for(int j = 0; j < 8; j ++) {
            lcd.drawRect(5 + (9*j), 4 +(4*i), 10, 5, FILL_TRANSPARENT);
        }
    }

    //draw inputted notes
    for(int i = 0; i < 8; i ++) {
        if (programmed_note_array[i] < 9) {
        lcd.drawRect(5 + (9* i), 4 +(4*(programmed_note_array[i])), 10, 5, FILL_BLACK);
        }
    }

    //draw playhead
    lcd.drawRect(5 + (9 *current_note_counter_step),5, 10, 32, FILL_BLACK);
    lcd.refresh();
};


void calculate_wavetables() {
    //generate sine values
    for(int k=0; k<128; k++) {
        sine_wave_values[k] = ((1.0 + sin((float(k)/128.0*6.28318530717959)))/2.0);
    }
    //generate square values 
    for(int k=0; k<128; k++) {
        if (k < 64) {
            Square_wave_values[k] = 0.0;
        }
        else if (k > 64) {
            Square_wave_values[k] = 1.0;
        }
    }
}


int main()
{
    Ticker Sample_Period;
    Ticker Note_player;
    speaker.period(1.0/200000.0);
    joystick.init();
    joystick_button.mode(PullUp);
    init_screen();
    update_screen(0,0,0);
    calculate_wavetables();

    //populate note array with no-note values 
    for (int i = 0; i<9; i++) {
        programmed_note_array[i] = 9;
    }

    while (true) {

        dirty = false;

        sine_volume_in = sine_volume.read();
        square_volume_in = square_volume.read();
        joystick_direction_in = joystick.get_direction(); 
        joystick_button_in = joystick_button.read();
        playback_speed_in = playback_speed.read();

        //interrupt flag checker for next note to be played
        if (next_note == true) {
            Sample_Period.attach(&timer_interrput, std::chrono::microseconds(C_major[current_note_value]));
            current_note_counter_step ++;

            if (current_note_counter_step >= 8) {
                current_note_counter_step = 0;
              }

            current_note_value = programmed_note_array[current_note_counter_step];
            dirty = true;
            next_note = false;
        }
    
        //Check for changes in joystick direction
        if (joystick_direction_in != joystick_direction_current) {
            joystick_direction_current = joystick_direction_in;

            if (joystick_direction_current == N & cursor_position_y > 0) {
                cursor_position_y--;

            } else if (joystick_direction_current == S & cursor_position_y < 7) {
                cursor_position_y++;

            } else if (joystick_direction_current == E & cursor_position_x < 7) {
                cursor_position_x++;

            } else if (joystick_direction_current == W & cursor_position_x > 0) {
                cursor_position_x--;
            }
            dirty = true;
        }

        //check if joystick button has been pressed - program note
        if (joystick_button_in == 0 & joystick_button_current == 1) {
            if (programmed_note_array[cursor_position_x] != cursor_position_y) {
                programmed_note_array[cursor_position_x] = cursor_position_y;

            } else if (programmed_note_array[cursor_position_x] == cursor_position_y) {
                programmed_note_array[cursor_position_x] = 9;
            }
            dirty = true;
        }
        joystick_button_current = joystick_button_in;

        //check and write changes to sine potentiometer
        if (abs(sine_volume_current - sine_volume_in) > 0.10){
            sine_volume_current = sine_volume_in;
            dirty = true;
        }

        //check and write changes to square potentiometer
        if (abs(square_volume_current - square_volume_in) > 0.05){
            square_volume_current = square_volume_in;
            dirty = true;
        }

        //check and write changes to playback speed potentiometer
        if(abs(playback_speed_current - playback_speed_in) > 0.05) {
            playback_speed_current = playback_speed_in;
            int playbackint =  round(20 +(playback_speed_current * 1000));
            Note_player.attach(&play_note_interrupt, std::chrono::milliseconds(playbackint));
            dirty = true;
        }

        //update screen
        if (dirty) {
            update_screen(sine_volume_current, square_volume_current, playback_speed_current);
            dirty = false;
        }

        ThisThread::sleep_for(30ms);
       
    }
}

//interrupt to play sine & square waveforms
void timer_interrput(void) {
    speaker = (sine_wave_values[i] * sine_volume_current) + (Square_wave_values[i] * square_volume_current);
    i = (i+1) & 0x07F;    
}

//play next note
void play_note_interrupt(void) {
    next_note = true;
}