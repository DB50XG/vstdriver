/* Copyright (C) 2003, 2004, 2005 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011, 2012 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

extern "C" { HINSTANCE hinst_vst_driver = 0; }

constexpr auto MAX_DRIVERS = 2;
constexpr auto MAX_CLIENTS = 8; // Per driver

static VSTMIDIDRV::MidiSynth& midiSynth = VSTMIDIDRV::MidiSynth::getInstance();

static bool isSynthOpened = false;
//static HWND hwnd = NULL;
FLOAT SynthVolume = 1.0;

/// <summary>
/// Entry point into a dynamic-link library (DLL).
/// When the system starts or terminates a process or thread, it calls the entry-point function for each loaded DLL using the first thread of the process.
/// The system also calls the entry-point function for a DLL when it is loaded or unloaded using the LoadLibrary and FreeLibrary functions.
/// </summary>
/// <param name="hinstDLL">
/// A handle to the DLL module. The value is the base address of the DLL.
/// The HINSTANCE of a DLL is the same as the HMODULE of the DLL, so hinstDLL can be used in calls to functions that require a module handle.
/// </param>
/// <param name="fdwReason">The reason code that indicates why the DLL entry-point function is being called.</param>
/// <param name="lpvReserved">
/// If fdwReason is DLL_PROCESS_ATTACH, lpvReserved is NULL for dynamic loads and non-NULL for static loads.
/// If fdwReason is DLL_PROCESS_DETACH, lpvReserved is NULL if FreeLibrary has been called or the DLL load failed and non-NULL if the process is terminating.
/// </param>
/// <returns>
/// When the system calls the DllMain function with the DLL_PROCESS_ATTACH value, the function returns TRUE if it succeeds or FALSE if initialization fails.
/// If the return value is FALSE when DllMain is called because the process uses the LoadLibrary function, LoadLibrary returns NULL. (The system immediately calls your entry-point function with DLL_PROCESS_DETACH and unloads the DLL.)
/// If the return value is FALSE when DllMain is called during process initialization, the process terminates with an error.
/// To get extended error information, call GetLastError.
/// When the system calls the DllMain function with any value other than DLL_PROCESS_ATTACH, the return value is ignored.
/// </returns>
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            /// The DLL is being loaded into the virtual address space of the current process as a result of the process starting up or as a result of a call to LoadLibrary.
            /// DLLs can use this opportunity to initialize any instance data or to use the TlsAlloc function to allocate a thread local storage (TLS) index.
            /// The lpReserved parameter indicates whether the DLL is being loaded statically or dynamically.
            hinst_vst_driver = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            break;

        case DLL_PROCESS_DETACH:
            /// The DLL is being unloaded from the virtual address space of the calling process because it was loaded unsuccessfully or the reference count has reached zero (the processes has either terminated or called FreeLibrary one time for each time it called LoadLibrary).
            /// The lpReserved parameter indicates whether the DLL is being unloaded as a result of a FreeLibrary call, a failure to load, or process termination.
            /// The DLL can use this opportunity to call the TlsFree function to free any TLS indices allocated by using TlsAlloc and to free any thread local data.
            /// Note that the thread that receives the DLL_PROCESS_DETACH notification is not necessarily the same thread that received the DLL_PROCESS_ATTACH notification.
            hinst_vst_driver = NULL;
            break;
    }

    return TRUE;
}

struct Driver
{
    bool open;
    int clientCount;
    HDRVR hdrvr;
    struct Client
    {
        bool allocated;
        DWORD_PTR instance;
        DWORD flags;
        DWORD_PTR callback;
        DWORD synth_instance;
    } clients[MAX_CLIENTS];
} drivers[MAX_DRIVERS];

/// <summary>
/// Entry-point function to enable or disable the MIDI input and output driver.
/// Processes driver messages for the installable driver. DriverProc is a driver-supplied function.
/// https://docs.microsoft.com/en-us/windows/win32/api/mmiscapi/nc-mmiscapi-driverproc
/// </summary>
/// <param name="dwDriverID">Identifier of the installable driver</param>
/// <param name="hdrvr">Handle of the installable driver instance. Each instance of the installable driver has a unique handle.</param>
/// <param name="wMessage">Driver message value. It can be a custom value or one of the standard values.</param>
/// <param name="dwParam1">32-bit message-specific value</param>
/// <param name="dwParam2">32-bit message-specific value</param>
/// Installable Driver Reference: http://msdn.microsoft.com/en-us/library/ms709328%28v=vs.85%29.aspx
/// The original header is:
/// 
/// LONG DriverProc(DWORD dwDriverId, HDRVR hdrvr, UINT msg, LONG lParam1, LONG lParam2);
///
/// but that does not support 64bit.
/// STDAPI_(LRESULT) DriverProc(DWORD_PTR dwDriverId, HDRVR hdrvr, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
/// See declaration of DefDriverProc to see where the values come from.
/// <returns>Returns nonzero if successful or zero otherwise.</returns>
STDAPI_(LRESULT) DriverProc(DWORD_PTR dwDriverId, HDRVR hdrvr, UINT uMsg, LPARAM dwParam1, LPARAM dwParam2)
{
    switch (uMsg)
    {
        case DRV_LOAD:
            /// Notifies the driver that it has been successfully loaded.
            /// The driver should make sure that any hardware and supporting drivers it needs to function properly are present.
            /// The hdrvr parameter is always zero. The dwDriverId, lParam1, and lParam2 parameters are not used.
            /// The DRV_LOAD message is always the first message that a device driver receives.
            /// Returns nonzero if successful or zero otherwise.
            memset(drivers, 0, sizeof(drivers));
            return DRV_OK;

        case DRV_ENABLE:
            /// Notifies the driver that it has been loaded or reloaded or that Windows has been enabled.
            /// Enables the driver. The driver should initialize any variables and locate devices with the input and output (I/O) interface.
            /// hdrvr - handle of the installable driver instance. The dwDriverId, lParam1, and lParam2 parameters are not used.
            /// Drivers are considered enabled from the time they receive this message until they are disabled by using the DRV_DISABLE message.
            /// No return value.
            return DRV_OK;

        case DRV_OPEN:
            /// Notifies the driver that it is about to be opened. Directs the driver to open an new instance.
            /// dwDriverId - identifier of the installable driver.
            /// hdrvr - Handle of the installable driver instance.
            /// lParam1 - Address of a null-terminated, wide-character string that specifies configuration information used to open the instance. If no configuration information is available, either this string is empty or the parameter is NULL.
            /// When msg is DRV_OPEN, lParam1 is the string following the driver filename from the SYSTEM.INI file and lParam2 is the value given as the lParam parameter in a call to the OpenDriver function.
            /// lParam2 - 32-bit driver-specific data.
            /// If the driver returns a nonzero value, the system uses that value as the driver identifier (the dwDriverId parameter) in messages it subsequently sends to the driver instance.
            /// The driver can return any type of value as the identifier.
            /// For example, some drivers return memory addresses that point to instance-specific information.
            /// Using this method of specifying identifiers for a driver instance gives the drivers ready access to the information while they are processing messages.
            /// Returns a nonzero value if successful or zero otherwise.
            int i;
            for (i = 0; i < MAX_DRIVERS; ++i)
            {
                if (!drivers[i].open)
                {
                    break;
                }
            }

            if (i == MAX_DRIVERS)
            {
                return 0;
            }

            drivers[i].open = true;
            drivers[i].clientCount = 0;
            drivers[i].hdrvr = hdrvr;
            return DRV_OK;

        case DRV_INSTALL:
        case DRV_PNPINSTALL:
            /// Notifies the driver that it has been successfully installed.
            /// The driver should create and initialize any needed registry keys and values and verify that the supporting drivers and hardware are installed and properly configured.
            /// dwDriverId - Identifier of the installable driver. This is the same value previously returned by the driver from the DRV_OPEN message.
            /// hdrvr - Handle of the installable driver instance.
            /// The lParam1 parameter is not used.
            /// lParam2 - Address of a DRVCONFIGINFO structure or NULL. If a structure is given, it contains the names of the registry key and value associated with the driver.
            /// Some installable drivers append configuration information to the value assigned to the registry value associated with the driver.
            /// Returns one of these values:
            /// DRVCNF_OK - The installation is successful; no further action is required. 
            /// DRVCNF_CANCEL - The installation failed.
            /// DRVCNF_RESTART - The installation is successful, but it does not take effect until the system is restarted.
            return DRVCNF_OK;

        case DRV_QUERYCONFIGURE:
            /// Directs the driver to specify whether it supports the DRV_CONFIGURE message. Directs the driver to specify whether it supports custom configuration.
            /// dwDriverId - Identifier of the installable driver. This is the same value previously returned by the driver from the DRV_OPEN message.
            /// hdrvr - Handle of the installable driver instance.
            /// The lParam1and lParam2 parameters are not used.
            /// Returns a nonzero value if the driver can display a configuration dialog box or zero otherwise.
            return 0;

        case DRV_CONFIGURE:
            /// Notifies the driver that it should display a configuration dialog box. This message is sent only if the driver returns a nonzero value when processing the DRV_QUERYCONFIGURE message. 
            /// Directs the installable driver to display its configuration dialog box and let the user specify new settings for the given installable driver instance.
            /// dwDriverId - Identifier of the installable driver. This is the same value previously returned by the driver from the DRV_OPEN message.
            /// hdrvr - Handle of the installable driver instance.
            /// lParam1 - Handle of the parent window. This window is used as the parent window for the configuration dialog box.
            /// lParam2 - Address of a DRVCONFIGINFO structure or NULL. If the structure is given, it contains the names of the registry key and value associated with the driver.
            /// Some installable drivers append configuration information to the value assigned to the registry value associated with the driver.
            /// The DRV_CANCEL, DRV_OK, and DRV_RESTART return values are obsolete; they have been replaced by DRVCNF_CANCEL, DRVCNF_OK, and DRVCNF_RESTART, respectively.
            /// Returns one of these values:
            /// DRVCNF_OK - The configuration is successful; no further action is required.
            /// DRVCNF_CANCEL - The user canceled the dialog box; no further action is required.
            /// DRVCNF_RESTART - The configuration is successful, but the changes do not take effect until the system is restarted.
            return DRVCNF_OK;

        case DRV_CLOSE:
            /// Notifies the driver that it should decrement its usage count and unload the driver if the count is zero.
            /// Directs the driver to close the given instance. If no other instances are open, the driver should prepare for subsequent release from memory.
            /// dwDriverId - Identifier of the installable driver. This is the same value previously returned by the driver from the DRV_OPEN message.
            /// hdrvr - Handle of the installable driver instance.
            /// lParam1 - 32-bit value specified as the lParam1 parameter in a call to the DriverClose function.
            /// lParam2 - 32-bit value specified as the lParam2 parameter in a call to the DriverClose function.
            /// When msg is DRV_CLOSE, lParam1 and lParam2 are the same values as the lParam1 and lParam2 parameters in a call to the CloseDriver function.
            /// Returns nonzero if successful or zero otherwise.
            for (int i = 0; i < MAX_DRIVERS; ++i)
            {
                if (drivers[i].open && drivers[i].hdrvr == hdrvr)
                {
                    drivers[i].open = false;
                    return DRV_OK;
                }
            }
            return DRV_CANCEL;

        case DRV_DISABLE:
            /// Notifies the driver that its allocated memory is about to be freed.
            /// Disables the driver. The driver should place the corresponding device, if any, in an inactive state and terminate any callback functions or threads.
            /// hdrvr - Handle of the installable driver instance. The dwDriverId, lParam1, and lParam2 parameters are not used.
            /// After disabling the driver, the system typically sends the driver a DRV_FREE message before removing the driver from memory.
            /// No return value.
            return DRV_OK;

        case DRV_FREE:
            /// Notifies the driver that it will be discarded.
            /// Notifies the driver that it is being removed from memory.
            /// The driver should free any memory and other system resources that it has allocated.
            /// hdrvr - Handle of the installable driver instance. The dwDriverId, lParam1, and lParam2 parameters are not used.
            /// The DRV_FREE message is always the last message that a device driver receives.
            /// No return value.
            return DRV_OK;

        case DRV_REMOVE:
            /// Notifies the driver that it is about to be removed from the system.
            /// When a driver receives this message, it should remove any sections it created in the registry.
            /// dwDriverId - Identifier of the installable driver. This is the same value previously returned by the driver from the DRV_OPEN message.
            /// hdrvr - Handle of the installable driver instance.
            /// The lParam1 and lParam2 parameters are not used.
            /// No return value.
            return DRV_OK;

        default:
            return DefDriverProc(dwDriverId, hdrvr, uMsg, dwParam1, dwParam2);
    }
}

HRESULT GetMidiDeviceCapabilities(UINT uDeviceID, PVOID capsPtr, DWORD capsSize)
{
    MIDIOUTCAPSA* myCapsA;
    MIDIOUTCAPSW* myCapsW;
    MIDIOUTCAPS2A* myCaps2A;
    MIDIOUTCAPS2W* myCaps2W;

    CHAR synthName[] = "VST MIDI Synth\0";
    WCHAR synthNameW[] = L"VST MIDI Synth\0";

    CHAR synthPortA[] = " (port A)\0";
    WCHAR synthPortAW[] = L" (port A)\0";

    CHAR synthPortB[] = " (port B)\0";
    WCHAR synthPortBW[] = L" (port B)\0";

    switch (capsSize)
    {
        case (sizeof(MIDIOUTCAPSA)):
            myCapsA = (MIDIOUTCAPSA*)capsPtr;
            /// Manufacturer identifier of the device driver for the MIDI output device.
            /// Manufacturer identifiers are defined in Manufacturer and Product Identifiers.
            myCapsA->wMid = MM_UNMAPPED;
            /// Product identifier of the MIDI output device.
            /// Product identifiers are defined in Manufacturer and Product Identifiers.
            myCapsA->wPid = MM_MPU401_MIDIOUT;
            /// Version number of the device driver for the MIDI output device.
            /// The high-order byte is the major version number, and the low-order byte is the minor version number.
            myCapsA->vDriverVersion = 0x0090;
            /// Product name in a null-terminated string.
            memcpy(myCapsA->szPname, synthName, sizeof(synthName));
            memcpy(myCapsA->szPname + strlen(synthName), uDeviceID ? synthPortB : synthPortA, sizeof(synthPortA));
            /// Type of the MIDI output device. This value can be one of the following:
            ///     MOD_MIDIPORT
            ///         MIDI hardware port.
            ///     MOD_SYNTH
            ///         Synthesizer.
            ///     MOD_SQSYNTH
            ///         Square wave synthesizer.
            ///     MOD_FMSYNTH
            ///         FM synthesizer.
            ///     MOD_MAPPER
            ///         Microsoft MIDI mapper.
            ///     MOD_WAVETABLE
            ///         Hardware wavetable synthesizer.
            ///     MOD_SWSYNTH
            ///         Software synthesizer.
            myCapsA->wTechnology = MOD_MIDIPORT;
            /// Number of voices supported by an internal synthesizer device.
            /// If the device is a port, this member is not meaningful and is set to 0.
            myCapsA->wVoices = 0;
            /// Maximum number of simultaneous notes that can be played by an internal synthesizer device.
            /// If the device is a port, this member is not meaningful and is set to 0.
            myCapsA->wNotes = 0;
            /// Channels that an internal synthesizer device responds to, where the least significant bit refers to channel 0 and the most significant bit to channel 15.
            /// Port devices that transmit on all channels set this member to 0xFFFF.
            myCapsA->wChannelMask = 0xffff;
            /// Optional functionality supported by the device. It can be one or more of the following:
            ///     MIDICAPS_CACHE
            ///         Supports patch caching.
            ///     MIDICAPS_LRVOLUME
            ///         Supports separate left and right volume control.
            ///     MIDICAPS_STREAM
            ///         Provides direct support for the midiStreamOut function.
            ///     MIDICAPS_VOLUME
            ///         Supports volume control.
            /// If a device supports volume changes, the MIDICAPS_VOLUME flag will be set for the dwSupport member.
            /// If a device supports separate volume changes on the left and right channels, both the MIDICAPS_VOLUME and the MIDICAPS_LRVOLUME flags will be set for this member.
            myCapsA->dwSupport;
            return MMSYSERR_NOERROR;

        case (sizeof(MIDIOUTCAPSW)):
            myCapsW = (MIDIOUTCAPSW*)capsPtr;
            myCapsW->wMid = MM_UNMAPPED;
            myCapsW->wPid = MM_MPU401_MIDIOUT;
            memcpy(myCapsW->szPname, synthNameW, sizeof(synthNameW));
            memcpy(myCapsW->szPname + wcslen(synthNameW), uDeviceID ? synthPortBW : synthPortAW, sizeof(synthPortAW));
            myCapsW->wTechnology = MOD_MIDIPORT;
            myCapsW->vDriverVersion = 0x0090;
            myCapsW->wVoices = 0;
            myCapsW->wNotes = 0;
            myCapsW->wChannelMask = 0xffff;
            myCapsW->dwSupport;
            return MMSYSERR_NOERROR;

        case (sizeof(MIDIOUTCAPS2A)):
            myCaps2A = (MIDIOUTCAPS2A*)capsPtr;
            myCaps2A->wMid = MM_UNMAPPED;
            myCaps2A->wPid = MM_MPU401_MIDIOUT;
            memcpy(myCaps2A->szPname, synthName, sizeof(synthName));
            memcpy(myCaps2A->szPname + strlen(synthName), uDeviceID ? synthPortB : synthPortA, sizeof(synthPortA));
            myCaps2A->wTechnology = MOD_MIDIPORT;
            myCaps2A->vDriverVersion = 0x0090;
            myCaps2A->wVoices = 0;
            myCaps2A->wNotes = 0;
            myCaps2A->wChannelMask = 0xffff;
            myCaps2A->dwSupport;
            return MMSYSERR_NOERROR;

        case (sizeof(MIDIOUTCAPS2W)):
            myCaps2W = (MIDIOUTCAPS2W*)capsPtr;
            myCaps2W->wMid = MM_UNMAPPED;
            myCaps2W->wPid = MM_MPU401_MIDIOUT;
            memcpy(myCaps2W->szPname, synthNameW, sizeof(synthNameW));
            memcpy(myCaps2W->szPname + wcslen(synthNameW), uDeviceID ? synthPortBW : synthPortAW, sizeof(synthPortAW));
            myCaps2W->wTechnology = MOD_MIDIPORT;
            myCaps2W->vDriverVersion = 0x0090;
            myCaps2W->wVoices = 0;
            myCaps2W->wNotes = 0;
            myCaps2W->wChannelMask = 0xffff;
            myCaps2W->dwSupport;
            return MMSYSERR_NOERROR;

        default:
            return MMSYSERR_ERROR;
    }
}

BOOL DriverCallback(int driverNum, DWORD_PTR clientNum, DWORD dwMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    Driver::Client& client = drivers[driverNum].clients[clientNum];
    return DriverCallback(client.callback, client.flags, drivers[driverNum].hdrvr, dwMsg, client.instance, dwParam1, dwParam2);
}

/// <summary>
/// Creates new device driver with
/// </summary>
/// <param name="driver"></param>
/// <param name="uDeviceID"></param>
/// <param name="uMsg"></param>
/// <param name="dwUser"></param>
/// <param name="dwParam1"></param>
/// <param name="dwParam2"></param>
/// <returns></returns>
LONG OpenDriver(Driver& driver, UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    if (driver.clientCount == MAX_CLIENTS)
    {
        return MMSYSERR_ALLOCATED;
    }

    int i;
    for (i = 0; i < MAX_CLIENTS; ++i)
    {
        if (!driver.clients[i].allocated)
        {
            break;
        }
    }

    MIDIOPENDESC* desc = (MIDIOPENDESC*)dwParam1;
    driver.clients[i].allocated = true;
    driver.clients[i].flags = HIWORD(dwParam2);
    driver.clients[i].callback = desc->dwCallback;
    driver.clients[i].instance = desc->dwInstance;
    driver.clients[i].synth_instance = NULL;
    *(LONG*)dwUser = i;
    ++driver.clientCount;

    DriverCallback(uDeviceID, i, MOM_OPEN, NULL, NULL);

    return MMSYSERR_NOERROR;
}

LONG CloseDriver(Driver& driver, UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    if (!driver.clients[dwUser].allocated)
    {
        return MMSYSERR_INVALPARAM;
    }

    driver.clients[dwUser].allocated = false;
    --driver.clientCount;

    DriverCallback(uDeviceID, dwUser, MOM_CLOSE, NULL, NULL);

    return MMSYSERR_NOERROR;
}

/// <summary>
/// Entry-point function to process messages from the Windows operating system for musical instrument digital interface (MIDI) output drivers and for internal synthesizer drivers. 
/// This function processes messages that WINMM sends to the MIDI output driver.
/// WINMM is a Windows dynamic link library (DLL) module that contains functions that help the operating system and the MIDI output driver communicate with each other.
/// Specifically, WINMM helps to manage 16-bit multimedia applications that run on Windows.
/// https://docs.microsoft.com/en-us/windows-hardware/drivers/audio/audio-device-messages-for-midi
/// </summary>
/// <param name="uDeviceID">Specifies the ID of the target device. Device IDs are sequential and have an initial value of zero and a final value equal to one less than the number of devices the driver supports.</param>
/// <param name="uMsg">Specifies the message that WINMM sends to the driver in response to a call from the client application.</param>
/// <param name="dwUser">For the MODM_OPEN message, the driver should fill this location with its instance data. For any other messages, the instance data is returned to the driver. Drivers that support multiple clients can use this instance data to track which client is associated with the message.</param>
/// <param name="dwParam1">Specifies a message-dependent parameter.</param>
/// <param name="dwParam2">Specifies a message-dependent parameter. If there are flags that provide additional information to the driver that works with modMessage, WINMM uses this parameter to pass the flags.</param>
/// <remarks>
/// Audio device messages are system-defined constants. So when modMessage receives an audio device message, it uses a switch statement to determine the action to perform, based on the value of the message.
/// The range of error messages that modMessage can return depends on the message that it was processing when the error occurred.
/// The numerical values of the MMSYSERR_ error messages start with zero (for MMSYSERR_NOERROR) and continue with MMSYSERR_BASE + n, where n is an integer from 1 to 21.
/// The value for MMSYSERR_BASE is a defined constant.
/// For more information about MSYSERR_BASE and the MMSYSERR_ error messages, see Mmsystem.h in the Windows SDK and Mmddk.h in the WDK respectively.
/// </remarks>
/// <returns>
/// Returns MMSYSERR_NOERROR if it can successfully process the message it received from MMSYSTEM.
/// Otherwise, it returns one of the following error messages.
///     MMSYSERR_ERROR          Unspecified error.
///     MMSYSERR_BADDEVICEID    The specified device ID is out of range.
///     MMSYSERR_NOTENABLED     The driver failed to load or initialize.
///     MMSYSERR_ALLOCATED      The specified device is already allocated.
///     MMSYSERR_INVALHANDLE    The handle of the specified device is invalid.
///     MMSYSERR_NODRIVER       No device driver is present.
///     MMSYSERR_NOMEM          Memory allocation error.
///     MMSYSERR_NOTSUPPORTED   The function requested by the message is not supported.
///     MMSYSERR_BADERRNUM      Error value is out of range.See Remarks section for more details.
///     MMSYSERR_INVALFLAG      An invalid flag was passed to modMessage(by using dwParam2).
///     MMSYSERR_INVALPARAM     An invalid parameter was passed to modMessage.
///     MMSYSERR_HANDLEBUSY     The specified handle is being used simultaneously by another thread(for example, a callback thread).
///     MMSYSERR_INVALIDALIAS   The specified alias was not found.
///     MMSYSERR_BADDB          Bad registry database.
///     MMSYSERR_KEYNOTFOUND    The specified registry key was not found.
///     MMSYSERR_READERROR      Registry read error.
///     MMSYSERR_WRITEERROR     Registry write error.
///     MMSYSERR_DELETEERROR    Registry delete error.
///     MMSYSERR_VALNOTFOUND    The specified registry value was not found.
///     MMSYSERR_NODRIVERCB     The driver that works with modMessage does not call DriverCallback.
///     MMSYSERR_MOREDATA       modMessage has more data to return.
///     MMSYSERR_LASTERROR      Indicates that this is the last error in the range of error values.See the Remarks section for more details.
/// </returns>
STDAPI_(DWORD) modMessage(DWORD uDeviceID, DWORD uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    MIDIHDR* midiHdr;
    Driver& driver = drivers[uDeviceID];
    switch (uMsg)
    {
        case MODM_OPEN:
            /// WINMM sends the MODM_OPEN message to the modMessage function of a MIDI output driver to allocate a specified device that a client application can use.
            ///
            /// uDeviceID
            ///		Specifies the ID of the target device.
            ///		Device IDs are sequential and have an initial value of zero and a final value equal that is to one less than the number of devices that the driver supports.
            /// uMsg
            ///		WINMM sets this parameter to MODM_OPEN when it calls modMessage to process this message.
            /// dwUser
            ///		The MIDI output driver must fill this location with its instance data, but only in response to the MODM_OPEN.
            /// dwParam1
            ///		This parameter specifies a far pointer to a MIDIOPENDESC structure.
            ///		This structure contains additional information for the driver such as instance data from the client and a callback function for the client.
            /// dwParam2
            ///		This parameter specifies option flags that determine how the device is opened. The flags can be any of the values in the following table.
            ///			CALLBACK_EVENT
            ///				If this flag is specified, dwCallback in the MIDIOPENDESC structure is assumed to be an event handle.
            ///			CALLBACK_FUNCTION
            ///				If this flag is specified, dwCallback in the MIDIOPENDESC structure is assumed to be the address of a callback function.
            ///			CALLBACK_THREAD
            ///				If this flag is specified, dwCallback in the MIDIOPENDESC structure is assumed to be a handle to a thread.
            ///			CALLBACK_WINDOW
            ///				If this flag is specified, dwCallback in the MIDIOPENDESC structure is assumed to be a window handle.
            ///			MIDI_IO_COOKED
            ///				If this flag is specified, the device is opened in stream mode and the driver receives stream messages.
            ///				The driver must be able to handle any contingencies that arise.
            ///				For example, the driver must be able to play short messages and system - exclusive messages asynchronously to the stream.
            /// 
            /// The modMessage function returns MMSYSERR_NOERROR if the operation is successful.
            /// Otherwise it returns one of the error messages in the following table.
            ///     MMSYSERR_NOTENABLED     The driver failed to load or initialize.
            ///     MMSYSERR_ALLOCATED      The MIDI device is already allocated by the maximum number of clients that the driver supports or
            ///                             the device cannot be opened because of system resource limitations other than memory.
            ///     MMSYSERR_NOMEM          The device cannot be opened because of a failure to allocate or lock memory.
            /// 
            /// The driver must be able to determine the number of clients it can allow to use a particular device.
            /// After a device is opened for the maximum number of clients that the driver supports, the driver returns MMSYSERR_ALLOCATED for any additional requests to open the device.
            /// If the open operation is successful, the driver uses the DriverCallback function to send the client a MOM_OPEN message.
            if (!isSynthOpened)
            {
                if (midiSynth.Init(uDeviceID) != 0)
                {
                    return MMSYSERR_NOTENABLED;
                }
                isSynthOpened = true;
            }

            return OpenDriver(driver, uDeviceID, uMsg, dwUser, dwParam1, dwParam2);

        case MODM_CLOSE:
            if (isSynthOpened)
            {
                midiSynth.Reset(uDeviceID);
                midiSynth.Close();
                isSynthOpened = false;
            }
            return CloseDriver(driver, uDeviceID, uMsg, dwUser, dwParam1, dwParam2);

        case MODM_PREPARE:
            return MMSYSERR_NOTSUPPORTED;

        case MODM_UNPREPARE:
            return MMSYSERR_NOTSUPPORTED;

        case MODM_GETDEVCAPS:
            /// WINMM sends the MODM_GETDEVCAPS message to the modMessage function of a MIDI output driver to retrieve information about the capabilities of a specific MIDI output device.
            /// uDeviceID
            ///     Specifies the ID of the target device.
            ///     Device IDs are sequential and have an initial value of zero and a final value that is equal to one less than the number of devices that the driver supports.
            /// uMsg
            ///     WINMM sets this parameter to MODM_GETDEVCAPS when it calls modMessage to process this message.
            /// dwUser
            ///     Use this parameter to return instance data to the driver.
            ///     Drivers that support multiple clients can use this instance data to track the client that is associated with the message.
            /// dwParam1
            ///     For Plug and Play(PnP) drivers, this parameter specifies a pointer to an MDEVICECAPSEX structure.
            ///     The driver fills this structure with the capabilities of the device.
            ///     For drivers that are not Plug and Play, this parameter specifies a far pointer to a MIDIOUTCAPS data structure.
            ///     The driver fills this structure with the capabilities of the device.
            /// dwParam2
            ///     For Plugand Play drivers, this parameter specifies a device node.
            ///     For drivers that are not Plug and Play, this parameter specifies the size of the MIDIOUTCAPS structure in bytes.
            ///
            /// For drivers that are not Plug and Play, the driver must only write dwParam2 or less bytes to the location pointed to by dwParam1.
            ///
            /// If the operation is successful, modMessage returns MMSYSERR_NOERROR.
            /// Otherwise, it returns MMSYSERR_NOTENABLED to indicate that the driver failed to load or initialize.
            return GetMidiDeviceCapabilities(uDeviceID, (PVOID)dwParam1, (DWORD)dwParam2);

        case MODM_DATA:
            /// MMSYSTEM sends the MODM_DATA message to the modMessage function of a MIDI output driver when the client wants to make a single MIDI event available as output.
            /// uDeviceID
            ///     Specifies the ID of the target device. Device IDs are sequential and have an initial value of zero and a final value that is equal to one less than the number of devices that the driver supports.
            /// uMSG
            ///     WINMM sets this parameter to MODM_DATA when it calls modMessage to process this message.
            /// dwUser
            ///     Use this parameter to return instance data to the driver.
            ///     Drivers that support multiple clients can use this instance data to track the client that is associated with the message.
            /// dwParam1
            ///     Specifies the MIDI event that will be available at the output.
            ///     The low-order byte is the first byte of the event.
            /// dwParam2
            ///     Not used.
            /// 
            /// If the operation is successful, modMessage returns MMSYSERR_NOERROR.
            /// Otherwise, it returns one of the error messages in the following table.
            ///     MMSYSERR_NOTENABLED     The driver failed to load or initialize.
            ///     MIDIERR_NOTREADY        The MIDI hardware is busy processing other data.
            /// 
            /// This message is used to make all MIDI events available as output, except system-exclusive events.
            /// System-exclusive events are communicated with the MODM_LONGDATA message.
            /// MIDI events that are communicated with MODM_DATA can be one, two, or three bytes long.
            /// The driver must parse the event to determine how many bytes to transfer.
            /// Unused bytes are not guaranteed to be zero.
            /// 
            /// The driver developer can develop a driver to not return until the message has been sent to the output device.
            /// Alternatively, the driver can return immediately and the MIDI data can be output in the background.
            if (!driver.clients[dwUser].allocated)
            {
                return MMSYSERR_NOTENABLED;
            }

            return midiSynth.PutMidiMessage(uDeviceID, (DWORD)dwParam1);

        case MODM_LONGDATA:
            if (!driver.clients[dwUser].allocated)
            {
                return MMSYSERR_NOTENABLED;
            }

            midiHdr = (MIDIHDR*)dwParam1;

            if ((midiHdr->dwFlags & MHDR_PREPARED) == 0)
            {
                return MIDIERR_UNPREPARED;
            }

            midiHdr->dwFlags &= ~MHDR_DONE;
            midiHdr->dwFlags |= MHDR_INQUEUE;

            midiSynth.PutSysEx(uDeviceID, (unsigned char*)midiHdr->lpData, midiHdr->dwBufferLength);

            midiHdr->dwFlags |= MHDR_DONE;
            midiHdr->dwFlags &= ~MHDR_INQUEUE;
            DriverCallback(uDeviceID, dwUser, MOM_DONE, dwParam1, NULL);

            return MMSYSERR_NOERROR;

        case MODM_GETNUMDEVS:
            /// WINMM sends the MODM_GETNUMDEVS message to the modMessage function of a MIDI output driver to request the number of MIDI output devices available.
            /// The modMessage function returns the number of MIDI output devices that the driver supports.
            return MAX_DRIVERS;

        case MODM_GETVOLUME:
            /// WINMM sends the MODM_GETVOLUME message to the modMessage function of a MIDI output driver to request the current volume level setting for a MIDI device.
            /// uDeviceID
            ///     Specifies the ID of the target device. Device IDs are sequential and have an initial value of zero and a final value that is equal to one less than the number of devices that the driver supports.
            /// uMsg
            ///     WINMM sets this parameter to MODM_GETVOLUME when it calls modMessage to process this message.
            /// dwUser
            ///     Use this parameter to return instance data to the driver.
            ///     Drivers that support multiple clients can use this instance data to track the client that is associated with the message.
            /// dwParam1
            ///     This parameter specifies a far pointer to a DWORD location.
            ///     The driver fills this location with the current volume level setting.
            ///     The high-order word contains the right channel setting and the low-order word contains the left channel setting.
            ///     A value of zero is silence, and a value of 0xFFFF is full volume.
            ///     If the driver does not support both left and right channel volume changes, it returns the volume level setting in the low-order word.
            /// dwParam2
            ///     Not used.
            /// 
            /// Only drivers for internal synthesizer devices can support volume level changes.
            /// Drivers for MIDI output ports should return an MMSYSERR_NOTSUPPORTED error for this message.
            /// Support for volume level changes by internal synthesizer devices is optional.
            /// However, if a driver supports changes to the volume level with the MODM_SETVOLUME message, it must support queries with the MODM_GETVOLUME message.

            * (LONG*)dwParam1 = (LONG)(SynthVolume * 0xFFFF);
            // midiOutGetVolume((HMIDIOUT)dwUser, (LPDWORD)dwParam1);
            return MMSYSERR_NOERROR;

        case MODM_SETVOLUME:
            /// WINMM sends the MODM_SETVOLUME message to the modMessage function of a MIDI output driver to set the volume for a MIDI device.
            ///
            /// uDeviceID
            ///     Specifies the ID of the target device.
            ///     Device IDs are sequential and have an initial value of zero and a final value that is equal to one less than the number of devices that the driver supports.
            /// uMsg
            ///     WINMM sets this parameter to MODM_SETVOLUME when it calls modMessage to process this message.
            /// dwUser
            ///     Use this parameter to return instance data to the driver.
            ///     Drivers that support multiple clients can use this instance data to track the client that is associated with the message.
            /// dwParam1
            ///     This parameter specifies the new volume level.
            ///     The high-order word contains the right channel setting and the low-order word contains the left channel setting.
            ///     A value of zero is silence, and a value of 0xFFFF is full volume.
            ///     If the driver does not support both left and right channel volume changes, it uses the volume specified in the low-order word.
            ///     The driver will typically not support the full 16 bits of volume control and must truncate the lower bits if necessary.
            ///     However, the original volume level set with MODM_SETVOLUME must be returned with MODM_GETVOLUME.
            /// dwParam2
            ///     Not used.
            /// 
            /// The modMessage function returns MMSYSERR_NOERROR if the operation was successful.
            /// Otherwise, it returns one of the error messages in the following table.
            ///     MMSYSERR_NOTENABLED     The driver failed to load or initialize.
            ///     MMSYSERR_NOTSUPPORTED   The driver does not support changes to volume level.
            /// 
            /// This volume level is the final output volume; therefore, only drivers for internal synthesizer devices can support volume level changes.
            /// Drivers for MIDI output ports must return a MMSYSERR_NOTSUPPORTED error for this message.
            /// Support for volume level changes is optional for internal synthesizer devices.
            /// When a driver receives a MODM_GETDEVCAPS message, it must indicate support for volume level changes by setting or clearing the MIDICAPS_VOLUME and MIDICAPS_LRVOLUME bits in the dwSupport field of the MIDIOUTCAPS data structure.
            /// If a driver supports the MODM_SETVOLUME message, it must also support MODM_GETVOLUME.

            //midiSynth.PutSysex(uDeviceID, (unsigned char*)SysexXgVolume, sizeof(SysexXgVolume));

            /// The midiOutSetVolume function sets the volume of a MIDI output device.
            /// hmo
            ///     Handle to an open MIDI output device.
            ///     This parameter can also contain the handle of a MIDI stream, as long as it is cast to HMIDIOUT.
            ///     This parameter can also be a device identifier.
            /// dwVolume
            ///     New volume setting.
            ///     The low-order word contains the left-channel volume setting, and the high-order word contains the right-channel setting.
            ///     A value of 0xFFFF represents full volume, and a value of 0x0000 is silence.
            ///     If a device does not support both leftand right volume control, the low - order word of dwVolume specifies the mono volume level, and the high-order word is ignored.
            /// 
            /// Returns MMSYSERR_NOERROR if successful or an error otherwise.
            /// Possible error values include the following.
            ///     MMSYSERR_INVALHANDLE    The specified device handle is invalid.
            ///     MMSYSERR_NOMEM          The system is unable to allocate or lock memory.
            ///     MMSYSERR_NOTSUPPORTED   The function is not supported.
            ///
            /// If a device identifier is used, then the result of the midiOutSetVolume call applies to all instances of the device.
            /// If a device handle is used, then the result applies only to the instance of the device referenced by the device handle.
            /// 
            /// Not all devices support volume changes.
            /// You can determine whether a device supports it by querying the device using the midiOutGetDevCaps function and the MIDICAPS_VOLUME flag.
            /// 
            /// You can also determine whether the device supports volume control on both the left and right channels by querying the device using the midiOutGetDevCaps function and the MIDICAPS_LRVOLUME flag.
            /// 
            /// Devices that do not support a full 16 bits of volume-level control use the high-order bits of the requested volume setting.
            /// For example, a device that supports 4 bits of volume control produces the same volume setting for the following volume-level values: 0x4000, 0x43be, and 0x4fff.
            /// The midiOutGetVolume function returns the full 16-bit value, as set by midiOutSetVolume, irrespective of the device's capabilities.
            /// 
            /// Volume settings are interpreted logarithmically.
            /// This means that the perceived increase in volume is the same when increasing the volume level from 0x5000 to 0x6000 as it is from 0x4000 to 0x5000.

            //midiOutSetVolume((HMIDIOUT)dwUser, 0x0000);
            return MMSYSERR_NOERROR;

        default:
            return MMSYSERR_NOERROR;
    }
}