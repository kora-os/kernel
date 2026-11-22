// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libconsole/con_base.h"

// The line editor of a console (a CON: handler, project_doc/console-plan.md):
// what turns typed bytes (libconsole/keys.h) into what its programs read.
//
// Cooked mode, the default, as AmigaOS's CON: and a Unix tty's canonical
// mode: the console echoes and edits the line, and a program reads it only
// once Enter ends it, with its '\n'. Left and Right move in the line, Home
// (or Ctrl+A) and End (Ctrl+E) to its ends; Backspace and Delete remove a
// character, Ctrl+U the start of the line, Ctrl+K its end; Up and Down go
// through the history of lines entered. Ctrl+D on an empty line is the end of
// input: a read returns 0. Tab and other control keys are ignored.
//
// Raw mode, as RAW: : every byte goes to the programs as typed, without echo
// or editing.
//
// In both modes Ctrl+C is the console's: it sets a break (con_edit_break)
// and, in cooked mode, drops the line being typed.
//
// The editor is pure: it writes its echo through a callback, as escape
// sequences for an xterm-compatible terminal (libconsole/term.h), and knows
// the terminal only by its width and the column a line starts in.

#define CON_LINE_MAX 512      // the longest line, '\n' included
#define CON_HISTORY 32        // lines remembered
#define CON_READY_MAX 2048    // input entered and not read yet

#define CON_COOKED 0
#define CON_RAW 1

typedef struct {
    // Where the echo goes: bytes for the terminal.
    void (*echo)(void *ctx, const char *bytes, size_t length);
    void *ctx;
    int mode;  // CON_COOKED or CON_RAW

    // The line being typed: bytes, UTF-8, and the cursor's byte offset.
    char line[CON_LINE_MAX];
    size_t length, cursor;
    // The terminal's width, and the column the line started in.
    uint32_t cols, start_col;
    bool editing;  // a line has been started (its start column is set)

    // Lines entered (cooked) or bytes typed (raw), not read yet, in order.
    // An end of input (Ctrl+D) is the byte 0xFF, which UTF-8 never uses.
    char ready[CON_READY_MAX];
    size_t ready_length;
    bool brk;       // Ctrl+C since the last con_edit_break()

    // History: oldest first; `browse` is the entry shown, history_count when
    // the line is the one being typed (kept in `draft`).
    char history[CON_HISTORY][CON_LINE_MAX];
    size_t history_count, browse;
    char draft[CON_LINE_MAX];
    size_t draft_length;

    // An escape sequence being read (ESC [ ... final), and a UTF-8 character.
    char escape[16];
    size_t escape_length;
    char pending[4];
    size_t pending_length, pending_need;
} con_edit_t;

void con_edit_init(con_edit_t *e, void (*echo)(void *ctx, const char *bytes, size_t length),
                   void *ctx);

// The terminal is `cols` wide and its cursor in column `col` (from 0): where
// the next line starts. Call it before feeding input whenever output may have
// moved the cursor; it applies only while no line is being typed.
void con_edit_place(con_edit_t *e, uint32_t cols, uint32_t col);

// Feed typed bytes.
void con_edit_input(con_edit_t *e, const char *bytes, size_t length);

// Switch between CON_COOKED and CON_RAW. A line being typed stays, to be
// ended in cooked mode.
void con_edit_mode(con_edit_t *e, int mode);

// What a read gets: up to `length` bytes of the input entered, oldest first
// (a read never gets more than one line). Returns how many; 0 with *end set
// for an end of input; 0 with *end clear when there is nothing yet, and the
// read has to wait.
size_t con_edit_read(con_edit_t *e, char *buf, size_t length, bool *end);

// Whether Ctrl+C was pressed since the last call.
bool con_edit_break(con_edit_t *e);
