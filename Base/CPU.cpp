// Part of SimCoupe - A SAM Coup� emulator
//
// CPU.cpp: Z80 processor emulation and main emulation loop
//
//  Copyright (c) 1996-2001  Allan Skillman
//  Copyright (c) 2000-2001  Dave Laundon
//  Copyright (c) 1999-2001  Simon Owen
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

// Changes 1999-2000 by Simon Owen:
//  - general revamp and reformat, with execution now polled for each frame
//  - very rough contended memory timings by doubling basic timings
//  - frame/line interrupt and flash frequency values corrected

// Changes 2000-2001 by Dave Laundon
//  - perfect contended memory timings on each memory/port access
//  - new cpu event model to reduce the per-instruction overhead
//  - MIDI OUT interrupt timings corrected

// ToDo:
//  - tidy things up a bit, particularly the register macros
//  - general state saving (CPU registers already in a structure for it)

#include "SimCoupe.h"

#include "CPU.h"
#include "Debug.h"
#include "Display.h"
#include "Frame.h"
#include "Input.h"
#include "IO.h"
#include "Memory.h"
#include "Options.h"
#include "Profile.h"
#include "UI.h"
#include "Util.h"


#undef USE_FLAG_TABLES      // Experimental - disabled for now

// Look up table for the parity of all byte values
BYTE g_abParity[256];
#define parity(a) (g_abParity[a])

#ifdef USE_FLAG_TABLES
BYTE g_abInc[256], g_abDec[256];
#endif


#define a       regs.AF.B.h_
#define f       regs.AF.B.l_
#define b       regs.BC.B.h_
#define c       regs.BC.B.l_
#define d       regs.DE.B.h_
#define e       regs.DE.B.l_
#define h       regs.HL.B.h_
#define l       regs.HL.B.l_

#define af      regs.AF.W
#define bc      regs.BC.W
#define de      regs.DE.W
#define hl      regs.HL.W

#define a1      regs.AF_.B.h_
#define f1      regs.AF_.B.l_
#define b1      regs.BC_.B.h_
#define c1      regs.BC_.B.l_
#define d1      regs.DE_.B.h_
#define e1      regs.DE_.B.l_
#define h1      regs.HL_.B.h_
#define l1      regs.HL_.B.l_

#define alt_af  regs.AF_.W
#define alt_bc  regs.BC_.W
#define alt_de  regs.DE_.W
#define alt_hl  regs.HL_.W

#define ix      regs.IX.W
#define iy      regs.IY.W
#define sp      regs.SP.W
#define pc      regs.PC.W

#define sp_h    regs.SP.B.h_
#define sp_l    regs.SP.B.l_

#define r       regs.R
#define i       regs.I          // This daft one means we can't use 'i' as a 'for' variable in this module!
#define iff1    regs.IFF1
#define iff2    regs.IFF2
#define im      regs.IM


// Since Java has no macros, changing helpers to be inlines like this should make things easier
inline void rflags (BYTE b_, BYTE c_) { f = c_ | (b_ & 0xa8) | ((!b_) << 6) | parity(b_); }


////////////////////////////////////////////////////////////////////////////////
//  H E L P E R   M A C R O S


// Update g_nLineCycle for one memory access
// This is the basic three T-State CPU memory access
// Longer memory M-Cycles should have the extra T-States added after MEM_ACCESS
// Logic -  if in RAM:
//              if we are in the main screen area, or one of the extra MODE 1 contended areas:
//                  CPU can only access memory 1 out of every 8 T-States
//              else
//                  CPU can only access memory 1 out of every 4 T-States
#define MEM_ACCESS(a)   ((g_nLineCycle += 3) |= (afContendedPages[VPAGE(a)]) ? pMemAccess[g_nLineCycle >> 6] : 0)

// Update g_nLineCycle for one port access
// This is the basic four T-State CPU I/O access
// Longer I/O M-Cycles should have the extra T-States added after PORT_ACCESS
// Logic -  if ASIC-controlled port:
//              CPU can only access I/O port 1 out of every 8 T-States
#define PORT_ACCESS(a)  ((g_nLineCycle += 4) |= ((a) >= BASE_ASIC_PORT) ? 7 : 0)


BYTE g_bOpcode;             // The currently executing or previously executed instruction
int g_nLine;                // Scan line being generated (0 is the top of the generated display, not the main screen)
int g_nLineCycle;           // Cycles so far in the current scanline
int g_nPrevLineCycle;       // Cycles before current instruction began

bool g_fDebugging;

int nInterruptType;         // Type of Z80 interrupt to deal with

DWORD g_dwCycleCounter;     // Global cycle counter used for various timings

bool fDelayedEI;            // Flag and counter to carry out a delayed EI
bool g_fFlashPhase;         // Mode 1 and 2 flash phase, toggled every 16 frames

