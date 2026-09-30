#pragma once

#if defined(KORAOS_VIRT)
	// BCM peripherals are unavailable on virt; board drivers must be excluded.
	#define PBASE 0
#elif RPI_VERSION == 3
	#define PBASE 0x3F000000
#elif RPI_VERSION == 4
	#ifdef QEMU_TESTING
		// QEMU's raspi4b machine uses RPi3 peripheral addresses
		#define PBASE 0x3F000000
	#else
		#define PBASE 0xFE000000
	#endif
#else
	#define PBASE 0
	#error RPI_VERSION not defined
#endif
