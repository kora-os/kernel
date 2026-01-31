#pragma once

#include "common.h"
#include "peripherals/base.h"

struct MailboxRegs {
    reg32 read;
    reg32 reserved0[3];
    reg32 poll;
    reg32 sender;
    reg32 status;
    reg32 config;
    reg32 write;
};

#define REGS_MAILBOX ((struct MailboxRegs *)(PBASE + 0x0000B880))

#define MAILBOX_STATUS_FULL 0x80000000
#define MAILBOX_STATUS_EMPTY 0x40000000

