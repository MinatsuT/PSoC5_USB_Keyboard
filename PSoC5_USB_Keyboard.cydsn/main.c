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

#define LSHIFT 0x02
#define ENTER 0x28

void In_EP(void);
void pushKey(uint16 key);
void flushPacket(void);

//Creats a Scan Code Look Up Table for the various ASCII values
//                                      0      1      2      3      4      5      6      7      8      9      a      b      c      d      e      f
const uint16 aASCII_ToScanCode[] = {0x02C, 0x11E, 0x11F, 0x120, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x134, 0x133, 0x036, 0x02d, 0x037, 0x038,
                                    0x027, 0x01E, 0x01F, 0x020, 0x021, 0x022, 0x023, 0x024, 0x025, 0x026, 0x034, 0x033, 0x136, 0x12d, 0x137, 0x138,
                                    0x02f, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D, 0x10E, 0x10F, 0x110, 0x111, 0x112,
                                    0x113, 0x114, 0x115, 0x116, 0x117, 0x118, 0x119, 0x11A, 0x11B, 0x11C, 0x11D, 0x030, 0x089, 0x032, 0x02e, 0x187,
                                    0x12f, 0x004, 0x005, 0x006, 0x007, 0x008, 0x009, 0x00A, 0x00B, 0x00C, 0x00D, 0x00E, 0x00F, 0x010, 0x011, 0x012,
                                    0x013, 0x014, 0x015, 0x016, 0x017, 0x018, 0x019, 0x01A, 0x01B, 0x01C, 0x01D, 0x130, 0x189, 0x132, 0x12e};

/* Array of Keycode information to send to PC */
static unsigned char Keyboard_Data[8] = {0, 0, 0, 0, 0, 0, 0, 0};

/* Max keys which are simultaneously sent. */
#define MAX_KEYS_IN_PACKET 6

/* Current packet position. */
static uint8 cur_pos = 0;

/* Serial overflow flag. */
static uint8 isOverflow = 0u;

/* For debug print. */
char dbuf[256];
#define DP(...)             {sprintf(dbuf, __VA_ARGS__); UART_PutString(dbuf);}

/* XOFF:Ctrl+S(0x13) XON:Ctrl+Q(0x11) */
#define XOFF                (0x13u)
#define XON                 (0x11u)

int main(void) {
    /* Enable global interrupts. */
    CyGlobalIntEnable;

    /* Start Debug Port */
    UART_Start();

    DP("\n\nPSoC5 LP UART Controlled USB Keyboard. Programmed By Minatsu, 2017.\n");
    DP("Waiting enumeration ...\n");

    /*Start USBFS Operation of Device 0 with 5V operation*/
    USBFS_Start(0, USBFS_DWR_VDDD_OPERATION);

    /*Wait for USB to be enumerated*/
    while (!USBFS_bGetConfiguration()) ;
    DP("Enumerated by host\n");

    /*Begins USB Traffic*/
    USBFS_LoadInEP(1, Keyboard_Data, 8);

    for (;;) {
        /*Checks for ACK from host*/
        if (USBFS_bGetEPAckState(1)) {
            /*Function to push and send Data to PC*/
            In_EP();
        }
    }
}

/* In EP handler. */
void In_EP(void) {
    uint8 c;

    if (UART_GetRxBufferSize()) {
        /* Receive 1 byte from UART, and push ScanCode into the packet. */
        c = UART_GetByte();
        if ((c >= 0x20) && (c <= 0x7E)) {
            pushKey(aASCII_ToScanCode[c-0x20]);
        } else if (c == 0x0d) {
            pushKey(ENTER);
        }
    } else {
        /* When there is no input, flush the packet. */
        if (cur_pos) {
            flushPacket();
        }
    }

    /* Software flow control. */
    if (UART_GetRxBufferSize() > UART_RX_BUFFER_SIZE/2) {
        if (!isOverflow) {
            UART_PutChar(XOFF);
            isOverflow = 1u;
        }
    } else {
        if (isOverflow) {
            UART_PutChar(XON);
            isOverflow = 0u;
        }
    }
}

/* Push key into the packet. */
void pushKey(uint16 key) {
    uint8 mod = (key & 0x100) ? LSHIFT : 0x00;
    uint8 code = key & 0xff;
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

    /*Loads EP1 for a IN transfer to PC*/
    USBFS_LoadInEP(1, Keyboard_Data, 8);
    /*Waits for ACK from PC*/
    while (!USBFS_bGetEPAckState(1)) ;

    /* Clear the packet. */
    for (i = 0; i < 8; i++) {
        Keyboard_Data[i] = 0x00;
    }

    /*Loads EP1 for a IN transfer to PC. This simulates the buttons being released.*/
    USBFS_LoadInEP(1, Keyboard_Data, 8);
    /*Waits for ACK from PC*/
    while (!USBFS_bGetEPAckState(1)) ;

    /* Reset current position in the packet. */
    cur_pos = 0;
}
/* End of File */
