/*
 *    AutoComplete interfaces implementation.
 *
 *    Copyright 2004    Maxime Belleng� <maxime.bellenge@laposte.net>
 *    Copyright 2009  Andrew Hill
 *    Copyright 2020-2021 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
  Implemented:
  - ACO_AUTOAPPEND style
  - ACO_AUTOSUGGEST style
  - ACO_UPDOWNKEYDROPSLIST style

  - Handle pwzsRegKeyPath and pwszQuickComplete in Init

  TODO:
  - implement ACO_SEARCH style
  - implement ACO_FILTERPREFIXES style
  - implement ACO_USETAB style
  - implement ACO_RTLREADING style

 */

#include "precomp.h"

static const WCHAR autocomplete_propertyW[] = {'W','i','n','e',' ','A','u','t','o',
                                               'c','o','m','p','l','e','t','e',' ',
                                               'c','o','n','t','r','o','l',0};

/**************************************************************************
 *  IAutoComplete_Constructor
 */
CAutoComplete::CAutoComplete()
{
    m_enabled = TRUE;
    m_initialized = FALSE;
    m_options = ACO_AUTOAPPEND;
    m_wpOrigEditProc = NULL;
    m_hwndListBox = NULL;
    m_txtbackup = NULL;
    m_quickComplete = NULL;
    m_hwndEdit = NULL;
    m_wpOrigLBoxProc = NULL;
}

/**************************************************************************
 *  IAutoComplete_Destructor
 */
CAutoComplete::~CAutoComplete()
{
    TRACE(" destroying IAutoComplete(%p)\n", this);
    HeapFree(GetProcessHeap(), 0, m_quickComplete);
    HeapFree(GetProcessHeap(), 0, m_txtbackup);
    if (m_wpOrigEditProc)
    {
        SetWindowLongPtrW(m_hwndEdit, GWLP_WNDPROC, (LONG_PTR)m_wpOrigEditProc);
        RemovePropW(m_hwndEdit, autocomplete_propertyW);
    }
    if (m_hwndListBox)
        DestroyWindow(m_hwndListBox);
}

/******************************************************************************
 * IAutoComplete_fnEnable
 */
HRESULT WINAPI CAutoComplete::Enable(BOOL fEnable)
{
    HRESULT hr = S_OK;

    TRACE("(%p)->(%s)\n", this, (fEnable) ? "true" : "false");

    m_enabled = fEnable;

    return hr;
}

/******************************************************************************
 * create_listbox
 */
void CAutoComplete::CreateListbox()
{
    HWND hwndParent = GetParent(m_hwndEdit);

    /* FIXME : The listbox should be resizable with the mouse. WS_THICKFRAME looks ugly */
    m_hwndListBox = CreateWindowExW(0, WC_LISTBOXW, NULL,
                                    WS_BORDER | WS_CHILD | WS_VSCROLL | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    hwndParent, NULL,
                                    (HINSTANCE)GetWindowLongPtrW(hwndParent, GWLP_HINSTANCE), NULL);

    if (m_hwndListBox)
    {
        m_wpOrigLBoxProc = (WNDPROC)SetWindowLongPtrW(m_hwndListBox, GWLP_WNDPROC, (LONG_PTR)ACLBoxSubclassProc);
        SetWindowLongPtrW(m_hwndListBox, GWLP_USERDATA, (LONG_PTR)this);
    }
}


/******************************************************************************
 * IAutoComplete_fnInit
 */
