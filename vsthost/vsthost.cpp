// vsthost.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <string>

// #define LOG_EXCHANGE

using std::vector;

enum
{
    BUFFER_SIZE = 4096
};

enum Command : uint32_t
{
    GetChunkData = 1,
    SetChunkData = 2,
    HasEditor = 3,
    DisplayEditorModal = 4,
    SetSampleRate = 5,
    Reset = 6,
    SendMidiEvent = 7,
    SendMidiSystemExclusiveEvent = 8,
    RenderAudioSamples = 9,
};

enum Response : uint32_t
{
    NoError = 0,
    CannotLoadVstiDll = 6,
    CannotGetProcAddress = 7,
    NotAVsti = 8,
    CannotReset = 8,
    VstiIsNotAMidiSynth = 9,
    CannotSetSampleRate = 10,
    CannotRenderAudioSamples = 11,
    CommandUnknown = 12,
};

enum Error : uint32_t
{
    InvalidCommandLineArguments = 1,
    MalformedChecksum = 2,
    ChecksumMismatch = 3,
    Comctl32LoadFailed = 4,
    ComStaInitializationFailed = 5,
};

/// <summary>
/// VSTi MIDI event and VSTi Midi System Exclusive Event
/// </summary>
struct MidiEvent
{
    struct MidiEvent* next;
    unsigned port;
    union
    {
        VstMidiEvent midiEvent;
        VstMidiSysexEvent sysexEvent;
    } ev;
};

MidiEvent* evChain = NULL;
MidiEvent* evTail = NULL;

bool need_idle = false;
bool idle_started = false;

static char* dll_dir = NULL;

static HANDLE null_file = NULL;
static HANDLE pipe_in = NULL;
static HANDLE pipe_out = NULL;

void FreeMidiEventChain()
{
    MidiEvent* ev = evChain;
    while (ev)
    {
        MidiEvent* next = ev->next;
        if (ev->port && ev->ev.sysexEvent.type == VstEventTypes::kVstSysExType)
        {
            free(ev->ev.sysexEvent.sysexDump);
        }
        free(ev);
        ev = next;
    }
    evChain = NULL;
    evTail = NULL;
}

#ifdef LOG_EXCHANGE
unsigned exchange_count = 0;
#endif

/// <summary>
/// Send data to the VST driver via the pipe output 
/// </summary>
/// <param name="data">The data to send</param>
/// <param name="size">The size of the data</param>
void SendData(const void* data, uint32_t size)
{
    if (size)
    {
        DWORD dwWritten;
        WriteFile(pipe_out, data, size, &dwWritten, NULL);
#ifdef LOG_EXCHANGE
        TCHAR logfile[MAX_PATH];
        _stprintf_s(logfile, _T("C:\\temp\\log\\bytes_%08u.out"), ++exchange_count);
        FILE* f = _tfopen(logfile, _T("wb"));
        fwrite(data, 1, size, f);
        fclose(f);
#endif
    }
}

/// <summary>
/// Send data to the VST driver via the pipe output 
/// </summary>
/// <param name="data">The data to send</param>
void SendData(uint32_t data)
{
    SendData(&data, sizeof(data));
}

/// <summary>
/// Send data to the VST driver via the pipe output 
/// </summary>
/// <param name="data">The data to send</param>
void SendData(const char* data)
{
    SendData(data, strlen(data));
}

/// <summary>
/// Send data to the VST driver via the pipe output 
/// </summary>
/// <param name="data">The data to send</param>
void SendData(vector<uint8_t> data)
{
    SendData(data.data(), data.size());
}

