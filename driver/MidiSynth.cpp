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
#include "winreg.h"
#include <map>

#undef GetMessage

// Define BASSASIO functions as pointers
#define BASSASIODEF(f) (WINAPI *f)
#define LOADBASSASIOFUNCTION(f) *((void**)&f)=GetProcAddress(bassAsio,#f)

// Define BASS functions as pointers
#define BASSDEF(f) (WINAPI *f)
#define LOADBASSFUNCTION(f) *((void**)&f)=GetProcAddress(bass,#f)

// Define BASSWASAPI functions as pointers
#define BASSWASAPIDEF(f) (WINAPI *f)
#define LOADBASSWASAPIFUNCTION(f) *((void**)&f)=GetProcAddress(bassWasapi,#f)

#include <bass.h>
#include <bassasio.h>
#include <basswasapi.h>

#include "VSTDriver.h"
#include <string>
#include <codecvt>

using std::string;
using std::wstring;
using std::map;
using std::wstring_convert;
using std::codecvt_utf8;


extern "C" { extern HINSTANCE hinst_vst_driver; }

namespace VSTMIDIDRV
{
    static MidiSynth& midiSynth = MidiSynth::GetInstance();

    /// <summary>
    /// Collects midi messages from the midi source
    /// </summary>
    static class MidiStream
    {
    private:
        static const unsigned int maxPos = 1024;
        unsigned int startpos;
        unsigned int endpos;

        struct message
        {
            void* sysex;
            DWORD msg;
            DWORD port_type;
        };

        message stream[maxPos];

    public:
        MidiStream() noexcept
        {
            Reset();
        }

        void Reset() noexcept
        {
            startpos = 0;
            endpos = 0;
        }

        /// <summary>
        /// Put MIDI message to the midi stream.
        /// </summary>
        /// <param name="uDeviceID">The port type.</param>
        /// <param name="dwParam1">The MIDI message to put.</param>
        /// <returns>MMSYSERR_NOERROR on sucess, MIDIERR_NOTREADY otherwise</returns>
        DWORD PutMessage(DWORD uDeviceID, DWORD dwParam1) noexcept
        {
            unsigned int newEndpos = endpos;

            ++newEndpos;

            // Check for buffer rolloff
            if (newEndpos == maxPos)
            {
                newEndpos = 0;
            }

            // Check for buffer full
            if (startpos == newEndpos)
            {
                return MIDIERR_NOTREADY;
            }

            // Put data and update endpos
            stream[endpos].sysex = 0;
            stream[endpos].msg = dwParam1;
            stream[endpos].port_type = uDeviceID;
            endpos = newEndpos;

            return MMSYSERR_NOERROR;
        }

        /// <summary>
        /// Put MIDI System Exclusive message to the midi stream.
        /// </summary>
        /// <param name="port">The port type.</param>
        /// <param name="sysEx">The MIDI System Exclusive message to put.</param>
        /// <param name="sysExLength">The length of the MIDI System Exclusive message.</param>
        /// <returns></returns>
        DWORD PutSysEx(DWORD port, const unsigned char* sysEx, DWORD sysExLength) noexcept
        {
            unsigned int newEndpos = endpos;

            ++newEndpos;

            // Check for buffer rolloff
            if (newEndpos == maxPos)
            {
                newEndpos = 0;
            }

            // Check for buffer full
            if (startpos == newEndpos)
            {
                return MIDIERR_NOTREADY;
            }

            void* sysExCopy = malloc(sysExLength);
            if (!sysExCopy)
            {
                return MIDIERR_NOTREADY;
            }

            memcpy(sysExCopy, sysEx, sysExLength);

            stream[endpos].sysex = sysExCopy;
            stream[endpos].msg = sysExLength;
            stream[endpos].port_type = port | 0x80000000;
            endpos = newEndpos;

            return MMSYSERR_NOERROR;
        }