// Memory access contention table
const int MEM_ACCESS_LINE = TSTATES_PER_LINE >> 6;
int aMemAccesses[10 * MEM_ACCESS_LINE], *pMemAccessBase, *pMemAccess, nMemAccessIndex;
bool fMemContention;

Z80Regs regs;


WORD* pHlIxIy, *pNewHlIxIy;
unsigned int radjust;
bool        g_fExecuteFrame;
CPU_EVENT   asCpuEvents[MAX_EVENTS], *psNextEvent, *psFreeEvent;
DWORD dwLastTime, dwFPSTime;

namespace CPU
{

bool Init (bool fPowerOnInit_/*=true*/)
{
    int n;

    // Sanity check the endian of the registers structure
    hl = 1;
    if (h)
        Message(msgFatal, "EEK!  The Z80Regs structure is the wrong endian for this platform!\n");

    // Initialise the Z80 registers (everything zero except ix and iy, which are 0xffff and Zero flag is set - strange!)
    memset(&regs, 0, sizeof regs);
    ix = iy = 0xffff;
    f = F_ZERO;
    radjust = 0;

    // No index prefix seen yet
    pHlIxIy = pNewHlIxIy = &hl;

    // Set CPU operating mode
    nInterruptType = fPowerOnInit_ ? Z80_none : Z80_pause;
    g_bOpcode = OP_NOP;

    // Counter used to determine when each line should be drawn
    g_fDebugging = false;
    g_nLineCycle = g_nPrevLineCycle = 0;

    // Initialise the Cpu Events Queue and create the first line-end event
    CPU_EVENT* psEvent_;
    for (psEvent_ = asCpuEvents; psEvent_ < (asCpuEvents + MAX_EVENTS - 1); psEvent_++)
        psEvent_->psNext = (psEvent_ + 1);
    psFreeEvent = psEvent_->psNext = asCpuEvents;
    psNextEvent = NULL;
    AddCpuEvent(evtEndOfLine, g_dwCycleCounter + TSTATES_PER_LINE);

    // Build the parity lookup table
    for (n = 0x00 ; n <= 0xff ; n++)
    {
        BYTE b2 = n ^ (n >> 4);
        b2 ^= (b2 << 2);
        g_abParity[n] = ~(b2 ^ (b2 >> 1)) & F_PARITY;

#ifdef USE_FLAG_TABLES
        g_abInc[n] = (n & 0xa8) | ((!n) << 6) | ((!( n & 0xf)) << 4) | ((n == 0x80) << 2);
        g_abDec[n] = (n & 0xa8) | ((!n) << 6) | ((!(~n & 0xf)) << 4) | ((n == 0x7f) << 2) | F_NADD;
#endif
    }

    // Build the memory access contention table
    // Note - instructions often overlap to the next line (hence the duplicates).
    //  0, 1, 4 - border lines
    //  2, 3    - screen lines
    //  5 to 9  - mode 1 versions of the same
    // Lines 0 and 2 (5 and 7) are used for normal border or screen lines, with 1 and 3 (6 and 8) being duplicates
    // so if we overlap to the next line we will still get the correct contention.
    // Lines 1 and 3 (6 and 8) are used for the last line of the border or screen so that if we overlap to the next
    // line we will get the new correct contention.
    // Line 0 is used continuously if we are in mode 3 or 4 and the screen is off.
    pMemAccess = pMemAccessBase = aMemAccesses;
    fMemContention = true;
    nMemAccessIndex = 0;
    for (n = 0; n < MEM_ACCESS_LINE; ++n) {
        int     m = n * TSTATES_PER_LINE / MEM_ACCESS_LINE;
        aMemAccesses[0 * MEM_ACCESS_LINE + n] =
        aMemAccesses[1 * MEM_ACCESS_LINE + n] =
        aMemAccesses[4 * MEM_ACCESS_LINE + n] = 3;
        aMemAccesses[2 * MEM_ACCESS_LINE + n] =
        aMemAccesses[3 * MEM_ACCESS_LINE + n] =
            (m >= BORDER_PIXELS && m < BORDER_PIXELS + SCREEN_PIXELS) ? 7 : 3;
        aMemAccesses[5 * MEM_ACCESS_LINE + n] =
        aMemAccesses[6 * MEM_ACCESS_LINE + n] =
        aMemAccesses[9 * MEM_ACCESS_LINE + n] = (m & 0x40) ? 7 : 3;
        aMemAccesses[7 * MEM_ACCESS_LINE + n] =
        aMemAccesses[8 * MEM_ACCESS_LINE + n] = ((m & 0x40) ||
            (m >= BORDER_PIXELS && m < BORDER_PIXELS + SCREEN_PIXELS)) ? 7 : 3;
    }

    return (!fPowerOnInit_ || Memory::Init()) && IO::Init(fPowerOnInit_);
}

void Exit (bool fReInit_/*=false*/)
{
    IO::Exit(fReInit_);
    Memory::Exit(fReInit_);
}


// Work out if we're in a vertical part of the screen that may be affected by contention
inline void SetContention ()
{
    pMemAccess = fMemContention ? (pMemAccessBase + nMemAccessIndex) : aMemAccesses;
}

// Update contention flags based on mode/screen-off changes
void UpdateContention ()
{
    fMemContention = !(BORD_SOFF && VMPR_MODE_3_OR_4);
    pMemAccessBase = aMemAccesses + ((vmpr_mode ^ MODE_1) ? 0 : (5 * MEM_ACCESS_LINE));
    SetContention();
}


// Read a byte and update timing
inline BYTE timed_read_byte (WORD addr)
{
    MEM_ACCESS(addr);
    return (read_byte(addr));
}

// Read a word and update timing
inline WORD timed_read_word (WORD addr)
{
    MEM_ACCESS(addr);
    MEM_ACCESS(addr + 1);
    return (read_word(addr));
}

// Write a byte and update timing
inline void timed_write_byte (WORD addr, BYTE contents)
{
    MEM_ACCESS(addr);
    write_byte(addr, contents);
}

// Write a word and update timing
inline void timed_write_word (WORD addr, WORD contents)
{
    MEM_ACCESS(addr);
    MEM_ACCESS(addr + 1);
    write_word(addr, contents);
}

// Write a word and update timing (high-byte first - used by stack functions)
inline void timed_write_word_reversed (WORD addr, WORD contents)
{
    MEM_ACCESS(addr + 1);
    MEM_ACCESS(addr);
    write_word(addr, contents);
}

// 16-bit push and pop
#define push(val)   do { sp -= 2; timed_write_word_reversed(sp,val); } while(0)
#define pop(var)    do { var = timed_read_word(sp); sp += 2; } while(0)


// Execute the CPU event specified
void ExecuteEvent (CPU_EVENT sThisEvent)
{
    switch (sThisEvent.nEvent)
    {
        case evtStdIntStart :
            // Check for a LINE interrupt on the following line
            if ((line_int < SCREEN_LINES) && (g_nLine == (line_int + TOP_BORDER_LINES - 1)))
            {
                // Signal the LINE interrupt, and start the interrupt counter
                status_reg &= ~STATUS_INT_LINE;
                AddCpuEvent(evtStdIntEnd, sThisEvent.dwTime + INT_ACTIVE_TIME);
            }
            // Check for a FRAME interrupt on the last line
            else if (g_nLine == (HEIGHT_LINES - 1))
            {
                // Signal a FRAME interrupt, and start the interrupt counter
                status_reg &= ~STATUS_INT_FRAME;
                AddCpuEvent(evtStdIntEnd, sThisEvent.dwTime + INT_ACTIVE_TIME);
            }
            break;

        case evtStdIntEnd :
            // Reset the interrupt as we're done
            status_reg |= (STATUS_INT_FRAME | STATUS_INT_LINE);
            break;

        case evtMidiOutIntStart :
            // Begin the MIDI_OUT interrupt and add an event to end it
            status_reg &= ~STATUS_INT_MIDIOUT;
            AddCpuEvent(evtMidiOutIntEnd, sThisEvent.dwTime + MIDI_INT_ACTIVE_TIME);
            break;

        case evtMidiOutIntEnd :
            // Reset the interrupt and clear the 'transmitting' bit in LPEN as we're done
            status_reg |= STATUS_INT_MIDIOUT;
            lpen &= ~LPEN_TXFMST;
            break;

        case evtEndOfLine :
            // Subtract a line's worth of cycles and move to the next line down
            g_nPrevLineCycle -= TSTATES_PER_LINE;
            g_nLineCycle -= TSTATES_PER_LINE;
            g_nLine++;
            // Add an event for the next line
            AddCpuEvent(evtEndOfLine, sThisEvent.dwTime + TSTATES_PER_LINE);

            // Are we still inside the frame?
            if (g_nLine < HEIGHT_LINES)
            {
                // Work out if we're in a vertical part of the screen that may be affected by contention
                nMemAccessIndex = MEM_ACCESS_LINE * (
                    (((g_nLine >= TOP_BORDER_LINES) && (g_nLine < TOP_BORDER_LINES + SCREEN_LINES)) ? 2 : 0) +
                    ((g_nLine == TOP_BORDER_LINES - 1) || (g_nLine == TOP_BORDER_LINES + SCREEN_LINES - 1))
                );
                SetContention();

                // Are we on a line that may potentially require an interrupt at the start of the right border?
                if (((g_nLine >= (TOP_BORDER_LINES - 1)) && (g_nLine < (TOP_BORDER_LINES - 1 + SCREEN_LINES))) ||
                    (g_nLine == (HEIGHT_LINES - 1)))
                {
                    // Add an event to check for LINE/FRAME interrupts
                    AddCpuEvent(evtStdIntStart, sThisEvent.dwTime + INT_START_TIME);

                    // Update the input in the centre of the screen (well away from the frame boundary) to avoid the BASIC
                    // keyboard scanner discarding key presses when it thinks keys have bounced.  This was the cause of the
                    // first key press on the boot screen only clearing it (took AGES to track that down!)
                    if (g_nLine == (HEIGHT_LINES / 2))
                        Input::Update();
                }
            }
            else
                // Signal to stop executing instructions for this frame
                g_fExecuteFrame = false;
            break;

    }
}


// The main z80 emulation loop
void ExecuteFrame ()
{
    g_fExecuteFrame = true;

    while (g_fExecuteFrame)
    {
#ifdef _DEBUG
        // Primitive debug tracing, until real debugger is available
        if (g_fDebugging)
            TRACE("A=%02x B=%02x C=%02x D=%02x E=%02x H=%02x L=%02x\n", regs.AF.B.h_, regs.BC.B.h_, regs.BC.B.l_, regs.DE.B.h_, regs.DE.B.l_, regs.HL.B.h_, regs.HL.B.l_);
#endif

        // Update the line/global counters and check for pending events

        CheckCpuEvents();

        // Are there any active interrupts?
        if (status_reg != STATUS_INT_NONE)
        {
            // Only process the interrupt if interrupts are enabled (and not delayed after a DI)
            // ... and not in the middle of an indexed instruction
            if (iff1 && (g_bOpcode != OP_EI) && (g_bOpcode != OP_DI) && (pNewHlIxIy == &hl))
            {
                // Advance PC if we're stopped on a HALT, as we've got an interrupt it was halted for
                if (g_bOpcode == OP_HALT)
                    pc++;

                // Disable interrupts
                iff1 = iff2 = 0;

                // What we do next depends on the interrupt mode set
                switch (im)
                {
                    case 0:
                        // Push PC onto the stack, and execute the interrupt handler
                        g_nLineCycle += 6;
                        push(pc);
                        pc = IM1_INTERRUPT_HANDLER;
                        break;

                    case 1:
                        // Push PC onto the stack, and execute the interrupt handler
                        g_nLineCycle += 7;
                        push(pc);
                        pc = IM1_INTERRUPT_HANDLER;
                        break;

                    case 2:
                        // Push PC onto the stack, and execute the IM 2 handler
                        g_nLineCycle += 7;
                        push(pc);
                        // Fetch the IM 2 handler address from an address formed from I and 0xff (from the bus)
                        pc = timed_read_word((i << 8) | 0xff);
                        break;
                }
            }
        }

        // Initialise, fetch instruction and advance program counter
        pHlIxIy = pNewHlIxIy;
        pNewHlIxIy = &hl;

        MEM_ACCESS(pc);
        g_bOpcode = (nInterruptType == Z80_pause) ? OP_NOP : read_byte(pc++);

        radjust++;

        // Execute the instruction
        switch (g_bOpcode)
        {
#include "Z80ops.h"
        }
    }
}


// The main Z80 emulation loop
void Run ()
{
    // Loop until told to quit
    while (UI::CheckEvents())
    {
        if (GetOption(paused))
            continue;

        // Execute a single frame's worth, generating the display as it goes
        Frame::Start();
        ProfileStart(CPU);
        ExecuteFrame();
        ProfileEnd();
        Frame::End();

        IO::FrameUpdate();

        // Start back at top of top border
        g_nLine = 0;

        // Toggle paper/ink colours every 16 frames for for mode 1 and 2 flashing
        static int nFrame = 0;
        if (!(++nFrame % 16))
            g_fFlashPhase = !g_fFlashPhase;

        // Deal with non maskable interrupt types
        switch (nInterruptType)
        {
            case Z80_none:      // Normal operation
            case Z80_pause:     // CPU paused (for when reset is held down)
                break;

            // Non-maskable interrupt
            case Z80_nmi:
                // Advance PC if we're stopped on a HALT
                if (timed_read_byte(pc) == OP_HALT)
                    pc++;

                // Save the interrupt status in iff2 and disable interrupts
                iff2 = iff1;
                iff1 = 0;
                g_nLineCycle += 2;

                // Call NMI handler at address 0x0066
                push(pc);
                pc = NMI_INTERRUPT_HANDLER;

                nInterruptType = Z80_none;
                break;

            // Z80 reset
            case Z80_reset:
                Init(true);
                nInterruptType = Z80_none;
                break;
        }
    }

    TRACE("Quitting main emulation loop...\n");
}

void NMI()
{
    nInterruptType = Z80_nmi;
}

};  // namespace CPU
