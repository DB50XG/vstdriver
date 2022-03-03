#if !defined(AFX_VIEWS_H__20020629_8D64_963C_A351_0080AD509054__INCLUDED_)
#define AFX_VIEWS_H__20020629_8D64_963C_A351_0080AD509054__INCLUDED_

#include <iostream>
#include <fstream>
#include <filesystem>
#include "utf8conv.h"
#include "../external_packages/mmddk.h"
#include "../driver/VSTDriver.h"

/// Define BASS functions as pointers
#define BASSDEF(f) (WINAPI *f)
#define LOADBASSFUNCTION(f) *((void**)&f)=GetProcAddress(bass,#f)

/// Define BASSASIO functions as pointers
#define BASSASIODEF(f) (WINAPI *f)
#define LOADBASSASIOFUNCTION(f) *((void**)&f)=GetProcAddress(bassasio,#f)

/// Define BASSWASAPI functions as pointers
#define BASSWASAPIDEF(f) (WINAPI *f)
#define LOADBASSWASAPIFUNCTION(f) *((void**)&f)=GetProcAddress(basswasapi,#f)

#include "bass.h"
#include "bassasio.h"
#include "basswasapi.h"

using namespace std;
using namespace utf8util;

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

typedef AEffect* (*PluginEntryProc) (audioMasterCallback audioMaster);
static INT_PTR CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/// for VSTDriver
extern "C" HINSTANCE hinst_vst_driver = NULL;

