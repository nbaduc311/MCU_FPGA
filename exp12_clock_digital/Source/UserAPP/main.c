/******************** (C) COPYRIGHT 2023 SONiX *******************************
* COMPANY:		SONiX
* ... (C�c comment ti�u d? gi? nguy�n) ...
*****************************************************************************/

/*_____ I N C L U D E S ____________________________________________________*/
#include <SN32F400.h>
#include <SN32F400_Def.h>
#include "..\Driver\GPIO.h"
#include "..\Driver\CT16B0.h"
#include "..\Driver\WDT.h"
#include "..\Driver\Utility.h"
#include "..\Driver\I2C.h"
#include "..\Module\Segment.h"
#include "..\Module\KeyScan.h"
#include "..\Module\EEPROM.h"
#include "..\Module\Buzzer.h"
#include "..\Driver\SysTick.h"

/*_____ D E C L A R A T I O N S ____________________________________________*/
void PFPA_Init(void);
void NotPinOut_GPIO_init(void);

/*_____ D E F I N I T I O N S ______________________________________________*/
#ifndef	SN32F407					
	#error Please install SONiX.SN32F4_DFP.0.0.18.pack or version >= 0.0.18
#endif
#define	PKG						SN32F407				

#define	EEPROM_WRITE_ADDR			0xA0
#define	EEPROM_READ_ADDR			0xA1

/*_____ M A C R O S ________________________________________________________*/

/*_____ F U N C T I O N S __________________________________________________*/

// 1. Bi?n th?i gian th?c
uint8_t hour = 0;
uint8_t minute = 0;
uint8_t second = 0;

// 2. M?I: Bi?n d?m d�ng khi Setup gi? ph�t
uint8_t setup_hour = 0;
uint8_t setup_minute = 0;
uint8_t setup_changed = 0;  // C? d�nh d?u: 1 = C� b?m ch?nh s?a, 0 = Kh�ng l�m g�

// 2. Bi?n h?n gi? (Alarm)
uint8_t alarm_hour = 0;
uint8_t alarm_minute = 0;

// 3. Bi?n qu?n l� tr?ng th�i h? th?ng
// 0: B�nh thu?ng
// 1: Ch?nh Gi? th?c  | 2: Ch?nh Ph�t th?c
// 3: H?n Gi? (Alarm) | 4: H?n Ph�t (Alarm)
uint8_t clock_mode = 0;
uint16_t setup_idle_timer = 0;     

// 4. Bi?n t?o hi?u ?ng nh?p nh�y
uint16_t blink_counter = 0; 
uint8_t blink_state = 1;    
uint16_t read_key = 0;

// 6. Non-blocking EEPROM state
uint8_t save_alarm_state = 0;
uint16_t save_alarm_timer = 0;
uint8_t alarm_buf[2]; 

// 7. Bien dieu khien coi bao thuc
uint16_t alarm_ring_timer = 0;   // 0 = tat; > 0 = so ms con lai
uint16_t alarm_pip_counter = 0;  // Dem ms trong moi chu ky 1000ms (500ms ON, 500ms OFF)