HRESULT WINAPI CAutoComplete::Init(HWND hwndEdit, IUnknown *punkACL, LPCOLESTR pwzsRegKeyPath, LPCOLESTR pwszQuickComplete)
{
    TRACE("(%p)->(0x%08lx, %p, %s, %s)\n",
      this, hwndEdit, punkACL, debugstr_w(pwzsRegKeyPath), debugstr_w(pwszQuickComplete));

    if (m_options & ACO_AUTOSUGGEST)
        TRACE(" ACO_AUTOSUGGEST\n");
    if (m_options & ACO_AUTOAPPEND)
        TRACE(" ACO_AUTOAPPEND\n");
    if (m_options & ACO_SEARCH)
        FIXME(" ACO_SEARCH not supported\n");
    if (m_options & ACO_FILTERPREFIXES)
        FIXME(" ACO_FILTERPREFIXES not supported\n");
    if (m_options & ACO_USETAB)
        FIXME(" ACO_USETAB not supported\n");
    if (m_options & ACO_UPDOWNKEYDROPSLIST)
        TRACE(" ACO_UPDOWNKEYDROPSLIST\n");
    if (m_options & ACO_RTLREADING)
        FIXME(" ACO_RTLREADING not supported\n");

    if (!hwndEdit || !punkACL)
        return E_INVALIDARG;

    if (m_initialized)
    {
        WARN("Autocompletion object is already initialized\n");
        /* This->hwndEdit is set to NULL when the edit window is destroyed. */
        return m_hwndEdit ? E_FAIL : E_UNEXPECTED;
    }

    if (!SUCCEEDED(punkACL->QueryInterface(IID_PPV_ARG(IEnumString, &m_enumstr))))
    {
        TRACE("No IEnumString interface\n");
        return  E_NOINTERFACE;
    }

    m_hwndEdit = hwndEdit;
    m_initialized = TRUE;

    /* Keep at least one reference to the object until the edit window is destroyed. */
    AddRef();

    /* If another AutoComplete object was previously assigned to this edit control,
       release it but keep the same callback on the control, to avoid an infinite
       recursive loop in ACEditSubclassProc while the property is set to this object */
    CAutoComplete *prev = static_cast<CAutoComplete *>(GetPropW(m_hwndEdit, autocomplete_propertyW));

    if (prev && prev->m_initialized)
    {
        m_wpOrigEditProc = prev->m_wpOrigEditProc;
        SetPropW(m_hwndEdit, autocomplete_propertyW, this);
        prev->m_wpOrigEditProc = NULL;
        prev->Release();
    }
    else
    {
        SetPropW(m_hwndEdit, autocomplete_propertyW, (HANDLE)this);
        m_wpOrigEditProc = (WNDPROC)SetWindowLongPtrW(m_hwndEdit, GWLP_WNDPROC, (LONG_PTR)ACEditSubclassProc);
    }

    if (m_options & ACO_AUTOSUGGEST)
    {
        CreateListbox();
    }

    if (pwzsRegKeyPath)
    {
        WCHAR *key;
        WCHAR result[MAX_PATH];
        WCHAR *value;
        HKEY hKey = 0;
        LONG res;
        LONG len;

        /* pwszRegKeyPath contains the key as well as the value, so we split */
        key = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (wcslen(pwzsRegKeyPath) + 1) * sizeof(WCHAR));

        if (key)
        {
            wcscpy(key, pwzsRegKeyPath);
            value = const_cast<WCHAR *>(wcsrchr(key, '\\'));

            if (value)
            {
                *value = 0;
                value++;
                /* Now value contains the value and buffer the key */
                res = RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey);
        
                if (res != ERROR_SUCCESS)
                {
                    /* if the key is not found, MSDN states we must seek in HKEY_LOCAL_MACHINE */
                    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hKey);
                }
        
                if (res == ERROR_SUCCESS)
                {
                    len = sizeof(result);
                    res = RegQueryValueW(hKey, value, result, &len);
                    if (res == ERROR_SUCCESS)
                    {
                        m_quickComplete = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(WCHAR));
                        wcscpy(m_quickComplete, result);
                    }
                    RegCloseKey(hKey);
                }
            }

            HeapFree(GetProcessHeap(), 0, key);
        }
        else
        {
            TRACE("HeapAlloc Failed when trying to alloca %d bytes\n", (wcslen(pwzsRegKeyPath) + 1) * sizeof(WCHAR));
            return S_FALSE;
        }
    }

    if ((pwszQuickComplete) && (!m_quickComplete))
    {
        m_quickComplete = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (wcslen(pwszQuickComplete) + 1) * sizeof(WCHAR));

        if (m_quickComplete)
        {
            wcscpy(m_quickComplete, pwszQuickComplete);
        }
        else
        {
            TRACE("HeapAlloc Failed when trying to alloca %d bytes\n", (wcslen(pwszQuickComplete) + 1) * sizeof(WCHAR));
            return S_FALSE;
        }
    }

    return S_OK;
}

/**************************************************************************
 *  IAutoComplete_fnGetOptions
 */
HRESULT WINAPI CAutoComplete::GetOptions(DWORD *pdwFlag)
{
    HRESULT hr = S_OK;

    TRACE("(%p) -> (%p)\n", this, pdwFlag);

    *pdwFlag = m_options;

    return hr;
}

/**************************************************************************
 *  IAutoComplete_fnSetOptions
 */