struct MyDLGTEMPLATE : DLGTEMPLATE
{
    WORD ext[3];
    MyDLGTEMPLATE()
    {
        memset(this, 0, sizeof(*this));
    };
};

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CView1 : public CDialogImpl<CView1>
{
    CEdit vst_info;
    CButton vst_load, vst_configure;
    CStatic vst_vendor, vst_effect, vst_product;
    TCHAR* vstiPath = NULL;
    VSTDriver* effect = NULL;
public:
    enum
    {
        IDD = IDD_MAIN
    };
    BEGIN_MSG_MAP(CView1)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialogView1)
        COMMAND_ID_HANDLER(IDC_VSTLOAD, OnButtonAdd)
        COMMAND_ID_HANDLER(IDC_VSTCONFIG, OnButtonConfig)
    END_MSG_MAP()

    CView1()
    {
        LoadVstiPath();
    }

    ~CView1()
    {
        FreeVsti();
        if (vstiPath)
        {
            delete[] vstiPath;
            vstiPath = NULL;
        }
    }

    void LoadVstiPath()
    {
        CRegKeyEx reg;
        long result = reg.Open(HKEY_CURRENT_USER, L"Software\\VSTi Driver", KEY_READ | KEY_WOW64_32KEY);
        if (result != NO_ERROR)
        {
            return;
        }

        ULONG size;
        result = reg.QueryStringValue(L"plugin", NULL, &size);
        if (result == NO_ERROR && size > 0)
        {
            vstiPath = new TCHAR[size]{};
            reg.QueryStringValue(L"plugin", vstiPath, &size);
        }

        reg.Close();
    }

    bool SaveVstiPath(TCHAR* vstiPath)
    {
        CRegKeyEx reg;
        long result = reg.Create(HKEY_CURRENT_USER, L"Software\\VSTi Driver", 0, 0, KEY_WRITE | KEY_WOW64_32KEY);
        if (result != NO_ERROR)
        {
            return false;
        }

        result = reg.SetStringValue(L"plugin", vstiPath);

        reg.Close();

        return result == NO_ERROR;
    }

    void LoadSettings()
    {
        bool hasEditor = false;

        if (vstiPath)
        {
            vst_info.SetWindowText(vstiPath);
            if (LoadVsti(vstiPath))
            {
                hasEditor = effect && effect->HasEditor();
            }
        }

        vst_configure.EnableWindow(hasEditor);
    }

    LRESULT OnButtonAdd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        LPCTSTR sFiles =
            L"VSTi (*.dll)\0*.dll\0"
            L"All Files (*.*)\0*.*\0\0";
        CFileDialog dlg(TRUE, NULL, vstiPath, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, sFiles);
        if (dlg.DoModal() == IDOK && dlg.m_szFileName)
        {
            if (!vstiPath)
            {
                vstiPath = new TCHAR[MAX_PATH]{};
            }
            bool isVstiPathChanged = _tcscmp(vstiPath, dlg.m_szFileName);

            if (isVstiPathChanged)
            {
                int vstiPathLength = lstrlen(dlg.m_szFileName);
                vstiPath = new TCHAR[vstiPathLength + 1]{};
                lstrcpy(vstiPath, dlg.m_szFileName);
            }
            else if (effect)
            {
                return 0;
            }

            if (!LoadVsti(vstiPath))
            {
                return 0;
            }

            while (!SaveVstiPath(vstiPath) && MessageBox(L"Cannot add VSTi path to the registry!", L"VSTi Settings", MB_ICONWARNING | MB_CANCELTRYCONTINUE | MB_DEFBUTTON2) == IDTRYAGAIN);

            vst_info.SetWindowText(vstiPath);

            vst_configure.EnableWindow(effect->HasEditor());
        }

        return 0;
    }

    LRESULT OnButtonConfig(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        if (effect && effect->HasEditor())
        {
            HWND m_hWnd = GetAncestor(this->m_hWnd, GA_ROOT);
            ::EnableWindow(m_hWnd, FALSE);
            effect->DisplayEditorModal();
            effect->SaveVstiSettings();
            ::EnableWindow(m_hWnd, TRUE);
        }
        return 0;
    }

    void FreeVsti()
    {
        if (effect)
        {
            effect->CloseVSTDriver();
            delete effect;
            effect = NULL;
        }
    }

    bool LoadVsti(TCHAR* vstiPath)
    {
        if (!filesystem::exists(filesystem::path(vstiPath)))
        {
            return false;
        }

        FreeVsti();

        effect = new VSTDriver();
        uint32_t* error = nullptr;
        if (!effect->OpenVSTDriver(vstiPath, &error))
        {
            delete effect;
            effect = NULL;

            if (error)
            {
                switch (*error)
                {
                    case Response::NotAVsti:
                        MessageBox(L"Failed to open VSTi!\nThe provided dll is not a VSTi!", L"VSTi Settings", MB_ICONWARNING | MB_OK);
                        break;

                    case Response::VstiIsNotAMidiSynth:
                        MessageBox(L"Failed to open VSTi!\nThis VSTi is not a midi synth!", L"VSTi Settings", MB_ICONWARNING | MB_OK);
                        break;

                    case Response::CannotGetProcAddress:
                        MessageBox(L"Failed to open VSTi!\nCannot find the main procedure of the provided dll!", L"VSTi Settings", MB_ICONWARNING | MB_OK);
                        break;

                    case Response::CannotLoadVstiDll:
                        MessageBox(L"Failed to open VSTi!\nCannot load the dll!", L"VSTi Settings", MB_ICONWARNING | MB_OK);
                        break;

                    default:
                        break;
                }
            }
            else
            {
                MessageBox(L"Failed to open VSTi!\nCheck if vsthost is present.", L"VSTi Settings", MB_ICONWARNING | MB_OK);
            }

            return false;
        }

        string conv;
        effect->GetEffectName(conv);
        wstring effectName = utf16_from_ansi(conv);
        vst_effect.SetWindowText(effectName.c_str());

        effect->GetVendorString(conv);
        wstring vendor = utf16_from_ansi(conv);
        vst_vendor.SetWindowText(vendor.c_str());

        effect->GetProductString(conv);
        wstring product = utf16_from_ansi(conv);
        vst_product.SetWindowText(product.c_str());

        return true;
    }

    LRESULT OnInitDialogView1(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        effect = NULL;

        vst_info = GetDlgItem(IDC_VSTLOADED);
        vst_load = GetDlgItem(IDC_VSTLOAD);
        vst_configure = GetDlgItem(IDC_VSTCONFIG);

        vst_effect = GetDlgItem(IDC_EFFECT);
        vst_vendor = GetDlgItem(IDC_VENDOR);
        vst_product = GetDlgItem(IDC_PRODUCT);

        vst_info.SetWindowText(L"No VSTi loaded");

        LoadSettings();

        InitializeToolTips();

        return FALSE;
    }

    void InitializeToolTips()
    {
        HWND m_hWnd = GetAncestor(this->m_hWnd, GA_ROOT);
        CreateToolTip(IDC_VSTLOAD, m_hWnd, L"Select the dll file of a VSTi to load");
        CreateToolTip(IDC_VSTLOADED, m_hWnd, L"The currently loaded dll file of a VSTi");
    }

    /// <summary>
    /// Creates a tooltip for an item in a dialog box.
    /// </summary>
    /// <param name="toolID">Identifier of an dialog box item.</param>
    /// <param name="hDlg">Window handle of the dialog box.</param>
    /// <param name="pszText">String to use as the tooltip text.</param>
    /// <returns>The handle to the tooltip.</returns>
    HWND CreateToolTip(int toolID, HWND hDlg, PTSTR pszText)
    {
        if (!toolID || !hDlg || !pszText)
        {
            return FALSE;
        }

        // Get the window of the tool.
        HWND hwndTool = GetDlgItem(toolID);

        // Create the tooltip. g_hInst is the global instance handle.
        HWND hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
                                      NULL,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      hDlg, NULL,
                                      GetModuleHandle(NULL), NULL);

        if (!hwndTool || !hwndTip)
        {
            return (HWND)NULL;
        }

        // Associate the tooltip with the tool.
        TOOLINFO toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        toolInfo.hwnd = hDlg;
        toolInfo.uId = (UINT_PTR)hwndTool;
        toolInfo.lpszText = pszText;
        SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

        return hwndTool;
    }
};

