// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for keylog (src/input/keylog.c), the debug console's trace of
// the input event queue: the xev-style text of an event, the notes taken
// under locks and printed later, and what it says about lost and dropped
// events. The UART, the queue and the task table are stubs.

#include "test.h"
#include "arch/spinlock.h"
#include "input/hid_keys.h"
#include "input/input.h"
#include "input/keylog.h"
#include "proc/task.h"

char *strstr(const char *haystack, const char *needle);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
size_t strlen(const char *s);

static int held;
uint64_t spin_lock_irqsave(struct spinlock *lock) {
    (void)lock;
    CHECK(held == 0, "the keylog lock is not taken twice");
    held++;
    return 0;
}
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    (void)lock;
    (void)flags;
    held--;
}

static unsigned dropped;
unsigned input_dropped(void) { return dropped; }
const char *input_source_name(unsigned source) {
    return source == 1 ? "usb-keyboard" : source == 2 ? "virtio-keyboard" : "unknown";
}

// Threads 2 (screens, process 2), 5 (shell) and 9 (a thread of process 7).
bool task_describe(int tid, char name[TASK_NAME_MAX], int *pid) {
    CHECK(held == 0, "names are looked up without the keylog lock");
    switch (tid) {
    case 2: strcpy(name, "screens"); *pid = 2; return true;
    case 5: strcpy(name, "shell"); *pid = 5; return true;
    case 9: strcpy(name, "worker"); *pid = 7; return true;
    default: return false;
    }
}
bool proc_describe(int pid, char name[TASK_NAME_MAX]) {
    if (pid == 7) {
        strcpy(name, "condemo");
        return true;
    }
    return false;
}

static char uart[KEYLOG_NOTES * KEYLOG_TEXT_MAX];
void uart_puts(const char *text) {
    CHECK(held == 0, "keylog prints without its lock");
    bool fits = strlen(uart) + strlen(text) < sizeof(uart);
    CHECK(fits, "the capture fits");
    if (fits) {
        strcat(uart, text);
    }
}

static char text[KEYLOG_TEXT_MAX];

static struct keylog_note note(uint8_t type, uint16_t keycode, uint16_t modifiers,
                               uint32_t codepoint) {
    return (struct keylog_note){
        .event = {.type = type, .modifiers = modifiers, .time_ms = 123456,
                  .keycode = keycode, .source = 1, .codepoint = codepoint},
        .to = KEYLOG_TO_CLAIMER, .tid = 2, .focus_pid = 7};
}

#define TEXT_IS(expected)                                                   \
    CHECK(strcmp(text, expected) == 0, "got:\n%s\nwant:\n%s", text, expected)

static void test_format(void) {
    struct keylog_note n = note(INPUT_EVENT_KEY_DOWN, HID_KEY_A, INPUT_MOD_LEFT_GUI, 'a');
    keylog_format(text, &n, "usb-keyboard", "screens (pid 2)", "condemo (pid 7)");
    TEXT_IS("KeyDown from usb-keyboard to screens (pid 2) -> front: condemo (pid 7)\n"
            "    time 123456 ms, keycode 0x04 (A), modifiers 0x0008 (LGUI)\n"
            "    char U+0061 \"a\", repeat no\n");

    n = note(INPUT_EVENT_KEY_UP, HID_KEY_A, 0, 0);
    keylog_format(text, &n, "virtio-keyboard", "kernel console (read by shell (pid 5))", NULL);
    TEXT_IS("KeyUp from virtio-keyboard to kernel console (read by shell (pid 5))\n"
            "    time 123456 ms, keycode 0x04 (A), modifiers 0x0000 (none)\n");

    n = note(INPUT_EVENT_KEY_DOWN, HID_KEY_ENTER, INPUT_MOD_LEFT_SHIFT | INPUT_MOD_CAPS_LOCK, 0);
    n.event.flags = INPUT_FLAG_REPEAT;
    keylog_format(text, &n, "s", "r", NULL);
    TEXT_IS("KeyDown from s to r\n"
            "    time 123456 ms, keycode 0x28 (Enter), modifiers 0x0102 (LShift+Caps)\n"
            "    char none, repeat yes\n");

    n = note(INPUT_EVENT_KEY_DOWN, 0x34, INPUT_MOD_RIGHT_ALT, 0xE9);  // AltGr: é
    keylog_format(text, &n, "s", "r", NULL);
    CHECK(strstr(text, "keycode 0x34 (Apostrophe), modifiers 0x0040 (RAlt)\n") != NULL &&
          strstr(text, "char U+00E9 \"\xC3\xA9\", repeat no\n") != NULL, "UTF-8: %s", text);
    n = note(INPUT_EVENT_KEY_DOWN, 0x20, 0, 0x20AC);  // the euro sign
    keylog_format(text, &n, "s", "r", NULL);
    CHECK(strstr(text, "char U+20AC \"\xE2\x82\xAC\"") != NULL, "three bytes: %s", text);
    n = note(INPUT_EVENT_KEY_DOWN, HID_KEY_ESCAPE, INPUT_MOD_LEFT_CTRL, 0x1B);
    keylog_format(text, &n, "s", "r", NULL);
    CHECK(strstr(text, "char U+001B, repeat no") != NULL, "a control character is not quoted");

    n = note(INPUT_EVENT_KEY_DOWN, 0xFF, 0x03FF, 0x10FFFF);
    n.event.type = 9;
    keylog_format(text, &n, "unknown", "a receiver with a long name (pid 1234567)",
                  "another long process name (pid 7654321)");
    CHECK(strstr(text, "Event 9 from unknown") == text, "an unknown type: %s", text);
    CHECK(strstr(text, "keycode 0xff (?), modifiers 0x03ff "
                       "(LCtrl+LShift+LAlt+LGUI+RCtrl+RShift+RAlt+RGUI+Caps+Num)\n") != NULL,
          "every modifier, an unnamed key: %s", text);
    CHECK(strlen(text) < KEYLOG_TEXT_MAX, "the longest text fits (%u)", (unsigned)strlen(text));
}

