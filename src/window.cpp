#include "window.h"
#include "interop.hpp"
#include "openre.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace openre::window
{
    // 0x00508DF0
#ifdef _WIN32
    static INT_PTR __cdecl
    show_dialog(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWnd, DLGPROC lpDialogFunc, LPARAM dwInitParam)
    {
        if (IsIconic(hWnd))
            ShowWindow(hWnd, SW_RESTORE);
        return DialogBoxParamA(hInstance, lpTemplateName, hWnd, lpDialogFunc, dwInitParam);
    }
#else
    // Non-Windows stub so the hook registration below still compiles.
    static intptr_t show_dialog(void*, const char*, void*, void*, intptr_t)
    {
        return -1;
    }
#endif

    // 0x00508E30
#ifdef _WIN32
    static BOOL __stdcall dialog_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_INITDIALOG)
        {
            RECT rcDlg, rcParent;

            GetWindowRect(hDlg, &rcDlg);
            auto hParent = GetParent(hDlg);

            if (hParent)
            {
                GetWindowRect(hParent, &rcParent);

                auto x = (rcParent.right + rcDlg.left - rcDlg.right - rcParent.left) / 2;
                auto y = (rcParent.bottom + rcDlg.top - rcDlg.bottom - rcParent.top) / 2;

                GetWindowRect(hParent, &rcDlg);
                x += rcDlg.left;
                y += rcDlg.top;

                SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }

            auto hCtrl = GetDlgItem(hDlg, 1);
            if (hCtrl)
                SetFocus(hCtrl);
            return hCtrl == nullptr;
        }

        if (msg == WM_COMMAND)
        {
            auto id = LOWORD(wParam);
            if (id && (id <= 2 || id == 1019))
            {
                EndDialog(hDlg, id);
                return TRUE;
            }
        }

        return FALSE;
    }
#else
    // Non-Windows stub so the hook registration below still compiles.
    static int dialog_proc(void*, unsigned int, uintptr_t, intptr_t)
    {
        return 0;
    }
#endif

    void window_init_hooks()
    {
        interop::writeJmp(0x00508DF0, &show_dialog);
        interop::writeJmp(0x00508E30, &dialog_proc);
    }
}