HRESULT WINAPI CAutoComplete::SetOptions(DWORD dwFlag)
{
    HRESULT hr = S_OK;

    TRACE("(%p) -> (0x%x)\n", this, dwFlag);

    m_options = (AUTOCOMPLETEOPTIONS)dwFlag;

    if ((m_options & ACO_AUTOSUGGEST) && m_hwndEdit && !m_hwndListBox)
        CreateListbox();

    return hr;
}

/* Edit_BackWord --- Delete previous word in text box */
static void Edit_BackWord(HWND hwndEdit)
{
    INT iStart, iEnd;
    iStart = iEnd = 0;
    SendMessageW(hwndEdit, EM_GETSEL, (WPARAM)&iStart, (LPARAM)&iEnd);

    if (iStart != iEnd || iStart < 0)
        return;

    size_t cchText = GetWindowTextLengthW(hwndEdit);
    if (cchText < (size_t)iStart || (INT)cchText <= 0)
        return;

    CComHeapPtr<WCHAR> pszText;
    if (!pszText.Allocate(cchText + 1))
        return;

    if (GetWindowTextW(hwndEdit, pszText, cchText + 1) <= 0)
        return;

    WORD types[2];
    for (--iStart; 0 < iStart; --iStart)
    {
        GetStringTypeW(CT_CTYPE1, &pszText[iStart - 1], 2, types);
        if (((types[0] & C1_PUNCT) && !(types[1] & C1_SPACE)) ||
            ((types[0] & C1_SPACE) && (types[1] & (C1_ALPHA | C1_DIGIT))))
        {
            SendMessageW(hwndEdit, EM_SETSEL, iStart, iEnd);
            SendMessageW(hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
            return;
        }
    }

    if (iStart == 0)
    {
        SendMessageW(hwndEdit, EM_SETSEL, iStart, iEnd);
        SendMessageW(hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    }
}

/*
  Window procedure for autocompletion
 */
LRESULT APIENTRY CAutoComplete::ACEditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CAutoComplete *pThis = static_cast<CAutoComplete *>(GetPropW(hwnd, autocomplete_propertyW));
    HRESULT hr;
    WCHAR hwndText[255];
    WCHAR *hwndQCText;
    RECT r;
    BOOL control, filled, displayall = FALSE;
    int cpt, height, sel;
    ULONG fetched;

    if (!pThis->m_enabled)
    {
        return CallWindowProcW(pThis->m_wpOrigEditProc, hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg)
    {
        case CB_SHOWDROPDOWN:
        {
            ShowWindow(pThis->m_hwndListBox, SW_HIDE);
        }; break;

        case WM_KILLFOCUS:
        {
            if ((pThis->m_options & ACO_AUTOSUGGEST) && ((HWND)wParam != pThis->m_hwndListBox))
            {
                ShowWindow(pThis->m_hwndListBox, SW_HIDE);
            }
            return CallWindowProcW(pThis->m_wpOrigEditProc, hwnd, uMsg, wParam, lParam);
        }; break;

        case WM_KEYUP:
        {
            GetWindowTextW(hwnd, (LPWSTR)hwndText, 255);

            switch(wParam)
            {
                case VK_RETURN:
                {
                    /* If quickComplete is set and control is pressed, replace the string */
                    control = GetKeyState(VK_CONTROL) & 0x8000;
                    if (control && pThis->m_quickComplete)
                    {
                        hwndQCText = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           (wcslen(pThis->m_quickComplete)+wcslen(hwndText))*sizeof(WCHAR));
                        sel = swprintf(hwndQCText, pThis->m_quickComplete, hwndText);
                        SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)hwndQCText);
                        SendMessageW(hwnd, EM_SETSEL, 0, sel);
                        HeapFree(GetProcessHeap(), 0, hwndQCText);
                    }

                    ShowWindow(pThis->m_hwndListBox, SW_HIDE);
                    return 0;
                }; break;

                case VK_LEFT:
                case VK_RIGHT:
                {
                    return 0;
                }; break;

                case VK_UP:
                case VK_DOWN:
                {
                    /* Two cases here :
                       - if the listbox is not visible, displays it
                       with all the entries if the style ACO_UPDOWNKEYDROPSLIST
                       is present but does not select anything.
                       - if the listbox is visible, change the selection
                    */
                    if ( (pThis->m_options & (ACO_AUTOSUGGEST | ACO_UPDOWNKEYDROPSLIST))
                     && (!IsWindowVisible(pThis->m_hwndListBox) && (! *hwndText)) )
                    {
                        /* We must display all the entries */
                        displayall = TRUE;
                    }
                    else
                    {
                        if (IsWindowVisible(pThis->m_hwndListBox))
                        {
                            int count;

                            count = SendMessageW(pThis->m_hwndListBox, LB_GETCOUNT, 0, 0);
                            /* Change the selection */
                            sel = SendMessageW(pThis->m_hwndListBox, LB_GETCURSEL, 0, 0);
                            if (wParam == VK_UP)
                                sel = ((sel-1) < 0) ? count-1 : sel-1;
                            else
                                sel = ((sel+1) >= count) ? -1 : sel+1;
                            
                            SendMessageW(pThis->m_hwndListBox, LB_SETCURSEL, sel, 0);
                            
                            if (sel != -1)
                            {
                                WCHAR *msg;
                                int len;

                                len = SendMessageW(pThis->m_hwndListBox, LB_GETTEXTLEN, sel, (LPARAM)NULL);
                                msg = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (len + 1) * sizeof(WCHAR));
                                
                                if (msg)
                                {
                                    SendMessageW(pThis->m_hwndListBox, LB_GETTEXT, sel, (LPARAM)msg);
                                    SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)msg);
                                    SendMessageW(hwnd, EM_SETSEL, wcslen(msg), wcslen(msg));
                                
                                    HeapFree(GetProcessHeap(), 0, msg);
                                }
                                else
                                {
                                    TRACE("HeapAlloc failed to allocate %d bytes\n", (len + 1) * sizeof(WCHAR));
                                }
                            }
                            else
                            {
                                SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)pThis->m_txtbackup);
                                SendMessageW(hwnd, EM_SETSEL, wcslen(pThis->m_txtbackup), wcslen(pThis->m_txtbackup));
                            }
                        }
                        return 0;
                    }
                }; break;
                
                case VK_BACK:
                {
                    if (GetKeyState(VK_CONTROL) < 0) // Ctrl+Backspace
                    {
                        Edit_BackWord(hwnd);
                        return 0;
                    }
                }
                // FALL THROUGH
                case VK_DELETE:
                {
                    if ((! *hwndText) && (pThis->m_options & ACO_AUTOSUGGEST))
                    {
                        ShowWindow(pThis->m_hwndListBox, SW_HIDE);
                        return CallWindowProcW(pThis->m_wpOrigEditProc, hwnd, uMsg, wParam, lParam);
                    }
                    
                    if (pThis->m_options & ACO_AUTOAPPEND)
                    {
                        DWORD b;
                        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&b, (LPARAM)NULL);
                        if (b>1)
                        {
                            hwndText[b-1] = '\0';
                        }
                        else
                        {
                            hwndText[0] = '\0';
                            SetWindowTextW(hwnd, hwndText);
                        }
                    }
                }; break;
                
                default:
                    ;
            }

            SendMessageW(pThis->m_hwndListBox, LB_RESETCONTENT, 0, 0);

            HeapFree(GetProcessHeap(), 0, pThis->m_txtbackup);

            pThis->m_txtbackup = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (wcslen(hwndText)+1)*sizeof(WCHAR));

            if (pThis->m_txtbackup)
            {
                wcscpy(pThis->m_txtbackup, hwndText);
            }
            else
            {
                TRACE("HeapAlloc failed to allocate %d bytes\n", (wcslen(hwndText)+1)*sizeof(WCHAR));
            }

            /* Returns if there is no text to search and we doesn't want to display all the entries */
            if ((!displayall) && (! *hwndText) )
                break;

            pThis->m_enumstr->Reset();
            filled = FALSE;
            size_t curlen = wcslen(hwndText);

            for(cpt = 0;;)
            {
                CComHeapPtr<OLECHAR> strs;
                hr = pThis->m_enumstr->Next(1, &strs, &fetched);
                if (hr != S_OK)
                    break;

                if (!_wcsnicmp(hwndText, strs, curlen))
                {

                    if (pThis->m_options & ACO_AUTOAPPEND && *hwndText)
                    {
                        CComBSTR str((PCWSTR)strs);
                        memcpy(str.m_str, hwndText, curlen * sizeof(WCHAR));
                        SetWindowTextW(hwnd, str);
                        SendMessageW(hwnd, EM_SETSEL, curlen, str.Length());
                        if (!(pThis->m_options & ACO_AUTOSUGGEST))
                            break;
                    }

                    if (pThis->m_options & ACO_AUTOSUGGEST)
                    {
                        SendMessageW(pThis->m_hwndListBox, LB_ADDSTRING, 0, (LPARAM)(LPOLESTR)strs);
                        filled = TRUE;
                        cpt++;
                    }
                }
            }

            if (pThis->m_options & ACO_AUTOSUGGEST)
            {
                if (filled)
                {
                    height = SendMessageW(pThis->m_hwndListBox, LB_GETITEMHEIGHT, 0, 0);
                    SendMessageW(pThis->m_hwndListBox, LB_CARETOFF, 0, 0);
                    GetWindowRect(hwnd, &r);
                    SetParent(pThis->m_hwndListBox, HWND_DESKTOP);
                    /* It seems that Windows XP displays 7 lines at most
                       and otherwise displays a vertical scroll bar */
                    SetWindowPos(pThis->m_hwndListBox, HWND_TOP,
                         r.left, r.bottom + 1, r.right - r.left, min(height * 7, height * (cpt + 1)),
                         SWP_SHOWWINDOW );
                }
                else
                {
                    ShowWindow(pThis->m_hwndListBox, SW_HIDE);
                }
            }

        }; break;

        case WM_DESTROY:
        {
            /* Release our reference that we had since ->Init() */
            pThis->Release();
            return 0;
        }


        default:
        {
            return CallWindowProcW(pThis->m_wpOrigEditProc, hwnd, uMsg, wParam, lParam);
        }

    }

    return 0;
}