static void test_key_names(void) {
    CHECK(strcmp(keylog_key_name(HID_KEY_Z), "Z") == 0, "Z");
    CHECK(strcmp(keylog_key_name(0x1E), "1") == 0 && strcmp(keylog_key_name(0x27), "0") == 0,
          "the digit row");
    CHECK(strcmp(keylog_key_name(HID_KEY_F12), "F12") == 0, "F12");
    CHECK(strcmp(keylog_key_name(HID_KEY_UP), "Up") == 0, "Up");
    CHECK(strcmp(keylog_key_name(HID_KEY_KP_ENTER), "KPEnter") == 0, "KPEnter");
    CHECK(strcmp(keylog_key_name(0x67), "KP=") == 0, "KP=");
    CHECK(strcmp(keylog_key_name(0x73), "F24") == 0, "F24");
    CHECK(strcmp(keylog_key_name(HID_KEY_LEFT_CTRL), "LCtrl") == 0 &&
          strcmp(keylog_key_name(HID_KEY_RIGHT_GUI), "RGUI") == 0, "the modifiers");
    CHECK(keylog_key_name(0) == NULL && keylog_key_name(0x74) == NULL &&
          keylog_key_name(0xE8) == NULL && keylog_key_name(0xFFFF) == NULL,
          "unnamed usages have no name");
}

static void test_notes(void) {
    struct input_event down = {.type = INPUT_EVENT_KEY_DOWN, .keycode = HID_KEY_A,
                               .source = 2, .codepoint = 'a'};
    uart[0] = '\0';
    keylog_note(&down, KEYLOG_TO_CONSOLE, 5, 0);
    keylog_flush();
    CHECK(!keylog_enabled() && uart[0] == '\0', "nothing is noted while keylog is off");

    keylog_enable(true);
    CHECK(keylog_enabled(), "keylog is on");
    keylog_note(&down, KEYLOG_TO_CONSOLE, 5, 0);
    keylog_note(&down, KEYLOG_TO_CLAIMER, 2, 7);
    keylog_note(&down, KEYLOG_TO_CLAIMER, 2, 0);
    keylog_note(&down, KEYLOG_TO_CLAIMER, 9, 8);
    keylog_note(&down, KEYLOG_TO_CLAIMER, 4, 0);
    CHECK(uart[0] == '\0', "noting prints nothing");
    keylog_flush();
    const char *first = strstr(uart, "KeyDown from virtio-keyboard to kernel console "
                                     "(read by shell (pid 5))\n");
    const char *second = strstr(uart, "KeyDown from virtio-keyboard to screens (pid 2) -> "
                                      "front: condemo (pid 7)\n");
    const char *third = strstr(uart, "KeyDown from virtio-keyboard to screens (pid 2)\n");
    const char *fourth = strstr(uart, "to worker (pid 7) -> front: pid 8\n");
    const char *fifth = strstr(uart, "to pid 4\n");
    CHECK(first != NULL && second > first && third > second && fourth > third && fifth > fourth,
          "the notes are printed in order, receivers named:\n%s", uart);

    uart[0] = '\0';
    keylog_flush();
    CHECK(uart[0] == '\0', "a flush prints each note once");

    for (int i = 0; i < KEYLOG_NOTES + 5; i++) {
        keylog_note(&down, KEYLOG_TO_CONSOLE, 5, 0);
    }
    dropped += 3;
    keylog_flush();
    CHECK(strstr(uart, "keylog: the input queue dropped 3 events\n") == uart,
          "the queue's drops come first:\n%.200s", uart);
    CHECK(strstr(uart, "keylog: 5 events not shown\n") != NULL, "lost notes are counted");
    unsigned count = 0;
    for (const char *p = uart; (p = strstr(p, "KeyDown")) != NULL; p++) {
        count++;
    }
    CHECK(count == KEYLOG_NOTES, "the notes kept are printed (%u)", count);

    uart[0] = '\0';
    keylog_enable(false);
    keylog_note(&down, KEYLOG_TO_CONSOLE, 5, 0);
    dropped += 2;
    keylog_flush();
    CHECK(uart[0] == '\0', "off: no notes and no drops");
    keylog_enable(true);
    keylog_flush();
    CHECK(uart[0] == '\0', "drops while it was off are not reported");
    keylog_enable(false);
    CHECK(held == 0, "every critical section is balanced");
}

TEST_MAIN(test_format, test_key_names, test_notes)
