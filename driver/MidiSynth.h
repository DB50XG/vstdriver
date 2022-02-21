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
        unsigned int sampleRate;
        unsigned int midiLatency, midiLatencyMS;
        unsigned int bufferSize, bufferSizeMS;
        unsigned int chunkSize, chunkSizeMS;
        bool resetEnabled;
        float outputGain;

        short* buffer;
        float* bufferf;
        DWORD framesRendered;

        VSTDriver* vstDriver;

        MidiSynth();

    public:
        static MidiSynth& getInstance();
        int Init(unsigned uDeviceID) noexcept;
        void Close() noexcept;
        int Reset(unsigned uDeviceID) noexcept;
        void Render(short* bufpos, DWORD totalFrames);
        void RenderFloat(float* bufpos, DWORD totalFrames);
        DWORD PutMidiMessage(unsigned uDeviceID, DWORD dwParam1);
        DWORD PutSysEx(unsigned uDeviceID, unsigned char* bufpos, DWORD len);
    };

}
#endif