/*****************************************************************************
* Function		: main
*****************************************************************************/
int	main(void)
{
	SystemInit();
	SystemCoreClockUpdate();				
	PFPA_Init();										
	NotPinOut_GPIO_init();

	SN_SYS0->EXRSTCTRL_b.RESETDIS = 0;
	
	GPIO_Init();								
	WDT_Init();		
	I2C0_Init();							
	CT16B0_Init();
	SysTick_Init();

	SN_PFPA->CT16B0_b.PWM0 = 1;

	while(!eeprom_check(EEPROM_WRITE_ADDR));
	eeprom_read(EEPROM_READ_ADDR, 0x02, &alarm_hour, 1);

	while(!eeprom_check(EEPROM_WRITE_ADDR));
	eeprom_read(EEPROM_READ_ADDR, 0x03, &alarm_minute, 1);

	if(alarm_hour > 23) alarm_hour = 0;
	if(alarm_minute > 59) alarm_minute = 0;
	
	while (1)
	{
		__WDT_FEED_VALUE;
		
		// =========================================================
		// X? L� NH?P 1 GI�Y (�?M �?NG H?)
		// =========================================================
		if(timer_1s_flag)
		{
			timer_1s_flag = 0;
			
			second++;
			if(second >= 60)
			{
				second = 0;
				minute++;
				if(minute >= 60)
				{
					minute = 0;
					hour++;
					if(hour >= 24) hour = 0;
				}
				if (hour == alarm_hour && minute == alarm_minute)
				{
					if (alarm_ring_timer == 0)
					{
						alarm_ring_timer  = 5000; // Tổng thời gian kêu = 5 giây
						alarm_pip_counter = 0;
					}
				}
			}
			
			// --- Tự động thoát Setup nếu Rảnh tay 30 giây ---
			if (clock_mode != 0) 
			{
				if (setup_idle_timer > 0) setup_idle_timer--;
				
				if (setup_idle_timer == 0) 
				{
					// Nếu đang chỉnh giờ thực và có thay đổi thì lưu lại
					if ((clock_mode == 1 || clock_mode == 2) && setup_changed == 1) 
					{
						hour = setup_hour;
						minute = setup_minute;
						second = 0;
					}
					// Nếu đang chỉnh báo thức thì lưu vào EEPROM
					if (clock_mode == 3 || clock_mode == 4) 
					{
						save_alarm_state = 1; 
					}
					
					// Thoát về mode bình thường
					clock_mode = 0;
					blink_state = 1; 
					blink_counter = 0;
					
					// Kêu báo hiệu 0.3s
					alarm_ring_timer = 300; 
					alarm_pip_counter = 0;
				}
			}
		}
		
		// =========================================================
		// X? L� NH?P 1 MILI-GI�Y (QU�T LED & PH�M)
		// =========================================================
		if(timer_1ms_flag)
		{
			timer_1ms_flag = 0;
			
			if (alarm_ring_timer > 0)
			{
				alarm_ring_timer--;
				alarm_pip_counter++;
				
				if (alarm_pip_counter < 500)
				{
					buzzer_on();   // Kêu trong 500ms đầu
				}
				else if (alarm_pip_counter < 1000)
				{
					buzzer_off();  // Tắt trong 500ms sau
				}
				else
				{
					alarm_pip_counter = 0; // Reset lại vòng lặp 1 giây
				}
				
				if (alarm_ring_timer == 0)
				{
					buzzer_off();  // Vừa hết 5 giây -> Tắt hẳn còi
					alarm_pip_counter = 0;
				}
			}

			// --- 0. EEPROM Non-blocking SM voi ACK Polling ---
			if (save_alarm_state > 0)
			{
				if (save_alarm_state == 1)
				{
					// Check EEPROM ready truoc khi ghi (ACK Polling)
					if (eeprom_check(EEPROM_WRITE_ADDR))
					{
						// Ghi ca 2 bien cung 1 lan (page write 2 byte lien tiep)
						alarm_buf[0] = alarm_hour;
						alarm_buf[1] = alarm_minute;
						eeprom_write(EEPROM_WRITE_ADDR, 0x02, alarm_buf, 2);
						save_alarm_state = 2; // Buoc sang cho ACK Polling xac nhan
					}
				}
				else if (save_alarm_state == 2)
				{
					if (eeprom_check(EEPROM_WRITE_ADDR)) // Da ghi xong ca Hour va Minute
					{
						save_alarm_state = 0; // Ket thuc luong
					}
				}
			}
			
			// --- 1. T?o hi?u ?ng nh?p nh�y 0.5s ---
			blink_counter++;
			if(blink_counter >= 500)
			{
				blink_counter = 0;
				blink_state = !blink_state; 
			}
			
			// --- Bật/Tắt LED D6 cảnh báo theo chế độ Hẹn Giờ ---
			if (clock_mode == 3 || clock_mode == 4)
			{
				if (blink_state) SET_LED0_ON;
				else SET_LED0_OFF;
			}
			else
			{
				SET_LED0_OFF;
			}
			
			// --- 2. Qu�t Ph�m ---
			read_key = KeyScan();
			if (read_key) 
			{
				setup_idle_timer = 30 * debug_speed; // 30 giây thực, bất kể tốc độ debug
				// --- KEY 1: N�T SET TIME ---
				if (read_key == (KEY_PUSH_FLAG | KEY_1)) 
				{
					alarm_ring_timer = 300; // <---- 300 mili-giây
					alarm_pip_counter = 0;
					if(clock_mode == 0) 
					{
						clock_mode = 1;       		// V�o mode ch?nh Gi?
						setup_hour = hour;    		// Copy th?i gian th?c v�o bi?n d?m
						setup_minute = minute;
						setup_changed = 0;    		// Reset c? thay d?i
					}
					else if(clock_mode == 1) 
					{
						clock_mode = 2;  			// Gi? -> Ph�t
					}
					else 
					{
						clock_mode = 0;             // Tho�t v? ch? d? thu?ng
						
						// Ch? c?p nh?t gi? th?c n?u ngu?i d�ng c� b?m tang/gi?m
						if (setup_changed == 1) 
						{
							hour = setup_hour;
							minute = setup_minute;
							second = 0; 			// Reset gi�y v? 0 cho chu?n
						}
					}
					
					blink_state = 1; blink_counter = 0; 
				}
				// --- KEY 13: N�T ALARM ---
				else if (read_key == (KEY_PUSH_FLAG | KEY_13)) 
				{
					alarm_ring_timer = 300; // <---- 300 mili-giây
					alarm_pip_counter = 0;
					// N?u dang ch?nh gi? th?c m� b?m tho�t sang Alarm, v?n ph?i luu gi? l?i (n?u c� d?i)
					if ((clock_mode == 1 || clock_mode == 2) && setup_changed == 1) 
					{
						hour = setup_hour;
						minute = setup_minute;
						second = 0;
					}

					if(clock_mode == 0) clock_mode = 3;       
					else if(clock_mode == 3) clock_mode = 4;  
					else 
					{
						clock_mode = 0;
						save_alarm_state = 1; // Kich hoat State Machine ghi EEPROM
					}
					
					blink_state = 1; blink_counter = 0;
				}
				// --- KEY 4: N�T TANG (UP) ---
				else if (read_key == (KEY_PUSH_FLAG | KEY_4)) 
				{
					alarm_ring_timer = 300; // <---- 300 mili-giây
					alarm_pip_counter = 0;
					if (clock_mode == 1) { setup_hour++; if(setup_hour >= 24) setup_hour = 0; setup_changed = 1; }
					else if (clock_mode == 2) { setup_minute++; if(setup_minute >= 60) setup_minute = 0; setup_changed = 1; }
					else if (clock_mode == 3) { alarm_hour++; if(alarm_hour >= 24) alarm_hour = 0; }
					else if (clock_mode == 4) { alarm_minute++; if(alarm_minute >= 60) alarm_minute = 0; }
					
					blink_state = 1; blink_counter = 0; 
				}
				// --- KEY 8: N�T GI?M (DOWN) ---
				else if (read_key == (KEY_PUSH_FLAG | KEY_8)) 
				{
					alarm_ring_timer = 300; // <---- 300 mili-giây
					alarm_pip_counter = 0;
					if (clock_mode == 1) { if(setup_hour == 0) setup_hour = 23; else setup_hour--; setup_changed = 1; }
					else if (clock_mode == 2) { if(setup_minute == 0) setup_minute = 59; else setup_minute--; setup_changed = 1; }
					else if (clock_mode == 3) { if(alarm_hour == 0) alarm_hour = 23; else alarm_hour--; }
					else if (clock_mode == 4) { if(alarm_minute == 0) alarm_minute = 59; else alarm_minute--; }
					
					blink_state = 1; blink_counter = 0;
				}
				// --- KEY 16: BẬT/TẮT DEBUG MODE (x200 SPEED) ---
				else if (read_key == (KEY_PUSH_FLAG | KEY_16)) 
				{
					alarm_ring_timer = 300; 
					alarm_pip_counter = 0;
					
					if (debug_speed == 1) debug_speed = 200; // Bật Debug
					else debug_speed = 1;                    // Tắt Debug
				}
			}
			
			// --- 3. Ch?n ngu?n d? li?u hi?n th? ---
			if (clock_mode == 3 || clock_mode == 4) 
			{
				Digital_Display_Clock(alarm_hour, alarm_minute); // Hi?n gi? b�o th?c
			} 
			else if (clock_mode == 1 || clock_mode == 2)
			{
				Digital_Display_Clock(setup_hour, setup_minute); // Hi?n b? d?m dang ch?nh s?a
			}
			else 
			{
				Digital_Display_Clock(hour, minute); // Hi?n gi? th?c dang ch?y
			}
			
			// --- 4. �p d?ng hi?u ?ng nh?p nh�y cho c?m LED dang du?c ch?nh s?a ---
			if ((clock_mode == 1 || clock_mode == 3) && blink_state == 0) 
			{
				segment_buff[0] = 0x00; // T?t Gi?
				segment_buff[1] = 0x00; 
			}
			else if ((clock_mode == 2 || clock_mode == 4) && blink_state == 0) 
			{
				segment_buff[2] = 0x00; // T?t Ph�t
				segment_buff[3] = 0x00; 
			}
			
			// --- 5. �?y t�n hi?u ra ch�n GPIO ---
			Digital_Scan();
		}
	}
}