class CView3 : public CDialogImpl<CView3>
{
    CListBox playbackDevices;
    CComboBox driverMode;

    HINSTANCE   bass;			// bass handle
    HINSTANCE   bassasio;       // bassasio handle
    HINSTANCE   basswasapi;		// basswasapi handle

    bool isAsio = false;
    bool isWasapi = false;

public:
    enum
    {
        IDD = IDD_SOUND
    };

    BEGIN_MSG_MAP(CView3)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialogView3)
        COMMAND_HANDLER(IDC_COMBO1, CBN_SELCHANGE, OnCbnSelchangeCombo1)
        COMMAND_HANDLER(IDC_LIST1, LBN_SELCHANGE, OnLbnSelchangeList1)
    END_MSG_MAP()

    CView3()
    {
        LoadBass();
    }

    ~CView3()
    {
        if (bassasio)
        {
            // Stop ASIO device in case it was playing
            if (BASS_ASIO_IsStarted)
            {
                BASS_ASIO_Stop();
            }
            BASS_ASIO_Free();

            FreeLibrary(bassasio);
            bassasio = NULL;
        }

        if (basswasapi)
        {
            BASS_WASAPI_Stop(TRUE);
            BASS_WASAPI_Free();
            FreeLibrary(basswasapi);
            basswasapi = NULL;
        }

        if (bass)
        {
            BASS_Free();
            FreeLibrary(bass);
            bass = NULL;
        }
    }

    void LoadBass()
    {
        TCHAR installpath[MAX_PATH]{};
        TCHAR basspath[MAX_PATH]{};
        TCHAR basswasapipath[MAX_PATH]{};
        TCHAR bassasiopath[MAX_PATH]{};

        GetModuleFileName(hinst_vst_driver, installpath, MAX_PATH);
        PathRemoveFileSpec(installpath);

        // Load Bass
        lstrcat(basspath, installpath);
        lstrcat(basspath, L"\\bass.dll");
        bass = LoadLibrary(basspath);

        // Load Bass Asio
        lstrcat(bassasiopath, installpath);
        lstrcat(bassasiopath, L"\\bassasio.dll");
        bassasio = LoadLibrary(bassasiopath);

        // Load Bass Wasapi
        lstrcat(basswasapipath, installpath);
        lstrcat(basswasapipath, L"\\basswasapi.dll");
        basswasapi = LoadLibrary(basswasapipath);

        if (bass)
        {
            LOADBASSFUNCTION(BASS_GetDeviceInfo);
            LOADBASSFUNCTION(BASS_Free);

            if (basswasapi)
            {
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_Stop);
                LOADBASSWASAPIFUNCTION(BASS_WASAPI_Free);
                isWasapi = true;
            }
        }

        if (bassasio)
        {
            LOADBASSASIOFUNCTION(BASS_ASIO_Init);
            LOADBASSASIOFUNCTION(BASS_ASIO_GetInfo);
            LOADBASSASIOFUNCTION(BASS_ASIO_GetDevice);
            LOADBASSASIOFUNCTION(BASS_ASIO_SetDevice);
            LOADBASSASIOFUNCTION(BASS_ASIO_GetDeviceInfo);
            LOADBASSASIOFUNCTION(BASS_ASIO_Free);
            LOADBASSASIOFUNCTION(BASS_ASIO_IsStarted);
            LOADBASSASIOFUNCTION(BASS_ASIO_Stop);
            LOADBASSASIOFUNCTION(BASS_ASIO_GetRate);
            LOADBASSASIOFUNCTION(BASS_ASIO_ChannelGetInfo);

            /// Check if there is at least one ASIO capable device
            if (BASS_ASIO_Init(-1, BASS_ASIO_THREAD))
            {
                isAsio = true;
                BASS_ASIO_Free();
            }
        }
    }

    /// <summary>
    /// Load the output drivers for WASAPI driver mode
    /// </summary>
    void LoadWasapiDrivers()
    {
        CString deviceItem;
        CString selectedOutputDriver = LoadOutputDriverWasapi();

        BASS_DEVICEINFO deviceInfo{};

        for (size_t deviceId = 1; BASS_GetDeviceInfo(deviceId, &deviceInfo); ++deviceId)
        {
            deviceItem.Format(L"%s", CString(deviceInfo.name));

            if (CString(deviceInfo.driver).IsEmpty())
            {
                continue;
            }

            playbackDevices.AddString(deviceItem);

            if (selectedOutputDriver.IsEmpty() && (deviceInfo.flags & BASS_DEVICE_DEFAULT))
            {
                playbackDevices.SelectString(0, deviceItem);
            }
            else
            {
                playbackDevices.SelectString(0, selectedOutputDriver);
            }
        }
    }

    /// <summary>
    /// Load the output drivers for ASIO driver mode
    /// </summary>
    void LoadAsioDrivers()
    {
        CString deviceItem;
        CString deviceName;
        CString selectedOutputDriver = LoadOutputDriverAsio();

        BASS_ASIO_DEVICEINFO asioDeviceInfo{};

        for (size_t deviceId = 0; BASS_ASIO_Init(deviceId, BASS_ASIO_THREAD); ++deviceId)
        {
            BASS_ASIO_GetDeviceInfo(deviceId, &asioDeviceInfo);

            deviceName = CString(asioDeviceInfo.name);

            BASS_ASIO_CHANNELINFO channelInfo{};

            for (size_t channel = 0; BASS_ASIO_ChannelGetInfo(FALSE, channel, &channelInfo); ++channel)
            {
                deviceItem.Format(L"%s %s", deviceName, CString(channelInfo.name));
                if (playbackDevices.FindStringExact(0, deviceItem) == LB_ERR)
                {
                    playbackDevices.AddString(deviceItem);
                    if ((selectedOutputDriver.IsEmpty() && channel == 0) || deviceItem.CompareNoCase(selectedOutputDriver) == 0)
                    {
                        /// Select the first ASIO channel if there is no user selection yet
                        playbackDevices.SelectString(0, deviceItem);
                    }
                }
            }

            BASS_ASIO_Free();
        }
    }

    /// <summary>
    /// Save Output Driver settings
    /// </summary>
    bool SaveOutputDriver(CString valueName, CString value)
    {
        CRegKeyEx reg;

        /// Create the Output Driver registry subkey
        long result = reg.Create(HKEY_CURRENT_USER, L"Software\\VSTi Driver\\Output Driver", 0, 0, KEY_WRITE | KEY_WOW64_32KEY);

        if (result != NO_ERROR)
        {
            return false;
        }

        /// Save the OutputDriver settings
        result = reg.SetStringValue(valueName, value);

        reg.Close();

        return result == NO_ERROR;
    }

    bool SaveOutputDriverAsio(CString outputDriver)
    {
        return SaveOutputDriver(L"ASIO", outputDriver);
    }

    bool SaveOutputDriverWasapi(CString outputDriver)
    {
        return SaveOutputDriver(L"WASAPI", outputDriver);
    }

    bool SaveDriverMode(CString driverMode)
    {
        return SaveOutputDriver(L"Mode", driverMode);
    }

    CString LoadOutputDriver(CString valueName)
    {
        CRegKeyEx reg;
        CString value;

        long result = reg.Open(HKEY_CURRENT_USER, L"Software\\VSTi Driver\\Output Driver", KEY_READ | KEY_WOW64_32KEY);
        if (result != NO_ERROR)
        {
            return value;
        }

        ULONG size;

        result = reg.QueryStringValue(valueName, NULL, &size);
        if (result == NO_ERROR && size > 0)
        {
            reg.QueryStringValue(valueName, value.GetBuffer(size), &size);
            value.ReleaseBuffer();
        }

        reg.Close();

        return value;
    }

    CString LoadOutputDriverAsio()
    {
        return LoadOutputDriver(L"ASIO");
    }

    CString LoadOutputDriverWasapi()
    {
        return LoadOutputDriver(L"WASAPI");
    }

    CString LoadDriverMode()
    {
        return LoadOutputDriver(L"Mode");
    }

    LRESULT OnInitDialogView3(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        playbackDevices = GetDlgItem(IDC_LIST1);

        driverMode = GetDlgItem(IDC_COMBO1);

        driverMode.AddString(L"WASAPI");
        driverMode.SelectString(0, L"WASAPI");

        CString selectedDriverMode = LoadDriverMode();

        if (selectedDriverMode.IsEmpty())
        {
            selectedDriverMode = L"ASIO";
        }

        if (isAsio)
        {
            driverMode.AddString(L"ASIO");
            driverMode.EnableWindow(true);

            if (selectedDriverMode.CompareNoCase(L"ASIO") == 0)
            {
                driverMode.SelectString(0, L"ASIO");
                LoadAsioDrivers();
            }
            else
            {
                LoadWasapiDrivers();
            }
        }
        else
        {
            LoadWasapiDrivers();
        }

        return 0;
    }

    LRESULT OnCbnSelchangeCombo1(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        CString newDriverMode;
        CString selectedDriverMode;

        int index = driverMode.GetCurSel();
        int length = driverMode.GetLBTextLen(index);

        if (driverMode.GetLBText(index, newDriverMode.GetBuffer(length)))
        {
            newDriverMode.ReleaseBuffer();

            selectedDriverMode = LoadDriverMode();

            if (selectedDriverMode.CompareNoCase(newDriverMode) != 0)
            {
                playbackDevices.ResetContent();

                if (newDriverMode.CompareNoCase(L"ASIO") == 0)
                {
                    LoadAsioDrivers();
                }
                else
                {
                    LoadWasapiDrivers();
                }

                SaveDriverMode(newDriverMode);
            }
        }

        return 0;
    }

    LRESULT OnLbnSelchangeList1(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        CString selectedOutputDriver;
        CString selectedDriverMode;

        int index = playbackDevices.GetCurSel();
        int length = playbackDevices.GetTextLen(index);

        if (playbackDevices.GetText(index, selectedOutputDriver.GetBuffer(length)))
        {
            selectedOutputDriver.ReleaseBuffer();

            int index = driverMode.GetCurSel();
            int length = driverMode.GetLBTextLen(index);

            if (driverMode.GetLBText(index, selectedDriverMode.GetBuffer(length)))
            {
                selectedDriverMode.ReleaseBuffer();

                if (selectedDriverMode.CompareNoCase(L"ASIO") == 0)
                {
                    SaveOutputDriverAsio(selectedOutputDriver);
                }
                else
                {
                    SaveOutputDriverWasapi(selectedOutputDriver);
                }
            }
        }

        return 0;
    }
};