LRESULT APIENTRY CAutoComplete::ACLBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CAutoComplete *pThis = reinterpret_cast<CAutoComplete *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    WCHAR *msg;
    int sel, len;

    switch (uMsg)
    {
        case WM_MOUSEMOVE:
        {
            sel = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            SendMessageW(hwnd, LB_SETCURSEL, (WPARAM)sel, (LPARAM)0);
        }; break;
        
        case WM_LBUTTONDOWN:
        {
            sel = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            
            if (sel < 0)
                break;
            
            len = SendMessageW(pThis->m_hwndListBox, LB_GETTEXTLEN, sel, 0);
            msg = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (len + 1) * sizeof(WCHAR));
            
            if (msg)
            {
                SendMessageW(hwnd, LB_GETTEXT, sel, (LPARAM)msg);
                SendMessageW(pThis->m_hwndEdit, WM_SETTEXT, 0, (LPARAM)msg);
                SendMessageW(pThis->m_hwndEdit, EM_SETSEL, 0, wcslen(msg));
                ShowWindow(hwnd, SW_HIDE);
            
                HeapFree(GetProcessHeap(), 0, msg);
            }
            else
            {
                TRACE("HeapAlloc failed to allocate %d bytes\n", (len + 1) * sizeof(WCHAR));
            }
           
        }; break;
        
        default:
            return CallWindowProcW(pThis->m_wpOrigLBoxProc, hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

/**************************************************************************
 *  IAutoCompleteDropDown
 */
HRESULT STDMETHODCALLTYPE CAutoComplete::GetDropDownStatus(DWORD *pdwFlags, LPWSTR *ppwszString)
{
    BOOL dropped = IsWindowVisible(m_hwndListBox);

    if (pdwFlags)
        *pdwFlags = (dropped ? ACDD_VISIBLE : 0);

    if (ppwszString)
    {
        *ppwszString = NULL;

        if (dropped)
        {
            int sel = SendMessageW(m_hwndListBox, LB_GETCURSEL, 0, 0);
            if (sel >= 0)
            {
                DWORD len = SendMessageW(m_hwndListBox, LB_GETTEXTLEN, sel, 0);
                *ppwszString = (LPWSTR)CoTaskMemAlloc((len+1)*sizeof(WCHAR));
                SendMessageW(m_hwndListBox, LB_GETTEXT, sel, (LPARAM)*ppwszString);
            }
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CAutoComplete::ResetEnumerator()
{
    FIXME("(%p): stub\n", this);
    return E_NOTIMPL;
}

/**************************************************************************
 *  IEnumString
 */
HRESULT STDMETHODCALLTYPE CAutoComplete::Next(ULONG celt, LPOLESTR *rgelt, ULONG *pceltFetched)
{
    FIXME("(%p, %d, %p, %p): stub\n", this, celt, rgelt, pceltFetched);
    *pceltFetched = 0;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CAutoComplete::Skip(ULONG celt)
{
    FIXME("(%p, %d): stub\n", this, celt);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CAutoComplete::Reset()
{
    FIXME("(%p): stub\n", this);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CAutoComplete::Clone(IEnumString **ppOut)
{
    FIXME("(%p, %p): stub\n", this, ppOut);
    *ppOut = NULL;
    return E_NOTIMPL;
}