/// <summary>
/// Receive data from the VST driver via the pipe input
/// </summary>
/// <param name="data">The received data</param>
/// <param name="size">The size of the data</param>
void ReceiveData(void* data, uint32_t size)
{
    DWORD dwRead;
    if (ReadFile(pipe_in, data, size, &dwRead, NULL) && dwRead >= size)
    {
#ifdef LOG_EXCHANGE
        TCHAR logfile[MAX_PATH];
        _stprintf_s(logfile, _T("C:\\temp\\log\\bytes_%08u.in"), ++exchange_count);
        FILE* f = _tfopen(logfile, _T("wb"));
        fwrite(data, 1, size, f);
        fclose(f);
#endif
    }
    else
    {
        memset(data, 0, size);
#ifdef LOG_EXCHANGE
        TCHAR logfile[MAX_PATH];
        _stprintf_s(logfile, _T("C:\\temp\\log\\bytes_%08u.err"), ++exchange_count);
        FILE* f = _tfopen(logfile, _T("wb"));
        _ftprintf(f, _T("Wanted %u bytes, got %u"), size, dwRead);
        fclose(f);
#endif
    }
}

/// <summary>
/// Receive data from the VST driver via the pipe input
/// </summary>
uint32_t ReceiveData()
{
    uint32_t data;
    ReceiveData(&data, sizeof(data));
    return data;
}

void GetChunk(AEffect* pEffect, vector<uint8_t>& out)
{
    out.resize(0);
    uint32_t unique_id = pEffect->uniqueID;
    append_be(out, unique_id);
    bool type_chunked = !!(pEffect->flags & VstAEffectFlags::effFlagsProgramChunks);
    append_be(out, type_chunked);
    if (!type_chunked)
    {
        uint32_t num_params = pEffect->numParams;
        append_be(out, num_params);
        for (unsigned i = 0; i < num_params; ++i)
        {
            float parameter = pEffect->getParameter(pEffect, i);
            append_be(out, parameter);
        }
    }
    else
    {
        void* chunk;
        uint32_t size = pEffect->dispatcher(pEffect, AEffectOpcodes::effGetChunk, 0, 0, &chunk, 0);
        append_be(out, size);
        size_t chunk_size = out.size();
        out.resize(chunk_size + size);
        memcpy(&out[chunk_size], chunk, size);
    }
}

void SetChunk(AEffect* pEffect, vector<uint8_t> const& in)
{
    unsigned size = in.size();
    if (pEffect && size)
    {
        const uint8_t* inc = in.data();
        uint32_t effect_id;
        retrieve_be(effect_id, inc, size);
        if (effect_id != pEffect->uniqueID)
        {
            return;
        }

        bool type_chunked;
        retrieve_be(type_chunked, inc, size);

        if (type_chunked != !!(pEffect->flags & VstAEffectFlags::effFlagsProgramChunks))
        {
            return;
        }

        if (!type_chunked)
        {
            uint32_t num_params;
            retrieve_be(num_params, inc, size);
            if (num_params != pEffect->numParams)
            {
                return;
            }
            for (unsigned i = 0; i < num_params; ++i)
            {
                float parameter;
                retrieve_be(parameter, inc, size);
                pEffect->setParameter(pEffect, i, parameter);
            }
        }
        else
        {
            uint32_t chunk_size;
            retrieve_be(chunk_size, inc, size);
            if (chunk_size > size)
            {
                return;
            }
            pEffect->dispatcher(pEffect, AEffectOpcodes::effSetChunk, 0, chunk_size, (void*)inc, 0);
        }
    }
}

struct MyDLGTEMPLATE : DLGTEMPLATE
{
    WORD ext[3];
    MyDLGTEMPLATE()
    {
        memset(this, 0, sizeof(*this));
    };
};