class CView2 : public CDialogImpl<CView2>
{
    CComboBox synthlist;
    CButton apply;

    typedef DWORD(STDAPICALLTYPE* pmodMessage)(UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

public:
    enum
    {
        IDD = IDD_ADVANCED
    };
    BEGIN_MSG_MAP(CView1)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialogView2)
        COMMAND_ID_HANDLER(IDC_SNAPPLY, OnButtonApply)
    END_MSG_MAP()

    LRESULT OnInitDialogView2(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        synthlist = GetDlgItem(IDC_SYNTHLIST);
        apply = GetDlgItem(IDC_SNAPPLY);
        load_midisynths_mapper();
        return TRUE;
    }

    LRESULT OnButtonApply(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        set_midisynth_mapper();
        return 0;

    }

    /* These only work on Windows 6.1 and older */
    void set_midisynth_mapper()
    {
        CRegKeyEx reg;
        CRegKeyEx subkey;
        CString device_name;
        long lRet;
        int selection = synthlist.GetCurSel();
        int n = synthlist.GetLBTextLen(selection);
        synthlist.GetLBText(selection, device_name.GetBuffer(n));
        device_name.ReleaseBuffer(n);
        lRet = reg.Create(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia", REG_NONE, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_32KEY);
        lRet = reg.DeleteSubKey(L"MIDIMap");
        lRet = subkey.Create(reg, L"MIDIMap", REG_NONE, REG_OPTION_NON_VOLATILE, KEY_WRITE);
        lRet = subkey.SetStringValue(L"szPname", device_name);
        if (lRet == ERROR_SUCCESS)
        {
            MessageBox(L"MIDI synth set!", L"Notice.", MB_ICONINFORMATION);
        }
        else
        {
            MessageBox(L"Can't set MIDI registry key", L"Damn!", MB_ICONSTOP);
        }
        subkey.Close();
        reg.Close();
    }

    void load_midisynths_mapper()
    {
        LONG lResult;
        CRegKeyEx reg;
        CString device_name;
        ULONG size = 128;
        lResult = reg.Create(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap", REG_NONE, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WOW64_32KEY);
        reg.QueryStringValue(L"szPname", device_name.GetBuffer(size), &size);
        reg.Close();
        device_name.ReleaseBuffer(size);
        int device_count = midiOutGetNumDevs();
        for (int i = 0; i < device_count; ++i)
        {
            MIDIOUTCAPS Caps;
            ZeroMemory(&Caps, sizeof(Caps));
            MMRESULT Error = midiOutGetDevCaps(i, &Caps, sizeof(Caps));
            if (Error != MMSYSERR_NOERROR)
                continue;
            synthlist.AddString(Caps.szPname);
        }
        int index = 0;
        index = synthlist.FindStringExact(-1, device_name);
        if (index == CB_ERR) index = 0;
        synthlist.SetCurSel(index);
    }
};


#endif // !defined(AFX_VIEWS_H__20020629_8D64_963C_A351_0080AD509054__INCLUDED_)