#pragma once

#include "common.h"

#define MAILBOX_CHANNEL_PROPERTY 8

int mailbox_call(uint8_t channel, volatile uint32_t *buffer);

