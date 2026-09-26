#pragma once

#include "common.h"

#define MAILBOX_CHANNEL_PROPERTY 8

// `buffer` must be 16-byte aligned and coherent with the VideoCore (e.g. from
// coherent_page()): the call does no cache maintenance.
int mailbox_call(uint8_t channel, volatile uint32_t *buffer);

