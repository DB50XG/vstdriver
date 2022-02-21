/* Copyright (C) 2011 Chris Moeller, Brad Miller
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

#ifndef __VSTDRIVER_H__
#define __VSTDRIVER_H__

#include <windows.h>
#include <malloc.h>
#include <stdio.h>
#include <tchar.h>
#include "../external_packages/aeffect.h"
#include "../external_packages/aeffectx.h"
#include <cstdint>
#include <vector>

class VSTDriver
{
private:
    TCHAR* szPluginPath = NULL;
    unsigned     uPluginPlatform;

    bool         isInitialized;
    bool         isTerminating;
    HANDLE       hProcess;
    HANDLE       hThread;
    HANDLE       hReadEvent;
    HANDLE       hChildStd_IN_Rd;
    HANDLE       hChildStd_IN_Wr;
    HANDLE       hChildStd_OUT_Rd;
    HANDLE       hChildStd_OUT_Wr;

    std::vector<std::uint8_t> blChunk;

    /// <summary>
    /// The number of VSTi audio outputs
    /// </summary>
    unsigned audioOutputs;

    /// <summary>
    /// The name of the VSTi
    /// </summary>
    char* effectName;
    char* vendor;
    char* product;
    /// <summary>
    /// The VSTi vendor-specific version
    /// </summary>
    uint32_t vendorVersion;
    /// <summary>
    /// The unique id of the VSTi
    /// </summary>
    uint32_t uniqueId;

    unsigned test_plugin_platform();
    bool connect_pipe(HANDLE hPipe);
    bool process_create(uint32_t** error = NULL);
    void process_terminate();
    bool process_running();
    uint32_t ReceiveData();
    void ReceiveData(void* buffer, uint32_t size);
    uint32_t ReceivePipeData(void* buffer, uint32_t size);
    void SendData(uint32_t code);
    void SendData(const void* buffer, uint32_t size);

    void LoadVstiSettings();
    void InitializeVstiPath(TCHAR* szPath);

public:
    VSTDriver();
    ~VSTDriver();
    void CloseVSTDriver();
    bool OpenVSTDriver(TCHAR* szPath = NULL, uint32_t** error = NULL);
    void SaveVstiSettings();
    void ResetDriver();
    void ProcessMIDIMessage(DWORD dwPort, DWORD dwParam1);
    void ProcessSysEx(DWORD dwPort, const unsigned char* sysexbuffer, int exlen);
    void Render(short* samples, int len, float volume = 1.0f);
    void RenderFloat(float* samples, int len, float volume = 1.0f);

    void GetEffectName(std::string& out);
    void GetVendorString(std::string& out);
    void GetProductString(std::string& out);
    long GetVendorVersion();
    long GetUniqueID();

    // configuration
    void GetChunk(std::vector<uint8_t>& out);
    void SetChunk(const void* in, unsigned size);

    // editor
    bool HasEditor();
    void DisplayEditorModal();
};

static LPTIMECALLBACK TimeProc(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    VSTDriver* effect = (VSTDriver*)dwUser;

    /// Play note C4 (middle C)
    /// 0x90
    ///			9 - Note On
    ///			0 - Midi Channel
    /// 0x3C
    ///			Note C4
    /// 0x64
    ///			Velocity (100)
    effect->ProcessMIDIMessage(0, 0x00403C90);
    Sleep(40);

    /// Play note D4
    effect->ProcessMIDIMessage(0, 0x00403E90);
    effect->ProcessMIDIMessage(0, 0x00403C80);
    Sleep(40);

    /// Play note E4
    effect->ProcessMIDIMessage(0, 0x00404090);
    effect->ProcessMIDIMessage(0, 0x00403E80);
    Sleep(40);

    /// Play note F4
    effect->ProcessMIDIMessage(0, 0x00404190);
    effect->ProcessMIDIMessage(0, 0x00404080);
    Sleep(40);

    /// Play note G4
    effect->ProcessMIDIMessage(0, 0x00404390);
    effect->ProcessMIDIMessage(0, 0x00404180);
    Sleep(40);

    /// Play note A4
    effect->ProcessMIDIMessage(0, 0x00404590);
    effect->ProcessMIDIMessage(0, 0x00404380);
    Sleep(40);

    /// Play note B4
    effect->ProcessMIDIMessage(0, 0x00404790);
    effect->ProcessMIDIMessage(0, 0x00404580);
    Sleep(40);

    /// Play note C5
    effect->ProcessMIDIMessage(0, 0x00404890);
    effect->ProcessMIDIMessage(0, 0x00404780);
    Sleep(40);
    effect->ProcessMIDIMessage(0, 0x00404880);

    return 0;
}

#endif