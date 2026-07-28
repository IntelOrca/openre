#include "window.h"
#include "interop.hpp"
#include "openre.h"

#include <cstring>
#include <windows.h>

namespace openre::window
{
    // 0x00508DF0
    static INT_PTR __cdecl
    show_dialog(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWnd, DLGPROC lpDialogFunc, LPARAM dwInitParam)
    {
        if (IsIconic(hWnd))
            ShowWindow(hWnd, SW_RESTORE);
        return DialogBoxParamA(hInstance, lpTemplateName, hWnd, lpDialogFunc, dwInitParam);
    }

    // 0x00508E30
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

    void window_init_hooks()
    {
        interop::writeJmp(0x00508DF0, &show_dialog);
        interop::writeJmp(0x00508E30, &dialog_proc);
    }
}
