/* ========================================
 *
 * PSoC5 LP UART Controlled USB Keyboard.
 * Programmed By Minatsu, 2017.
 *
 * ========================================
 */
#include "project.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/**************************************
 * HID(Keyboard)
 *************************************/
#define LSHIFT 0x02
#define ENTER 0x28

// Scan Code Look Up Table for the various ASCII values
//                                      0      1      2      3      4      5      6      7      8      9      a      b      c      d      e      f
const uint16 aASCII_ToScanCode[] = {0x02C, 0x11E, 0x11F, 0x120, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x134, 0x133, 0x036, 0x02d, 0x037, 0x038,
                                    0x027, 0x01E, 0x01F, 0x020, 0x021, 0x022, 0x023, 0x024, 0x025, 0x026, 0x034, 0x033, 0x136, 0x12d, 0x137, 0x138,
                                    0x02f, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D, 0x10E, 0x10F, 0x110, 0x111, 0x112,
                                    0x113, 0x114, 0x115, 0x116, 0x117, 0x118, 0x119, 0x11A, 0x11B, 0x11C, 0x11D, 0x030, 0x089, 0x032, 0x02e, 0x187,
                                    0x12f, 0x004, 0x005, 0x006, 0x007, 0x008, 0x009, 0x00A, 0x00B, 0x00C, 0x00D, 0x00E, 0x00F, 0x010, 0x011, 0x012,
                                    0x013, 0x014, 0x015, 0x016, 0x017, 0x018, 0x019, 0x01A, 0x01B, 0x01C, 0x01D, 0x130, 0x189, 0x132, 0x12e};

/* Array of Keycode information to send to PC */
static unsigned char Keyboard_Data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static unsigned char Stream_Data[8];

/* Max keys which are simultaneously sent. */
#define MAX_KEYS_IN_PACKET 6

/* Current packet position. */
static uint8 cur_pos = 0;

/* Serial overflow flag. */
static uint8 isOverflow = 0u;


/**************************************
 * UART
 *************************************/
/* XOFF:Ctrl+S(0x13) XON:Ctrl+Q(0x11) */
#define XOFF                (0x13u)
#define XON                 (0x11u)


/**************************************
 * USB
 *************************************/
/* USB Host and LED status */
typedef enum {
    OffLine,
    OnLine,
    ActIn,
    ActOut
} STATUS;

/* USB host status. */
static uint host_status = OffLine;
static uint8 usb_offline_flag = 0u;


/**************************************
 * Debug support facilities
 *************************************/
/* Debug Print */
char dbuf[256];
#define DP(...)             {sprintf(dbuf, __VA_ARGS__); UART_PutString(dbuf);}

/* EZI2C monitor */
struct _EZI2C_buf {
    uint16 sofCount;
    uint16 timerCount;
} EZI2C_buf;


/**************************************
 * Function Prototypes
 *************************************/
void In_EP(void);
void pushKey(uint8 mod, uint8 code);
void flushPacket(void);
void sendRawKey(void);
void sendStream(void);
void setStatus(STATUS status);
uint8 waitAck(void);
CY_ISR(isr_capture_handler);


/**************************************
 * Functions
 *************************************/
int main(void) {
    /* Enable global interrupts. */
    CyGlobalIntEnable;

    /* Start Debug Port */
    UART_Start();
    DP("\n\nPSoC5LP UART Controlled USB Keyboard. Programmed By Minatsu, 2017-2018.\n");

    /* Start EZI2C monitoring slave. */
    EZI2C_buf.sofCount = 0;
    EZI2C_buf.timerCount = 0;
    EZI2C_SetBuffer1(sizeof(EZI2C_buf), 0, (void *)&EZI2C_buf);
    EZI2C_Start();

    /* Start sof capture handler. */
    isr_capture_StartEx(isr_capture_handler);

    /* Start PWM for onboard LED. */
    PWM_LED_Start();
    setStatus(OffLine);

    /*Start USBFS Operation of Device 0 with 5V operation*/
    USBFS_Start(0, USBFS_DWR_VDDD_OPERATION);

    /* Start USB SoF Counter. */
    SoFCounter_Start();

    /*******************************************************************************
     * MAIN LOOP
     *******************************************************************************/
    for (;;) {
        if (USBFS_IsConfigurationChanged()) {
            /*Wait for USB to be enumerated*/
            while (!USBFS_bGetConfiguration()) ;
            DP("USB connected.\n");
            /*Begins USB Traffic*/
            for (uint8 i = 0; i < 8; i++) Keyboard_Data[i] = 0;
            USBFS_LoadInEP(1, Keyboard_Data, 8);
            setStatus(OnLine);
        }

        if (host_status == OnLine && usb_offline_flag) {
            setStatus(OffLine);
        }

        /*Checks for ACK from host*/
        if (host_status == OnLine && waitAck()) {
            /*Function to push and send data to host*/
            setStatus(ActOut);
            In_EP();
            setStatus(ActIn);
        }

        /**************************************
         * Update EZI2C monitoring values.
         **************************************/
        if ( (EZI2C_GetActivity() & EZI2C_STATUS_BUSY) == 0u ) {
            EZI2C_buf.timerCount = 0u;
            EZI2C_buf.sofCount = SoFCounter_ReadCapture();
        }
    }
}

