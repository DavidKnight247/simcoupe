// Part of SimCoupe - A SAM Coup� emulator
//
// Clock.h: SAMBUS and DALLAS clock emulation
//
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

#ifndef CLOCK_H
#define CLOCK_H

namespace Clock
{
    bool Init ();
    void Exit ();

    BYTE In (WORD wPort_);
    void Out (WORD wPort_, BYTE bVal_);

    void FrameUpdate ();
};

#endif
