/*
 * finalproject_ucos2.c
 *
 * Created: 2025-12-15 오후 2:34:10
 * Author : user

 */ 

#include "includes.h"

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define UCHAR  unsigned char
#define USHORT unsigned short

#define ATS75_CONFIG_REG 1
#define ATS75_TEMP_REG   0
#define ATS75_ADDR       0x98  

#define TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define N_TASKS        6

OS_EVENT *Mbox;
OS_EVENT *MsgQueue;
void     *MsgQueueTbl[4];


OS_STK TaskStk[N_TASKS][TASK_STK_SIZE];

UCHAR send[4];

static volatile uint8_t g_alarm = 0;
static volatile uint8_t g_mute  = 2;
static volatile uint8_t g_btn_evt = 0;

#define ALARM_TH_C   30

#define BEEP_ON_MS    200
#define BEEP_OFF_MS   300

static volatile uint8_t buzz_on = 0;
static volatile uint8_t buzzer_status = 0; 

static volatile USHORT g_max_temp = 0;
static volatile uint8_t g_show_max = 0;

void write_twi_1byte_nopreset(UCHAR reg, UCHAR data);
void write_twi_0byte_nopreset(UCHAR reg);
void InitI2C(void);
int  ReadTemperature(void);

void TemperatureTask(void *data);
void FndTask(void *data);
void FndDisplayTask(void *data);
void BuzzerTask(void *data);
void ButtonTask(void *data);
void LedTask(void *data);

ISR(INT4_vect)
{
    g_btn_evt = 1;
    EIFR |= (1<<INTF4); 
}
ISR(INT5_vect)
{
    g_show_max = 1;        
    EIFR |= (1<<INTF5);   
}
ISR(TIMER2_OVF_vect)
{
    if (buzz_on) {
        if (buzzer_status == 0) {
            PORTB |= (1<<4);
            buzzer_status = 1;
        } else {
            PORTB &= ~(1<<4);
            buzzer_status = 0;
        }
        TCNT2 = 96; 
    }
}

int main(void)
{
    OSInit();
    DDRA = 0xFF;  PORTA = 0x00;  
    DDRC = 0xFF;  PORTC = 0x00;  
    DDRG = 0x0F;  PORTG = 0x00;  
    DDRB |= (1<<4); PORTB &= ~(1<<4);
    DDRE &= ~(1<<4); PORTE |= (1<<4); 
    
    EICRB = (EICRB & ~((1<<ISC40)|(1<<ISC41))) | (1<<ISC41);
    EIFR  |= (1<<INTF4);
    EIMSK |= (1<<INT4);
    DDRE &= ~(1<<5);   
    PORTE |= (1<<5);  

    EICRB = (EICRB & ~((1<<ISC50)|(1<<ISC51))) | (1<<ISC51);
    EIFR  |= (1<<INTF5);
    EIMSK |= (1<<INT5);

    OS_ENTER_CRITICAL();
    TCCR0 = 0x07;   
    TIMSK |= (1<<TOIE0);
    TCNT0 = 256 - (CPU_CLOCK_HZ / OS_TICKS_PER_SEC / 1024);
    OS_EXIT_CRITICAL();

    TCCR2 = 0x03;  
    TCNT2 = 96;
    TIMSK |= (1 << TOIE2); 

    Mbox = OSMboxCreate((void*)0);
    MsgQueue = OSQCreate(&MsgQueueTbl[0], 3);

    OSTaskCreate(TemperatureTask, (void*)0, &TaskStk[0][TASK_STK_SIZE-1], 0);
    OSTaskCreate(FndTask,         (void*)0, &TaskStk[1][TASK_STK_SIZE-1], 1);
    OSTaskCreate(FndDisplayTask,  (void*)0, &TaskStk[2][TASK_STK_SIZE-1], 5);
    OSTaskCreate(BuzzerTask,      (void*)0, &TaskStk[3][TASK_STK_SIZE-1], 4);
    OSTaskCreate(ButtonTask,      (void*)0, &TaskStk[4][TASK_STK_SIZE-1], 3);OSTaskCreate(LedTask, (void*)0, &TaskStk[5][TASK_STK_SIZE-1], 2);

    sei();
    OSStart();
    return 0;
}
void InitI2C(void)
{
    PORTD = 3;
    SFIOR &= ~(1 << PUD);
    TWSR = 0;
    TWBR = 32;
    TWCR = _BV(TWEA) | _BV(TWEN);
}
int ReadTemperature(void)
{
    int value;

    TWCR = _BV(TWSTA) | _BV(TWINT) | _BV(TWEN);
    while(!(TWCR & _BV(TWINT)));

    TWDR = ATS75_ADDR | 1;
    TWCR = _BV(TWINT) | _BV(TWEN);
    while(!(TWCR & _BV(TWINT)));

    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWEA);
    while(!(TWCR & _BV(TWINT)));

    value = TWDR;
    TWCR = _BV(TWINT) | _BV(TWEN);
    while(!(TWCR & _BV(TWINT)));

    value = ((value << 8) | TWDR);
    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);

    return value;
}
void LedTask(void *data)
{
    data = data;

    while (1) {
        if (g_alarm) {
            PORTA = 0xFF;             
            OSTimeDlyHMSM(0,0,0,300);  

            PORTA = 0x00;             
            OSTimeDlyHMSM(0,0,0,300); 
        } else {
            PORTA = 0x00;             
            OSTimeDlyHMSM(0,0,0,100);  
        }
    }
}
static volatile USHORT g_temp_raw = 0;