/* Set host status, and control on-board. */
#define PWM_HIGH (99u)
void setStatus(STATUS status) {
    switch (status) {
    case OffLine:
        PWM_LED_WriteCompare(0);
        host_status = OffLine;
        usb_offline_flag = 0u;
        break;

    case OnLine:
        PWM_LED_WriteCompare(PWM_HIGH);
        host_status = OnLine;
        break;

    case ActIn:
        if (host_status == OnLine) {
            PWM_LED_WriteCompare(0);
        }
        break;

    case ActOut:
        if (host_status == OnLine) {
            PWM_LED_WriteCompare(PWM_HIGH);
        }
        break;
    }
}

/* In EP handler. */
void In_EP(void) {
    uint8 c;

    if (UART_GetRxBufferSize()) {
        /* Receive 1 byte from UART, and push ScanCode into the packet. */
        c = UART_GetByte();
        if (c == 0x00) {
            sendRawKey();
        } else if (c == 0xff) {
            sendStream();
        } else if ((c >= 0x20) && (c <= 0x7E)) {
            uint16 key = aASCII_ToScanCode[c-0x20];
            pushKey((key & 0x100) ? LSHIFT : 0x00, key & 0xff);
        } else if (c == 0x0d) {
            pushKey(0x00, ENTER);
        }
    } else {
        /* When there is no input, flush the packet. */
        if (cur_pos) {
            flushPacket();
        } 
    }

    /* Software flow control. */
    if (!isOverflow && (UART_GetRxBufferSize() > UART_RX_BUFFER_SIZE/2)) {
        UART_PutChar(XOFF);
        isOverflow = 1u;
        //DP("XOFF\n");
    }
    if (isOverflow && (UART_GetRxBufferSize() < UART_RX_BUFFER_SIZE/4)) {
        UART_PutChar(XON);
        isOverflow = 0u;
        //DP("XON\n");
    }
}

/* Push raw key. */
void sendRawKey() {
    uint8 i;
    for (i = 0; i < 8; i++) {
        Stream_Data[i] = 0;
    }

    while (!UART_GetRxBufferSize()) ;
    Stream_Data[0] = UART_GetByte(); // mod
    while (!UART_GetRxBufferSize()) ;
    Stream_Data[2] = UART_GetByte(); // code

    setStatus(ActIn);
    USBFS_LoadInEP(1, Stream_Data, 8);
    waitAck();
    setStatus(ActOut);
}

/* send stream packet */
void sendStream() {
    while (!UART_GetRxBufferSize()) ;
    Stream_Data[0] = UART_GetByte();
    Stream_Data[1] = 0;

    for (int i = 0; i < 6; i++) {
        while (!UART_GetRxBufferSize()) ;
        Stream_Data[2+i] = UART_GetByte();
    }

    setStatus(ActIn);
    USBFS_LoadInEP(1, Stream_Data, 8);
    waitAck();
    //CyDelay(7);
    setStatus(ActOut);
}

/* Push key into the packet. */
void pushKey(uint8 mod, uint8 code) {
    uint8 i;

    /* Check if the same code is already in the packet. */
    for (i = 0; i < cur_pos; i++) {
        if (Keyboard_Data[2+i] == code) {
            flushPacket();
            break;
        }
    }

    /* If the packet is not empty and the modifier is changed, flush the packet. */
    if (cur_pos != 0 && Keyboard_Data[0] != mod) {
        flushPacket();
    }

    Keyboard_Data[0] = mod;
    Keyboard_Data[2+(cur_pos++)] = code;

    if (cur_pos == MAX_KEYS_IN_PACKET) {
        flushPacket();
    }
}

/* Flush the packet. */
void flushPacket() {
    uint8 i;

    if (!cur_pos) {
        return;
    }

    setStatus(ActIn);
    /*Loads EP1 for an IN transfer to PC*/
    USBFS_LoadInEP(1, Keyboard_Data, 8);
    waitAck();
    CyDelay(15);

    /* Clear the packet. */
    for (i = 0; i < 8; i++) {
        Keyboard_Data[i] = 0x00;
    }

    /*Loads EP1 for an IN transfer to PC. This simulates the buttons being released.*/
    USBFS_LoadInEP(1, Keyboard_Data, 8);
    waitAck();
    //CyDelay(15);
    setStatus(ActOut);

    /* Reset current position in the packet. */
    cur_pos = 0;
}

/* Wait EP Ack with timeout feature. */
uint8 waitAck() {
    if (host_status != OnLine) {
        return 0u;
    }

    //while (!USBFS_bGetEPAckState(1) && SoFTimer_ReadCounter()!=0u);
    while (!USBFS_bGetEPAckState(1) && !usb_offline_flag) ;

    if (usb_offline_flag) {
        setStatus(OffLine);
        DP("USB disconnected.\n");
        return 0u;
    }
    return 1u;
}

/**************************************
 *  Interrupt Handlers
 *************************************/
#define SOF_MAX_COUNT (100u)
#define SOF_OFFLINE_THRESHOLD (SOF_MAX_COUNT*1/3)
#define SOF_ONLINE_THRESHOLD (SOF_MAX_COUNT*2/3)
/* SoF counter captured. */
CY_ISR(isr_capture_handler) {
    if (host_status == OnLine && SoFCounter_ReadCapture() < SOF_OFFLINE_THRESHOLD) {
        usb_offline_flag = 1u;
    }
    SoFCounter_ReadStatusRegister(); /* Clear interrupt. */
}
/* End of File */