INT_PTR CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    AEffect* effect;
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam);
            SetWindowText(hwnd, L"VST Host");
            SetTimer(hwnd, 1, 20, 0);
            effect = (AEffect*)lParam;
            if (effect)
            {
                char product[VstStringConstants::kVstMaxProductStrLen] = { 0 };
                effect->dispatcher(effect, AEffectXOpcodes::effGetProductString, 0, 0, &product, 0);

                SetWindowTextA(hwnd, (LPCSTR)product);

                effect->dispatcher(effect, AEffectOpcodes::effEditOpen, 0, 0, hwnd, 0);
                ERect* eRect = 0;
                effect->dispatcher(effect, AEffectOpcodes::effEditGetRect, 0, 0, &eRect, 0);
                if (eRect)
                {
                    int width = eRect->right - eRect->left;
                    int height = eRect->bottom - eRect->top;
                    if (width < 50)
                    {
                        width = 50;
                    }
                    if (height < 50)
                    {
                        height = 50;
                    }
                    RECT wRect;
                    SetRect(&wRect, 0, 0, width, height);
                    AdjustWindowRectEx(&wRect, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
                    width = wRect.right - wRect.left;
                    height = wRect.bottom - wRect.top;
                    SetWindowPos(hwnd, HWND_TOP, 0, 0, width, height, SWP_SHOWWINDOW | SWP_NOMOVE);
                }
            }
        }
        break;

        case WM_TIMER:
            effect = (AEffect*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (effect)
            {
                effect->dispatcher(effect, AEffectOpcodes::effEditIdle, 0, 0, 0, 0);
            }
            break;

        case WM_CLOSE:
        {
            effect = (AEffect*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            KillTimer(hwnd, 1);
            if (effect)
            {
                effect->dispatcher(effect, AEffectOpcodes::effEditClose, 0, 0, 0, 0);
            }

            EndDialog(hwnd, IDOK);
        }
        break;
    }

    return 0;
}

struct audioMasterData
{
    VstIntPtr effect_number;
};

static VstIntPtr VSTCALLBACK audioMaster(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    audioMasterData* data = NULL;
    if (effect)
    {
        data = (audioMasterData*)effect->user;
    }

    switch (opcode)
    {
        case AudioMasterOpcodes::audioMasterAutomate:
            /// The user changed a parameter in plug-ins editor, which is out of the Host's control.
            /// This ensures that the Host is notified of the parameter change, which
            /// allows it to record these changes for automation.
            /// [index]: parameter index
            /// [opt]: parameter value
            /// @see AudioEffect::setParameterAutomated
            break;

        case AudioMasterOpcodes::audioMasterVersion:
            /// Used to ask for the Host's version
            /// [return value]: Host VST version (for example 2400 for VST 2.4)
            /// @see AudioEffect::getMasterVersion
            return kVstVersion;

        case AudioMasterOpcodes::audioMasterCurrentId:
            /// [return value]: current unique identifier on shell plug-in
            /// @see AudioEffect::getCurrentUniqueId
            if (data)
            {
                return data->effect_number;
            }
            break;

        case AudioMasterOpcodes::audioMasterIdle:
            /// Give idle time to Host application, e.g. if plug-in editor is doing mouse tracking in a modal loop.
            /// no arguments
            /// @see AudioEffect::masterIdle
            break;

        case AudioMasterOpcodes::audioMasterPinConnected:
            /// [return value]: 0=true, 1=false
            /// [index]: pin index
            /// [value]: 0=input, 1=output
            /// @see AudioEffect::isInputConnected
            /// @see AudioEffect::isOutputConnected
            break;

        case AudioMasterOpcodesX::audioMasterGetVendorString:
            /// [ptr]: char buffer for vendor string, limited to #kVstMaxVendorStrLen
            /// @see AudioEffectX::getHostVendorString
            strncpy((char*)ptr, "NoWork, Inc.", VstStringConstants::kVstMaxVendorStrLen);
            //strncpy((char *)ptr, "YAMAHA", 64);
            break;

        case AudioMasterOpcodesX::audioMasterGetProductString:
            /// [ptr]: char buffer for vendor string, limited to #kVstMaxProductStrLen
            /// @see AudioEffectX::getHostProductString
            strncpy((char*)ptr, "VSTi Host Bridge", VstStringConstants::kVstMaxProductStrLen);
            //strncpy((char *)ptr, "SOL/SQ01", 64);
            break;

        case AudioMasterOpcodesX::audioMasterGetVendorVersion:
            /// [return value]: vendor-specific version
            /// @see AudioEffectX::getHostVendorVersion
            return 1010;

        case AudioMasterOpcodesX::audioMasterGetLanguage:
            /// [return value]: language code
            /// @see VstHostLanguage
            return VstHostLanguage::kVstLangEnglish;

        case AudioMasterOpcodesX::audioMasterVendorSpecific:
            /// no definition, vendor specific handling
            /// @see AudioEffectX::hostVendorSpecific
            /* Steinberg HACK */
            if (ptr)
            {
                uint32_t* blah = (uint32_t*)(((char*)ptr) - 4);
                if (*blah == 0x0737bb68)
                {
                    *blah ^= 0x5CC8F349;
                    blah[2] = 0x19E;
                    return 0x1E7;
                }
            }
            break;

        case AudioMasterOpcodesX::audioMasterGetDirectory:
            /// [return value]: FSSpec on 32 bit MAC, else char*
            /// @see AudioEffectX::getDirectory
            return (VstIntPtr)dll_dir;

            /* More crap */
        case DECLARE_VST_DEPRECATED(audioMasterNeedIdle):
            need_idle = true;
            return 0;
    }

    return 0;
}

