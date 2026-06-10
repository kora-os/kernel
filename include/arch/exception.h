#pragma once

// Install the EL1 exception vector table (VBAR_EL1). Call once during early
// kernel init, before enabling lower-EL (user) execution.
void exception_init(void);
