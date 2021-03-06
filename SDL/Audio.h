// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.h: SDL sound implementation
//
//  Copyright (c) 1999-2012 Simon Owen
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

#ifndef AUDIO_H
#define AUDIO_H

class Audio
{
    public:
        static bool Init (bool fFirstInit_=false);
        static void Exit (bool fReInit_=false);

        static bool IsAvailable () { return SDL_GetAudioStatus() == SDL_AUDIO_PLAYING; }
        static bool AddData (Uint8* pbData_, int nLength_);
        static void Silence ();
};

////////////////////////////////////////////////////////////////////////////////

class CSoundStream
{
    public:
        CSoundStream ();
        virtual ~CSoundStream ();

    public:
        void Silence ();
        void AddData (Uint8* pbSampleData_, int nLength_);

        Uint8 *m_pbStart, *m_pbEnd, *m_pbNow;
        int m_nSampleBufferSize;
};

#endif  // AUDIO_H