LONG __stdcall myExceptFilterProc(LPEXCEPTION_POINTERS param)
{
    if (IsDebuggerPresent())
    {
        return UnhandledExceptionFilter(param);
    }
    else
    {
        //DumpCrashInfo( param );
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;// never reached
    }
}

#pragma comment(lib, "Winmm")

LPTIMECALLBACK TimeProc(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    MyDLGTEMPLATE vstiEditor;
    AEffect* pEffect = (AEffect*)dwUser;
    vstiEditor.style = WS_POPUPWINDOW | WS_DLGFRAME | WS_MINIMIZEBOX | WS_SYSMENU | WS_CAPTION | DS_MODALFRAME | DS_CENTER;
    DialogBoxIndirectParam(0, &vstiEditor, 0, (DLGPROC)WindowProc, (LPARAM)pEffect);
    return 0;
}

int CALLBACK _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv == NULL || argc != 3)
    {
        return Error::InvalidCommandLineArguments;
    }

    /// Get the checksum from the 3rd argument
    wchar_t* end_char = 0;
    unsigned inputChecksum = wcstoul(argv[2], &end_char, 16);
    if (end_char == argv[2] || *end_char)
    {
        return Error::MalformedChecksum;
    }

    /// Calculate the checksum from the 2nd argument
    unsigned checksum = 0;
    end_char = argv[1];
    while (*end_char)
    {
        checksum += (TCHAR)(*end_char++ * 820109);
    }

    if (inputChecksum != checksum)
    {
        return Error::ChecksumMismatch;
    }

    AEffect* pEffect = { 0 };

    audioMasterData effectData = { 0 };

    vector<uint8_t> blState;

    uint32_t sampleRate = 44100;

    vector<uint8_t> chunk;
    vector<float> sample_buffer;
    //unsigned int samples_buffered = 0;

    null_file = CreateFile(_T("NUL"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    pipe_in = GetStdHandle(STD_INPUT_HANDLE);
    pipe_out = GetStdHandle(STD_OUTPUT_HANDLE);

    SetStdHandle(STD_INPUT_HANDLE, null_file);
    SetStdHandle(STD_OUTPUT_HANDLE, null_file);

    /// Carries information used to load common control classes from the dynamic-link library (DLL).
    /// This structure is used with the InitCommonControlsEx function.
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_STANDARD_CLASSES;
    /// Ensures that the common control DLL (Comctl32.dll) is loaded, and registers specific common control classes from the DLL.
    /// An application must call this function before creating a common control.
    if (!InitCommonControlsEx(&icc))
    {
        return Error::Comctl32LoadFailed;
    }

    /// Initializes the COM library on the current thread and identifies the concurrency model as single-thread apartment (STA).
    if (FAILED(CoInitialize(NULL)))
    {
        return Error::ComStaInitializationFailed;
    }

#ifndef _DEBUG
    SetUnhandledExceptionFilter(myExceptFilterProc);
#endif

    size_t dll_name_len = wcslen(argv[1]);
    dll_dir = (char*)malloc(dll_name_len + 1);
    wcstombs(dll_dir, argv[1], dll_name_len);
    dll_dir[dll_name_len] = '\0';
    char* slash = strrchr(dll_dir, '\\');
    *slash = '\0';

    uint32_t code = 0;

    /// Load the VSTi DLL which is passed as the first argument
    /// "C:\Projects\New\VST\VSTDriver\output\vsthost32.exe" "C:\Program Files (x86)\yamaha_syxg50_vsti\syxg50.dll" 001AF06A (1765482)
    HMODULE vstiDll = LoadLibraryW(argv[1]);
    if (!vstiDll)
    {
        code = Response::CannotLoadVstiDll;
        goto exit;
    }

    main_func pMain = (main_func)GetProcAddress(vstiDll, "VSTPluginMain");
    if (!pMain)
    {
        pMain = (main_func)GetProcAddress(vstiDll, "main");
        if (!pMain)
        {
            pMain = (main_func)GetProcAddress(vstiDll, "MAIN");
            if (!pMain)
            {
                code = Response::CannotGetProcAddress;
                goto exit;
            }
        }
    }

#if 0
    MessageBox(GetDesktopWindow(), argv[1], _T("HUUUURRRRRR"), 0);
#endif

    pEffect = pMain(&audioMaster);
    if (!pEffect || pEffect->magic != kEffectMagic)
    {
        code = Response::NotAVsti;
        goto exit;
    }

    pEffect->user = &effectData;
    pEffect->dispatcher(pEffect, AEffectOpcodes::effOpen, 0, 0, 0, 0);

    if (pEffect->dispatcher(pEffect, AEffectXOpcodes::effGetPlugCategory, 0, 0, 0, 0) != kPlugCategSynth || pEffect->dispatcher(pEffect, AEffectXOpcodes::effCanDo, 0, 0, (void*)"receiveVstMidiEvent", 0) < 1)
    {
        code = Response::VstiIsNotAMidiSynth;
        goto exit;
    }

    /// <summary>
    /// The number of VSTi audio outputs
    /// </summary>
    uint32_t audioOutputs = min(pEffect->numOutputs, 2);

    {
        char effectName[VstStringConstants::kVstMaxEffectNameLen] = { 0 };
        pEffect->dispatcher(pEffect, AEffectXOpcodes::effGetEffectName, 0, 0, &effectName, 0);

        char vendor[VstStringConstants::kVstMaxVendorStrLen] = { 0 };
        pEffect->dispatcher(pEffect, AEffectXOpcodes::effGetVendorString, 0, 0, &vendor, 0);

        char product[VstStringConstants::kVstMaxProductStrLen] = { 0 };
        pEffect->dispatcher(pEffect, AEffectXOpcodes::effGetProductString, 0, 0, &product, 0);

        uint32_t vendorVersion = pEffect->dispatcher(pEffect, AEffectXOpcodes::effGetVendorVersion, 0, 0, 0, 0);
        uint32_t uniqueId = pEffect->uniqueID;

        SendData(Response::NoError);
        SendData(strlen(effectName));
        SendData(strlen(vendor));
        SendData(strlen(product));
        SendData(vendorVersion);
        SendData(uniqueId);
        SendData(audioOutputs);

        SendData(effectName);
        SendData(vendor);
        SendData(product);
    }

    float** float_list_in;
    float** float_list_out;
    float* float_null;
    float* float_out;

    for (;;)
    {
        uint32_t command = ReceiveData();
        if (!command)
        {
            break;
        }

        switch (command)
        {
            case Command::GetChunkData:
            {
                GetChunk(pEffect, chunk);

                SendData(0u);
                SendData(chunk.size());
                SendData(chunk);
            }
            break;

            case Command::SetChunkData:
            {
                uint32_t size = ReceiveData();
                chunk.resize(size);
                if (size)
                {
                    ReceiveData(chunk.data(), size);
                }

                SetChunk(pEffect, chunk);

                SendData(0u);
            }
            break;

            case Command::HasEditor:
            {
                uint32_t hasEditor = pEffect->flags & VstAEffectFlags::effFlagsHasEditor;

                SendData(0u);
                SendData(hasEditor);
            }
            break;

            case Command::DisplayEditorModal:
            {
                if (pEffect->flags & VstAEffectFlags::effFlagsHasEditor)
                {
                    timeSetEvent(100, 10, (LPTIMECALLBACK)TimeProc, (DWORD)pEffect, TIME_ONESHOT);
                }

                SendData(0u);
            }
            break;

            case Command::SetSampleRate:
            {
                uint32_t size = ReceiveData();
                if (size != sizeof(sampleRate))
                {
                    code = Response::CannotSetSampleRate;
                    goto exit;
                }

                sampleRate = ReceiveData();

                SendData(0u);
            }
            break;

            case Command::Reset:
            {
                if (blState.size())
                {
                    pEffect->dispatcher(pEffect, AEffectXOpcodes::effStopProcess, 0, 0, 0, 0);
                }
                pEffect->dispatcher(pEffect, AEffectOpcodes::effClose, 0, 0, 0, 0);

                blState.resize(0);

                FreeMidiEventChain();

                pEffect = pMain(&audioMaster);
                if (!pEffect)
                {
                    code = Response::CannotReset;
                    goto exit;
                }
                pEffect->user = &effectData;
                pEffect->dispatcher(pEffect, AEffectOpcodes::effOpen, 0, 0, 0, 0);
                SetChunk(pEffect, chunk);

                SendData(0u);
            }
            break;

            case Command::SendMidiEvent:
            {
                MidiEvent* ev = (MidiEvent*)calloc(sizeof(MidiEvent), 1);
                if (evTail)
                {
                    evTail->next = ev;
                }
                evTail = ev;
                if (!evChain)
                {
                    evChain = ev;
                }

                uint32_t b = ReceiveData();

                /// To Do - Limit the midi ports to one per host
                ev->port = (b & 0x7F000000) >> 24;
                if (ev->port > 2)
                {
                    ev->port = 2;
                }
                ev->ev.midiEvent.type = VstEventTypes::kVstMidiType;
                ev->ev.midiEvent.byteSize = sizeof(ev->ev.midiEvent);
                memcpy(&ev->ev.midiEvent.midiData, &b, 3);

                SendData(0u);
            }
            break;

            case Command::SendMidiSystemExclusiveEvent:
            {
                MidiEvent* ev = (MidiEvent*)calloc(sizeof(MidiEvent), 1);
                if (evTail)
                {
                    evTail->next = ev;
                }
                evTail = ev;
                if (!evChain)
                {
                    evChain = ev;
                }

                uint32_t size = ReceiveData();
                uint32_t port = size >> 24;
                size &= 0xFFFFFF;

                ev->port = port;
                if (ev->port > 2)
                {
                    ev->port = 2;
                }
                ev->ev.sysexEvent.type = VstEventTypes::kVstSysExType;
                ev->ev.sysexEvent.byteSize = sizeof(ev->ev.sysexEvent);
                ev->ev.sysexEvent.dumpBytes = size;
                ev->ev.sysexEvent.sysexDump = (char*)malloc(size);

                ReceiveData(ev->ev.sysexEvent.sysexDump, size);

                SendData(0u);
            }
            break;

            case Command::RenderAudioSamples:
            {
                if (!blState.size())
                {
                    pEffect->dispatcher(pEffect, AEffectOpcodes::effSetSampleRate, 0, 0, 0, float(sampleRate));
                    pEffect->dispatcher(pEffect, AEffectOpcodes::effSetBlockSize, 0, BUFFER_SIZE, 0, 0);
                    pEffect->dispatcher(pEffect, AEffectOpcodes::effMainsChanged, 0, 1, 0, 0);
                    pEffect->dispatcher(pEffect, AEffectXOpcodes::effStartProcess, 0, 0, 0, 0);

                    size_t buffer_size = sizeof(float*) * (pEffect->numInputs + audioOutputs * 3);   // float lists (inputs + outputs)
                    buffer_size += sizeof(float) * BUFFER_SIZE;                                         // null input
                    buffer_size += sizeof(float) * BUFFER_SIZE * audioOutputs * 3;                      // outputs

                    blState.resize(buffer_size);

                    float_list_in = (float**)blState.data();
                    float_list_out = float_list_in + pEffect->numInputs;
                    float_null = (float*)(float_list_out + audioOutputs * 3);
                    float_out = float_null + BUFFER_SIZE;

                    for (unsigned i = 0; i < pEffect->numInputs; ++i)
                    {
                        float_list_in[i] = float_null;
                    }
                    for (unsigned i = 0; i < audioOutputs * 3; ++i)
                    {
                        float_list_out[i] = float_out + BUFFER_SIZE * i;
                    }

                    memset(float_null, 0, sizeof(float) * BUFFER_SIZE);

                    sample_buffer.resize((BUFFER_SIZE << 1) * audioOutputs);
                }

                if (need_idle)
                {
                    pEffect->dispatcher(pEffect, DECLARE_VST_DEPRECATED(effIdle), 0, 0, 0, 0);

                    if (!idle_started)
                    {
                        unsigned idle_run = BUFFER_SIZE * 200;

                        while (idle_run)
                        {
                            unsigned sampleFrames = min(idle_run, BUFFER_SIZE);

                            pEffect->processReplacing(pEffect, float_list_in, float_list_out, sampleFrames);

                            pEffect->dispatcher(pEffect, DECLARE_VST_DEPRECATED(effIdle), 0, 0, 0, 0);

                            idle_run -= sampleFrames;
                        }
                    }
                }

                VstEvents* events = 0;

                if (evChain)
                {
                    unsigned event_count = 0;
                    MidiEvent* ev = evChain;
                    while (ev)
                    {
                        ++event_count;
                        ev = ev->next;
                    }

                    if (event_count > 0)
                    {
                        events = (VstEvents*)malloc(sizeof(VstInt32) + sizeof(VstIntPtr) + sizeof(VstEvent*) * event_count);

                        events->numEvents = event_count;
                        events->reserved = 0;

                        ev = evChain;

                        for (size_t i = 0; ev;)
                        {
                            if (ev->port == 0)
                            {
                                events->events[i++] = (VstEvent*)&ev->ev;
                            }
                            ev = ev->next;
                        }

                        pEffect->dispatcher(pEffect, AEffectXOpcodes::effProcessEvents, 0, 0, events, 0);
                    }
                }

                if (need_idle)
                {
                    pEffect->dispatcher(pEffect, DECLARE_VST_DEPRECATED(effIdle), 0, 0, 0, 0);

                    if (!idle_started)
                    {
                        if (events)
                        {
                            pEffect->dispatcher(pEffect, AEffectXOpcodes::effProcessEvents, 0, 0, events, 0);
                        }

                        idle_started = true;
                    }
                }

                uint32_t count = ReceiveData();

                SendData(0u);

                while (count)
                {
                    unsigned sampleFrames = min(count, BUFFER_SIZE);

                    pEffect->processReplacing(pEffect, float_list_in, float_list_out, sampleFrames);

                    float* out = sample_buffer.data();

                    if (audioOutputs == 2)
                    {
                        for (size_t i = 0; i < sampleFrames; ++i)
                        {
                            out[0] = float_out[i];
                            out[1] = float_out[i + BUFFER_SIZE];
                            out += 2;
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < sampleFrames; ++i)
                        {
                            out[0] = float_out[i];
                            ++out;
                        }
                    }

                    SendData(sample_buffer.data(), sampleFrames * sizeof(float) * audioOutputs);

                    count -= sampleFrames;
                }

                if (events)
                {
                    free(events);
                }

                FreeMidiEventChain();
            }
            break;

            default:
                code = Response::CommandUnknown;
                goto exit;
                break;
        }
    }

exit:

    if (pEffect)
    {
        if (blState.size())
        {
            pEffect->dispatcher(pEffect, AEffectXOpcodes::effStopProcess, 0, 0, 0, 0);
        }

        pEffect->dispatcher(pEffect, AEffectOpcodes::effClose, 0, 0, 0, 0);
    }

    FreeMidiEventChain();

    if (vstiDll)
    {
        FreeLibrary(vstiDll);
    }

    CoUninitialize();

    if (dll_dir)
    {
        free(dll_dir);
    }

    if (argv)
    {
        LocalFree(argv);
    }

    SendData(code);

    if (null_file)
    {
        CloseHandle(null_file);

        SetStdHandle(STD_INPUT_HANDLE, pipe_in);
        SetStdHandle(STD_OUTPUT_HANDLE, pipe_out);
    }

    return code;
}