void TemperatureTask(void *data)
{
    data = data;
    InitI2C();
    write_twi_1byte_nopreset(ATS75_CONFIG_REG, 0x00);
    write_twi_0byte_nopreset(ATS75_TEMP_REG);

    while (1) {
        g_temp_raw = (USHORT)ReadTemperature();

        if (g_temp_raw > g_max_temp)
            g_max_temp = g_temp_raw; 

        OSMboxPost(Mbox, (void*)&g_temp_raw);
        OSTimeDlyHMSM(0,0,1,0);
    }
}
void FndTask(void *data)
{
    data = data;
    INT8U err;
    UCHAR value_int, value_deci;
    UCHAR num[4];
    USHORT now_value, disp_value;
    INT8U i;

    while (1) {
        now_value = *(USHORT *)OSMboxPend(Mbox, 0, &err);
        disp_value = (g_show_max) ? g_max_temp : now_value;

        if ((disp_value & 0x8000) != 0x8000) num[3] = 11;
        else {
            num[3] = 10;
            disp_value = (~disp_value) - 1;
        }
        value_int  = (UCHAR)((disp_value & 0x7F00) >> 8);
        value_deci = (UCHAR)(disp_value & 0x00FF);

        num[2] = (value_int / 10) % 10;
        num[1] = value_int % 10;
        num[0] = ((value_deci & 0x80) == 0x80) * 5;

        for (i=0; i<4; i++) send[i] = num[i];
        OSQPost(MsgQueue, (void*)send);
        if (g_show_max) {
          OSTimeDlyHMSM(0,0,2,0);  
          g_show_max = 0;
        }
        else
        {
          g_alarm = (value_int >= ALARM_TH_C) ? 1 : 0;

          if (g_alarm && g_mute==2) g_mute=1;
          if (!g_alarm && g_mute==0) g_mute=2;
          
        }
        OSTimeDlyHMSM(0,0,1,0);
    }
}
void FndDisplayTask(void *data)
{
    data = data;
    UCHAR FND_DATA[] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7c,0x07,0x7f,0x67,0x40,0x00};
    UCHAR fnd_sel[4] = {0x01,0x02,0x04,0x08};
    UCHAR *display = send;
    INT8U i;

    while (1) {
        UCHAR *temp = (UCHAR*)OSQAccept(MsgQueue);
        if (temp != 0) display = temp;

        for (i=0; i<4; i++) {
            PORTC = FND_DATA[display[i]];
            PORTG = fnd_sel[i];
            if (i == 1) PORTC |= 0x80;
            OSTimeDlyHMSM(0,0,0,1);  

        }
    }
}
void BuzzerTask(void *data)
{
    data = data;
    INT8U i;

    while (1) {
        if (g_alarm && g_mute==1) {
            for (i=0; i<3; i++) {
                if (!g_alarm || g_mute!=1) break;

                buzz_on = 1;
                OSTimeDlyHMSM(0,0,0,BEEP_ON_MS);

                buzz_on = 0;
                PORTB &= ~(1<<4);
                OSTimeDlyHMSM(0,0,0,BEEP_OFF_MS);
            }
        } 
        buzz_on = 0;
        PORTB &= ~(1<<4);
        OSTimeDlyHMSM(0,0,0,10); 
    }
}
void ButtonTask(void *data)
{
    data = data;

    while (1) {
        if (g_btn_evt) {
            g_btn_evt = 0;
            if (g_alarm) g_mute = 0;
        }
        OSTimeDlyHMSM(0,0,0,20);
    }
}
void write_twi_1byte_nopreset(UCHAR reg, UCHAR data)
{
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || ((TWSR & 0xf8) != 0x08 && (TWSR & 0xf8) != 0x10));
    TWDR = ATS75_ADDR | 0;
    TWCR = (1<<TWINT) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || (TWSR & 0xf8) != 0x18);
    TWDR = reg;
    TWCR = (1<<TWINT) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || (TWSR & 0xF8) != 0x28);
    TWDR = data;
    TWCR = (1<<TWINT) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || (TWSR & 0xF8) != 0x28);
    TWCR = (1<<TWINT) | (1<<TWSTO) | (1<<TWEN);
}

void write_twi_0byte_nopreset(UCHAR reg)
{
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || ((TWSR & 0xf8) != 0x08 && (TWSR & 0xf8) != 0x10));
    TWDR = ATS75_ADDR | 0;
    TWCR = (1<<TWINT) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || (TWSR & 0xf8) != 0x18);
    TWDR = reg;
    TWCR = (1<<TWINT) | (1<<TWEN);
    while(((TWCR & (1<<TWINT)) == 0x00) || (TWSR & 0xF8) != 0x28);
    TWCR = (1<<TWINT) | (1<<TWSTO) | (1<<TWEN);
}