/*****************************************************************************
* Function		: NotPinOut_GPIO_init
* Description	: Set the status of the GPIO which are NOT pin-out to input pull-up. 
* Input				: None
* Output			: None
* Return			: None
* Note				: 1. User SHALL define PKG on demand.
*****************************************************************************/
void NotPinOut_GPIO_init(void)
{
#if (PKG == SN32F405)
	//set P0.4, P0.6, P0.7 to input pull-up
	SN_GPIO0->CFG = 0x00A008AA;
	//set P1.4 ~ P1.12 to input pull-up
	SN_GPIO1->CFG = 0x000000AA;
	//set P3.8 ~ P3.11 to input pull-up
	SN_GPIO3->CFG = 0x0002AAAA;
#elif (PKG == SN32F403)
	//set P0.4 ~ P0.7 to input pull-up
	SN_GPIO0->CFG = 0x00A000AA;
	//set P1.4 ~ P1.12 to input pull-up
	SN_GPIO1->CFG = 0x000000AA;
	//set P2.5 ~ P2.6, P2.10 to input pull-up
	SN_GPIO2->CFG = 0x000A82AA;
	//set P3.0, P3.8 ~ P3.13 to input pull-up
	SN_GPIO3->CFG = 0x0000AAA8;
#endif
}

/*****************************************************************************
* Function		: HardFault_Handler
* Description	: ISR of Hard fault interrupt
* Input			: None
* Output		: None
* Return		: None
* Note			: None
*****************************************************************************/
void HardFault_Handler(void)
{
	NVIC_SystemReset();
}