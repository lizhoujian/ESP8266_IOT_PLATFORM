#ifndef __USER_FX_H__
#define __USER_FX_H__

#define STX 0x2
#define ETX 0x3
#define ENQ 0x5
#define ACK 0x6
#define NACK 0x0f

#define ACTION_READ      0x0
#define ACTION_WRITE     0x1
#define ACTION_FORCE_ON  0x7
#define ACTION_FORCE_OFF 0x8

// register type
enum {
    REG_D = 0,
    REG_M,
    REG_T,
    REG_S,
    REG_C,
    REG_X,
    REG_Y
};

// register bytes address
#define REG_D_BASE_ADDRESS 0x1000
#define REG_M_BASE_ADDRESS 0x100
#define REG_T_BASE_ADDRESS 0x0c0
#define REG_S_BASE_ADDRESS 0x0
#define REG_C_BASE_ADDRESS 0x1c0
#define REG_X_BASE_ADDRESS 0x80
#define REG_Y_BASE_ADDRESS 0x0a0

// register bit address
#define REG_D_BIT_BASE_ADDRESS 0x0
#define REG_M_BIT_BASE_ADDRESS 0x800
#define REG_T_BIT_BASE_ADDRESS 0x0c0
#define REG_S_BIT_BASE_ADDRESS 0x0
#define REG_C_BIT_BASE_ADDRESS 0x1c0
#define REG_X_BIT_BASE_ADDRESS 0x80
#define REG_Y_BIT_BASE_ADDRESS 0x100

bool fx_enquiry(void);

#endif // __USER_FX_H__