        /// <summary>
        /// Get MIDI message or MIDI System Exclusive message from the midi stream and advance the .
        /// </summary>
        /// <param name="port">The port type.</param>
        /// <param name="message">The next MIDI message from the midi stream.</param>
        /// <param name="sysEx">The MIDI System Exclusive message.</param>
        /// <param name="sysExLength">The length of the MIDI System Exclusive message.</param>
        /// <returns></returns>
        void GetMessage(DWORD& port, DWORD& message, unsigned char*& sysEx, DWORD& sysExLength) noexcept
        {
            port = 0;
            message = 0;
            sysEx = 0;
            sysExLength = 0;

            // Check for buffer empty
            if (startpos == endpos)
            {
                return;
            }

            port = stream[startpos].port_type & 0x7fffffff;

            if (stream[startpos].port_type & 0x80000000)
            {
                sysEx = (unsigned char*)stream[startpos].sysex;
                sysExLength = stream[startpos].msg;
            }
            else
            {
                message = stream[startpos].msg;
            }

            ++startpos;

            // Check for buffer rolloff
            if (startpos == maxPos)
            {
                startpos = 0;
            }
        }

        DWORD PeekMessageCount() noexcept
        {
            if (endpos < startpos)
            {
                return endpos + maxPos - startpos;
            }
            else
            {
                return endpos - startpos;
            }
        }
    } midiStream;

    static class SynthMutexWin32
    {
    private:
        CRITICAL_SECTION cCritSec;

    public:
        int Init() noexcept
        {
            InitializeCriticalSection(&cCritSec);
            return 0;
        }

        void Close() noexcept
        {
            DeleteCriticalSection(&cCritSec);
        }

        void Enter() noexcept
        {
            EnterCriticalSection(&cCritSec);
        }

        void Leave() noexcept
        {
            LeaveCriticalSection(&cCritSec);
        }
    } synthMutex;

    static class WaveOutWin32
    {
    private:
        HINSTANCE bass = NULL;          // bass handle
        HINSTANCE bassAsio = NULL;      // bassasio handle
        HINSTANCE bassWasapi = NULL;    // basswasapi handle

        HSTREAM hStOutput = NULL;

        TCHAR* modeValueName = L"Mode";
        TCHAR* asioValueName = L"ASIO";
        TCHAR* wasapiValueName = L"WASAPI";

        bool soundOutFloat = false;
        DWORD wasapiBits = 16;
        DWORD buflen = 0;

        TCHAR installPath[MAX_PATH] = { 0 };
        TCHAR bassPath[MAX_PATH] = { 0 };
        TCHAR bassAsioPath[MAX_PATH] = { 0 };
        TCHAR bassWasapiPath[MAX_PATH] = { 0 };

        map<wstring, wstring> outputDriver;

        void InitializePaths() noexcept
        {
            GetModuleFileName(hinst_vst_driver, installPath, MAX_PATH);
            PathRemoveFileSpec(installPath);
            lstrcat(installPath, _T("\\vstmididrv\\"));

            lstrcat(bassPath, installPath);
            lstrcat(bassPath, _T("bass.dll"));

            lstrcat(bassAsioPath, installPath);
            lstrcat(bassAsioPath, _T("bassasio.dll"));

            lstrcat(bassWasapiPath, installPath);
            lstrcat(bassWasapiPath, _T("basswasapi.dll"));
        }

