// Part of SimCoupe - A SAM Coup� emulator
//
// Profile.h: Emulator profiling for on-screen stats
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

#ifndef PROFILE_H
#define PROFILE_H

// The member names in this structure must be dw<something>
typedef struct
{
    DWORD dwCPU;
    DWORD dwGfx;
    DWORD dwSnd;
    DWORD dwBlt;
    DWORD dwIdle;
    DWORD dwOther;
}
PROFILE;


// Macros to reference values in the structure above
#define ProfileStart(type)  Profile::ProfileStart_(&Profile::g_sProfile.dw##type)
#define ProfileEnd()        Profile::ProfileEnd_()

namespace Profile
{
    void Reset ();
    const char* GetStats ();
    void ProfileStart_ (DWORD* pdwNew_);
    void ProfileEnd_ ();

    extern PROFILE g_sProfile;
};

#endif  // PROFILE_H
