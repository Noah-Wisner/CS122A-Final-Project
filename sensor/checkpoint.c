#include <stdio.h>
#include "pico/stdlib.h"


//LEDs
#define RED_LED 17 
#define GREEN_LED 16 
#define BLUE_LED 15 

//Ultrasonic
#define TRIG 2
#define ECHO 3

//buton 
#define BUTTON 0 

//temperature and humidity sensor
#define DATA 4

//task and SM structure taken from CS120B

//Task struct for concurrent synchSMs implmentations
typedef struct task{
 signed   char state;    //Task's current state
 unsigned long period;     //Task period
 unsigned long elapsedTime;  //Time elapsed since last task tick
 int (*TickFct)(int);    //Task tick function
} task;

const unsigned long TASK1_PERIOD = 200;
const unsigned long TASK2_PERIOD = 100;
const unsigned long TASK3_PERIOD = 2000;
const unsigned long GCD_PERIOD = 100; 


//distance in CM 
int getDistance(){
    gpio_put(TRIG,0);
    sleep_us(2); 
    gpio_put(TRIG, 1); 
    sleep_us(10); 
    gpio_put(TRIG, 0); 

    while (!gpio_get(ECHO));
    uint32_t startTime = time_us_32();

    
    while (gpio_get(ECHO));
    uint32_t endTime = time_us_32();
    

    return (endTime - startTime) / 58; 
}

float getTemp() {
    int data[5] = {0,0,0,0,0};
    uint last = 1; 
    uint j = 0; 

    gpio_set_dir(DATA, 1);
    gpio_pull_up(DATA);
    gpio_put(DATA, 0);
    sleep_ms(20);
    gpio_put(DATA, 1);
    sleep_us(30);
    gpio_set_dir(DATA, 0);

    for (uint i = 0; i < 85; i++) {
        uint count = 0;
        while (gpio_get(DATA) == last) {
            count++;
            sleep_us(1);
            if (count == 255) break;
        }
        last = gpio_get(DATA);
        if (count == 255) break;

        if ((i >= 4) && (i % 2 == 0)) {
            data[j / 8] <<= 1;
            if (count > 40) data[j / 8] |= 1;
            j++;
        }
    }

    if ((j >= 40) && (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))) {
        float temp = (float)(((data[2] & 0x7F) << 8) + data[3]) / 10;
        if (temp > 125) temp = data[2];
        if (data[2] & 0x80) temp = -temp;
        return temp;
    }

    return -1; 

}


enum DOOR_States {DOOR_START, IDLE, PRESS} door_state; 
int TickFct_DOOR(int state); 


enum DISTANCE_States {DIST_START, DIST_IDLE, FRONT} dist_states;
int TickFct_DIST (int state); 

enum TEMP_States {TEMP_START, TEMP_READ} temp_states;
int TickFct_TEMP(int state); 

task tasks[3] = {
    {DOOR_START, TASK1_PERIOD, TASK1_PERIOD, &TickFct_DOOR},
    {DIST_START, TASK2_PERIOD, TASK2_PERIOD, &TickFct_DIST},
    {TEMP_START, TASK3_PERIOD, TASK3_PERIOD, &TickFct_TEMP},
};


int main()
{
    stdio_init_all();

    gpio_init(RED_LED); 
    gpio_set_dir(RED_LED, 1); 

    gpio_init(GREEN_LED); 
    gpio_set_dir(GREEN_LED, 1); 

    gpio_init(BLUE_LED); 
    gpio_set_dir(BLUE_LED, 1); 

    gpio_init(TRIG); 
    gpio_set_dir(TRIG, 1); 

    gpio_init(ECHO); 
    gpio_set_dir(ECHO, 0); 

    gpio_init(BUTTON); 
    gpio_set_dir(BUTTON, 0); 

    gpio_init(DATA); 
    gpio_set_dir(DATA, 0); 

    while (true) {
       for ( unsigned int i = 0; i < 3; i++ ) {                   // Iterate through each task in the task array
            if ( tasks[i].elapsedTime == tasks[i].period ) {           // Check if the task is ready to tick
                tasks[i].state = tasks[i].TickFct(tasks[i].state); // Tick and set the next state for this task
                tasks[i].elapsedTime = 0;                          // Reset the elapsed time for the next tick
            }
            tasks[i].elapsedTime += GCD_PERIOD;                        // Increment the elapsed time by GCD_PERIOD
        }

        sleep_ms(GCD_PERIOD); 
    }
}




int TickFct_DOOR(int state){
    bool press = gpio_get(BUTTON); 

    switch(state){
        case DOOR_START:
            door_state = IDLE; 
            break;
        case IDLE:
            door_state = press ? PRESS : IDLE;
            break; 
        case PRESS:
            door_state = press ? PRESS : IDLE; 
            break;
        default:
            break; 
    }


    switch(door_state){
        case DOOR_START:
            gpio_put(BLUE_LED, 0); 
            break; 
        case IDLE:
            gpio_put(BLUE_LED, 0); 
            break; 
        case PRESS:
            gpio_put(BLUE_LED, 1);
            break;
        default:
            break;  
    }

    return door_state; 
}

int TickFct_DIST(int state){
    int distance = getDistance(); 

    switch(state){
        case DIST_START:
            dist_states = DIST_IDLE; 
            break; 
        case DIST_IDLE:
            dist_states = (distance > 0 && distance < 25) ? FRONT : DIST_IDLE;
            break; 
        case FRONT:
            dist_states = (distance > 0 && distance < 25) ? FRONT : DIST_IDLE;
            break; 
        default:
            break; 
    }

    switch(dist_states){
        case DIST_START:
            gpio_put(GREEN_LED, 0); 
            gpio_put(RED_LED, 0);
            break; 
        case DIST_IDLE:
            gpio_put(GREEN_LED, 1);
            gpio_put(RED_LED, 0);
            break; 
        case FRONT:
            gpio_put(GREEN_LED, 0);
            gpio_put(RED_LED, 1);
            break;
        default:
            break; 
    }

    return dist_states; 
}


int TickFct_TEMP(int state){

    float tempReading = getTemp();

    switch(state){
        case TEMP_START:
            temp_states = TEMP_READ; 
            break; 
        case TEMP_READ:
            temp_states = TEMP_READ; 
            break; 
        default:
            break;
    }

    switch(temp_states){
        case TEMP_START:
            break;
        case TEMP_READ:
            printf("Temperature: %f \n", tempReading); 
            break; 
        default:
            break;
    }

    return temp_states; 
}