        bool LoadBass(wstring mode) noexcept
        {
            if (mode.empty() || mode.compare(asioValueName) == 0)
            {
                // Load Bass Asio
                bassAsio = LoadLibrary(bassAsioPath);

                LOADBASSASIOFUNCTION(BASS_ASIO_Init);

                /// Check if there is at least one ASIO capable device
                if (BASS_ASIO_Init(-1, BASS_ASIO_THREAD))
                {
                    LOADBASSASIOFUNCTION(BASS_ASIO_Free);
                    LOADBASSASIOFUNCTION(BASS_ASIO_Stop);
                    LOADBASSASIOFUNCTION(BASS_ASIO_Start);
                    LOADBASSASIOFUNCTION(BASS_ASIO_GetInfo);
                    LOADBASSASIOFUNCTION(BASS_ASIO_GetRate);
                    LOADBASSASIOFUNCTION(BASS_ASIO_SetRate);
                    LOADBASSASIOFUNCTION(BASS_ASIO_SetNotify);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ErrorGetCode);
                    LOADBASSASIOFUNCTION(BASS_ASIO_GetDeviceInfo);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ChannelGetInfo);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ChannelJoin);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ChannelReset);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ChannelEnable);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ChannelIsActive);
                    LOADBASSASIOFUNCTION(BASS_ASIO_ChannelSetFormat);

                    BASS_ASIO_Free();
                }
                else
                {
                    FreeLibrary(bassAsio);
                    bassAsio = NULL;
                }
            }

            if (bassAsio)
            {
                return true;
            }

            // Load Bass
            bass = LoadLibrary(bassPath);

            if (!bass)
            {
                return false;
            }

            // Load Bass Wasapi
            bassWasapi = LoadLibrary(bassWasapiPath);
            
            if (bass)
            {
                LOADBASSFUNCTION(BASS_Init);
                LOADBASSFUNCTION(BASS_Free);
                LOADBASSFUNCTION(BASS_SetConfig);
                LOADBASSFUNCTION(BASS_ErrorGetCode);
                LOADBASSFUNCTION(BASS_GetDeviceInfo);
                LOADBASSFUNCTION(BASS_StreamCreate);
                LOADBASSFUNCTION(BASS_StreamFree);
                LOADBASSFUNCTION(BASS_ChannelPlay);
                LOADBASSFUNCTION(BASS_ChannelStop);
                LOADBASSFUNCTION(BASS_ChannelPause);
                LOADBASSFUNCTION(BASS_ChannelPause);
                LOADBASSFUNCTION(BASS_ChannelGetData);
            }

            if (bassWasapi)
            {
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_Init);
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_Free);
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_Start);
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_Stop);
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_GetInfo);
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_GetDeviceInfo);
            }

            return true;
        }

        void LoadOutputDriverSettings()
        {
            HKEY hKey;

            LSTATUS result = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\VSTi Driver\\Output Driver", 0, KEY_READ | KEY_WOW64_32KEY, &hKey);

            if (result != NO_ERROR)
            {
                return;
            }

            DWORD values;
            DWORD valueNameMaxSize;
            DWORD valueMaxSize;

            result = RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &values, &valueNameMaxSize, &valueMaxSize, NULL, NULL);

            if (result != NO_ERROR || values == 0)
            {
                return;
            }

            TCHAR* valueName = NULL;
            DWORD valueNameSize = 0;
            TCHAR* value = NULL;
            DWORD valueSize = 0;

            DWORD valueType = REG_NONE;

            valueName = (TCHAR*)calloc(valueNameMaxSize + 1, sizeof(TCHAR));
            value = (TCHAR*)calloc(valueMaxSize + 1, sizeof(TCHAR));

            for (int i = 0; i < values; ++i)
            {
                valueNameSize = valueNameMaxSize + 1;
                valueSize = valueMaxSize + 1;

                result = RegEnumValue(hKey, i, valueName, &valueNameSize, NULL, &valueType, (LPBYTE)value, &valueSize);

                if (result == NO_ERROR && valueType == REG_SZ)
                {
                    wstring tempName = valueName;
                    wstring tempValue = value;

                    outputDriver[tempName] = tempValue;
                }
            }

            free(valueName);
            free(value);
        }

        /// <summary>
        /// Get the selected ASIO driver
        /// </summary>
        /// <param name="selectedDeviceId">The device id of the selected ASIO driver</param>
        /// <param name="selectedChannelId">The channeld id of the selected ASIO driver</param>
        void GetSelectedAsioDriver(int& selectedDeviceId, int& selectedChannelId)
        {
            selectedDeviceId = -1;
            selectedChannelId = 0;

            wstring selectedOutputDriver = outputDriver[asioValueName];

            BASS_ASIO_DEVICEINFO asioDeviceInfo{};

            for (size_t deviceId = 0; BASS_ASIO_Init(deviceId, BASS_ASIO_THREAD); ++deviceId)
            {
                BASS_ASIO_GetDeviceInfo(deviceId, &asioDeviceInfo);

                wstring deviceName = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(asioDeviceInfo.name);

                if (selectedOutputDriver.find(deviceName) == string::npos)
                {
                    continue;
                }

                selectedDeviceId = deviceId;

                BASS_ASIO_CHANNELINFO channelInfo{};

                size_t pos = strlen(asioDeviceInfo.name);

                for (size_t channel = 0; BASS_ASIO_ChannelGetInfo(FALSE, channel, &channelInfo); ++channel)
                {
                    wstring channelName = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(channelInfo.name);
                    if (selectedOutputDriver.find(channelName, pos) == string::npos)
                    {
                        continue;
                    }

                    selectedChannelId = channel;
                    break;
                }

                BASS_ASIO_Free();
            }
        }

        /// <summary>
        /// Get the selected WASAPI driver
        /// </summary>
        void GetSelectedWasapiDriver(int& selectedDeviceId)
        {
            // -1 = default output device
            selectedDeviceId = -1;

            wstring selectedOutputDriver = outputDriver[wasapiValueName];

            BASS_WASAPI_DEVICEINFO  deviceInfo{};

            for (size_t deviceId = 0; BASS_WASAPI_GetDeviceInfo(deviceId, &deviceInfo); ++deviceId)
            {
                wstring deviceName = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(deviceInfo.name);
                if (!(deviceInfo.flags & BASS_DEVICE_INPUT) && (deviceInfo.flags & BASS_DEVICE_ENABLED) && selectedOutputDriver.compare(deviceName) == 0)
                {
                    selectedDeviceId = deviceId;
                    break;
                }
            }
        }

    public:
        WaveOutWin32() noexcept
        {
        }

        int Init(unsigned int bufferSize, unsigned int chunkSize, unsigned int sampleRate)
        {
            if (bassPath[0] == NULL)
            {
                InitializePaths();
            }

            LoadOutputDriverSettings();

            wstring selectedMode = outputDriver[modeValueName];

            if (!LoadBass(selectedMode))
            {
                return -1;
            }

            int deviceId;

            if (bassAsio)
            {
                int channelId;

                GetSelectedAsioDriver(deviceId, channelId);

                if (!BASS_ASIO_Init(deviceId, BASS_ASIO_THREAD))
                {
                    return -2;
                }

                BASS_ASIO_SetRate(sampleRate);

                sampleRate = BASS_ASIO_GetRate();

                // Enable 1st output channel
                BASS_ASIO_ChannelEnable(FALSE, channelId, AsioProc, this);

                soundOutFloat = true;

                // Join the next channel to it (stereo)
                BASS_ASIO_ChannelJoin(FALSE, channelId + 1, channelId);

                // Set the source format to FLOAT
                // TODO: autodetect format or add format option in config
                BASS_ASIO_ChannelSetFormat(FALSE, channelId, BASS_ASIO_FORMAT_FLOAT);

                //BASS_ASIO_SetNotify((ASIONOTIFYPROC*)AsioNotifyProc, this);
            }
            else if (bassWasapi)
            {
                GetSelectedWasapiDriver(deviceId);

                DWORD wasapiFlags = BASS_WASAPI_AUTOFORMAT;
                wasapiFlags &= selectedMode.compare(L"WASAPI Shared") == 0 ? BASS_WASAPI_EVENT : BASS_WASAPI_EXCLUSIVE;
                
                if (!BASS_WASAPI_Init(deviceId, sampleRate, 2, wasapiFlags, 0, 0, WasapiProc, this))
                {
                    int error = BASS_ErrorGetCode();
                    return -3;
                }

                BASS_WASAPI_INFO winfo{};

                if (!BASS_WASAPI_GetInfo(&winfo))
                {
                    int error = BASS_ErrorGetCode();
                    BASS_WASAPI_Free();
                    return -4;
                }

                sampleRate = winfo.freq;

                switch (winfo.format)
                {
                    case BASS_WASAPI_FORMAT_8BIT:
                        wasapiBits = 8;
                        break;

                    default:
                    case BASS_WASAPI_FORMAT_16BIT:
                        wasapiBits = 16;
                        break;

                    case BASS_WASAPI_FORMAT_24BIT:
                        wasapiBits = 24;
                        break;

                    case BASS_WASAPI_FORMAT_32BIT:
                        wasapiBits = 32;
                        break;

                    case BASS_WASAPI_FORMAT_FLOAT:
                        soundOutFloat = TRUE;
                        break;
                }
            }
            else if (bass)
            {
                if (!BASS_Init(deviceId, sampleRate, 0, NULL, NULL))
                {
                    int error = BASS_ErrorGetCode();
                    return -5;
                }

                BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
                BASS_SetConfig(BASS_CONFIG_VISTA_TRUEPOS, 0);
                BASS_SetConfig(BASS_CONFIG_BUFFER, 0);

                hStOutput = BASS_StreamCreate(sampleRate, 2, (soundOutFloat ? BASS_SAMPLE_FLOAT : 0), StreamProc, this);
                if (!hStOutput)
                {
                    int error = BASS_ErrorGetCode();
                    BASS_Free();
                    return -6;
                }
            }

            return sampleRate;
        }

        int Close() noexcept
        {
            if (bassAsio)
            {
                // Stop ASIO device in case it was playing
                if (BASS_ASIO_IsStarted)
                {
                    BASS_ASIO_Stop();
                }
                BASS_ASIO_Free();

                FreeLibrary(bassAsio);
                bassAsio = NULL;
            }

            if (bassWasapi)
            {
                BASS_WASAPI_Stop(TRUE);
                BASS_WASAPI_Free();
                FreeLibrary(bassWasapi);
                bassWasapi = NULL;
            }
            else if (hStOutput)
            {
                BASS_ChannelStop(hStOutput);
                BASS_StreamFree(hStOutput);
                hStOutput = NULL;
            }

            if (bass)
            {
                BASS_Free();
                FreeLibrary(bass);
                bass = NULL;
            }

            return 0;
        }

        int Start() noexcept
        {
            if (bassAsio)
            {
                BASS_ASIO_Start(buflen, 0);
            }
            else if (bassWasapi)
            {
                BASS_WASAPI_Start();
            }
            else if (hStOutput)
            {
                BASS_ChannelPlay(hStOutput, FALSE);
            }

            return 0;
        }

        int Pause() noexcept
        {
            if (bassAsio)
            {
                //BASS_ASIO_ChannelPause(FALSE, -1);
            }
            else if (bassWasapi)
            {
                BASS_WASAPI_Stop(FALSE);
            }
            else if (hStOutput)
            {
                BASS_ChannelPause(hStOutput);
            }

            return 0;
        }

        int Resume() noexcept
        {
            if (bassAsio)
            {
                //BASS_ASIO_ChannelReset(FALSE, -1, BASS_ASIO_RESET_PAUSE);
            }
            else if (bassWasapi)
            {
                BASS_WASAPI_Start();
            }
            else if (hStOutput)
            {
                BASS_ChannelPlay(hStOutput, FALSE);
            }

            return 0;
        }

        static DWORD CALLBACK AsioProc(BOOL input, DWORD channel, void* buffer, DWORD length, void* user) noexcept
        {
            WaveOutWin32* _this = (WaveOutWin32*)user;
            if (_this->soundOutFloat)
            {
                midiSynth.RenderFloat((float*)buffer, length / 8);
            }
            else
            {
                midiSynth.Render((short*)buffer, length / 4);
            }
            return length;
        }

        static DWORD CALLBACK WasapiProc(void* buffer, DWORD length, void* user)
        {
            WaveOutWin32* _this = (WaveOutWin32*)user;
            if (_this->soundOutFloat || _this->wasapiBits == 16)
            {
                return StreamProc(NULL, buffer, length, user);
            }
            else
            {
                int bytes_per_sample = _this->wasapiBits / 8;
                int bytes_done = 0;
                while (length)
                {
                    unsigned short sample_buffer[1024];
                    int length_todo = (length / bytes_per_sample);
                    if (length_todo > 512) length_todo = 512;
                    int bytes_done_this = StreamProc(NULL, sample_buffer, length_todo * 4, 0);
                    if (bytes_done_this <= 0) return bytes_done;
                    if (bytes_per_sample == 4)
                    {
                        unsigned int* out = (unsigned int*)buffer;
                        for (int i = 0; i < bytes_done_this; i += 2)
                        {
                            *out++ = sample_buffer[i / 2] << 16;
                        }
                        buffer = out;
                    }
                    else if (bytes_per_sample == 3)
                    {
                        unsigned char* out = (unsigned char*)buffer;
                        for (int i = 0; i < bytes_done_this; i += 2)
                        {
                            int sample = sample_buffer[i / 2];
                            *out++ = 0;
                            *out++ = sample & 0xFF;
                            *out++ = (sample >> 8) & 0xFF;
                        }
                        buffer = out;
                    }
                    else if (bytes_per_sample == 1)
                    {
                        unsigned char* out = (unsigned char*)buffer;
                        for (int i = 0; i < bytes_done_this; i += 2)
                        {
                            *out++ = (sample_buffer[i / 2] >> 8) & 0xFF;
                        }
                        buffer = out;
                    }
                    bytes_done += (bytes_done_this / 2) * bytes_per_sample;
                    length -= (bytes_done_this / 2) * bytes_per_sample;
                }
                return bytes_done;
            }
        }

        static DWORD CALLBACK StreamProc(HSTREAM handle, void* buffer, DWORD length, void* user)
        {
            WaveOutWin32* _this = (WaveOutWin32*)user;
            if (_this->soundOutFloat)
            {
                midiSynth.RenderFloat((float*)buffer, length / 8);
            }
            else
            {
                midiSynth.Render((short*)buffer, length / 4);
            }
            return length;
        }

        static void CALLBACK AsioNotifyProc(DWORD notify, void* user)
        {
            switch (notify)
            {
                case BASS_ASIO_NOTIFY_RATE:
                    // The device's sample rate has changed. The new rate is available from BASS_ASIO_GetRate.
                    break;

                case BASS_ASIO_NOTIFY_RESET:
                    // The driver has requested a reset/reinitialization; for example, following a change of the default buffer size.
                    // This request can be ignored, but if a reinitialization is performed, it should not be done within the callback.
                    break;
            }
        }
    } waveOut;

    MidiSynth::MidiSynth() noexcept {}

    MidiSynth& MidiSynth::GetInstance()
    {
        static MidiSynth instance;
        return instance;
    }

    // Renders totalFrames frames starting from bufpos
    // The number of frames rendered is added to the global counter framesRendered
    void MidiSynth::Render(short* bufpos, DWORD totalFrames)
    {
        DWORD count;
        // Incoming MIDI messages timestamped with the current audio playback position + midiLatency
        while ((count = midiStream.PeekMessageCount()))
        {
            DWORD msg;
            DWORD sysex_len;
            DWORD port;
            unsigned char* sysex;
            synthMutex.Enter();
            midiStream.GetMessage(port, msg, sysex, sysex_len);
            if (msg && !sysex)
            {
                vstDriver->ProcessMIDIMessage(port, msg);
            }
            else if (!msg && sysex && sysex_len)
            {
                vstDriver->ProcessSysEx(port, sysex, sysex_len);
                free(sysex);
            }
            synthMutex.Leave();
        }

        synthMutex.Enter();
        vstDriver->Render(bufpos, totalFrames);
        synthMutex.Leave();
    }

    void MidiSynth::RenderFloat(float* bufpos, DWORD totalFrames)
    {
        DWORD count;
        // Incoming MIDI messages timestamped with the current audio playback position + midiLatency
        while ((count = midiStream.PeekMessageCount()))
        {
            DWORD msg;
            DWORD sysex_len;
            DWORD port;
            unsigned char* sysex;
            synthMutex.Enter();
            midiStream.GetMessage(port, msg, sysex, sysex_len);
            if (msg && !sysex)
            {
                vstDriver->ProcessMIDIMessage(port, msg);
            }
            else if (!msg && sysex && sysex_len)
            {
                vstDriver->ProcessSysEx(port, sysex, sysex_len);
                free(sysex);
            }
            synthMutex.Leave();
        }

        synthMutex.Enter();
        vstDriver->RenderFloat(bufpos, totalFrames);
        synthMutex.Leave();
    }

    BOOL IsVistaOrNewer() noexcept
    {
        OSVERSIONINFOEX osvi;
        BOOL bOsVersionInfoEx;
        ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
        bOsVersionInfoEx = GetVersionEx((OSVERSIONINFO*)&osvi);

        return bOsVersionInfoEx && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT && osvi.dwMajorVersion > 5;
    }

    int MidiSynth::Init(unsigned uDeviceID)
    {
        // Init synth
        if (synthMutex.Init())
        {
            return 1;
        }

        unsigned int sampleRate = 44100;
        int wResult = waveOut.Init(bufferSize, chunkSize, sampleRate);
        if (wResult < 0)
        {
            return -wResult;
        }

        sampleRate = wResult;

        vstDriver = new VSTDriver;
        if (!vstDriver->OpenVSTDriver(NULL, NULL, sampleRate))
        {
            delete vstDriver;
            vstDriver = NULL;
            return 1;
        }

        return waveOut.Start();
    }

    int MidiSynth::Reset(unsigned uDeviceID) noexcept
    {
        UINT wResult = waveOut.Pause();
        if (wResult)
        {
            return wResult;
        }

        synthMutex.Enter();
        vstDriver->ResetDriver();
        midiStream.Reset();
        synthMutex.Leave();

        return waveOut.Resume();
    }

    /// <summary>
    /// Put MIDI message to the midi stream.
    /// </summary>
    /// <param name="uDeviceID">The port type.</param>
    /// <param name="dwParam1">The MIDI message to put.</param>
    /// <returns></returns>
    DWORD MidiSynth::PutMidiMessage(unsigned uDeviceID, DWORD dwParam1)
    {
        return midiStream.PutMessage(uDeviceID, dwParam1);
    }

    /// <summary>
    /// Put MIDI SysEx message to the midi stream.
    /// </summary>
    /// <param name="uDeviceID">The port type.</param>
    /// <param name="bufpos">The MIDI SysEx message to put.</param>
    /// <param name="len">The length of the MIDI SysEx message.</param>
    /// <returns></returns>
    DWORD MidiSynth::PutSysEx(unsigned uDeviceID, unsigned char* bufpos, DWORD len)
    {
        return midiStream.PutSysEx(uDeviceID, bufpos, len);
    }

    void MidiSynth::Close() noexcept
    {
        waveOut.Close();

        synthMutex.Enter();
        vstDriver->CloseVSTDriver();
        delete vstDriver;
        vstDriver = NULL;
        synthMutex.Leave();
        synthMutex.Close();
    }
}