/* Copyright (C) 2011, 2012 Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "stdafx.h"

#ifndef VSTMIDIDRV_MIDISYNTH_H
#define VSTMIDIDRV_MIDISYNTH_H

class VSTDriver;

namespace VSTMIDIDRV {

    class MidiSynth {
    private:
        unsigned int chunkSize = 0;
        unsigned int bufferSize = 0;

        VSTDriver* vstDriver = NULL;

        MidiSynth() noexcept;

    public:
        void Close() noexcept;
        static MidiSynth& GetInstance();
        int Init(unsigned uDeviceID);
        DWORD PutMidiMessage(unsigned uDeviceID, DWORD dwParam1);
        DWORD PutSysEx(unsigned uDeviceID, unsigned char* bufpos, DWORD len);
        void Render(short* bufpos, DWORD totalFrames);
        void RenderFloat(float* bufpos, DWORD totalFrames);
        int Reset(unsigned uDeviceID) noexcept;
    };

}
#endif