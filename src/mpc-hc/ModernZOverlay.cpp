/*
 * ModernZ video control overlay.
 *
 * This file is part of MPC-HC.
 * Licensed under the GNU General Public License version 3 or later.
 */

#include "stdafx.h"
#include "ModernZOverlay.h"

#include "MainFrm.h"
#include "PlayerSeekBar.h"
#include "PlayerToolBar.h"
#include "mplayerc.h"

#include <algorithm>
#include <cstring>

IMPLEMENT_DYNAMIC(CModernZOverlay, CWnd)

BEGIN_MESSAGE_MAP(CModernZOverlay, CWnd)
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_RBUTTONUP()
    ON_WM_MOUSEWHEEL()
    ON_WM_TIMER()
    ON_WM_CAPTURECHANGED()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

CModernZOverlay::CModernZOverlay(CMainFrame* pMainFrame, CPlayerSeekBar* pSeekBar, CPlayerToolBar* pToolBar)
    : m_pMainFrame(pMainFrame)
    , m_pSeekBar(pSeekBar)
    , m_pToolBar(pToolBar)
{
}

CModernZOverlay::~CModernZOverlay()
{
    if (m_hWnd) {
        DestroyWindow();
    }
}

bool CModernZOverlay::CreateOverlayWindow(CWnd* parent)
{
    if (!parent || !::IsWindow(parent->GetSafeHwnd())) {
        return false;
    }

    const CString className = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    if (!CreateEx(WS_EX_LAYERED, className, nullptr, WS_CHILD | WS_CLIPSIBLINGS,
                  CRect(0, 0, 0, 0), parent, 0)) {
        return false;
    }

    m_pVideoParent = parent;
    m_available = true;
    return true;
}

bool CModernZOverlay::Bind(CWnd* pVideoParent)
{
    if (!pVideoParent || !::IsWindow(pVideoParent->GetSafeHwnd())) {
        HideOverlay();
        m_available = false;
        return false;
    }

    if (m_pVideoParent == pVideoParent && ::IsWindow(m_hWnd)) {
        return true;
    }

    const bool wasVisible = IsWindowVisible() != FALSE;
    if (::IsWindow(m_hWnd)) {
        DestroyWindow();
    }

    if (!CreateOverlayWindow(pVideoParent)) {
        m_available = false;
        return false;
    }

    if (!m_videoRect.IsRectEmpty()) {
        SetBounds(m_videoRect);
    }
    if (wasVisible || !m_autoHide) {
        ShowOverlay();
    }
    return true;
}

bool CModernZOverlay::SetBounds(const CRect& videoRect)
{
    m_videoRect = videoRect;
    if (!::IsWindow(m_hWnd) || videoRect.IsRectEmpty()) {
        HideOverlay();
        return false;
    }

    const int height = std::min(videoRect.Height(), m_pMainFrame->m_dpi.ScaleY(76));
    if (height <= 0 || videoRect.Width() <= m_pMainFrame->m_dpi.ScaleX(80)) {
        HideOverlay();
        return false;
    }

    CRect bounds(videoRect.left, videoRect.bottom - height, videoRect.right, videoRect.bottom);
    m_screenRect = bounds;
    m_pVideoParent->ClientToScreen(&m_screenRect);

    SetWindowPos(&wndTop, bounds.left, bounds.top, bounds.Width(), bounds.Height(),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (!Render()) {
        m_available = false;
        HideOverlay();
        return false;
    }

    m_available = true;
    if (!m_autoHide) {
        ShowOverlay();
    }
    return true;
}

void CModernZOverlay::SetAutoHide(bool autoHide)
{
    m_autoHide = autoHide;
    if (!m_autoHide) {
        KillTimer(TIMER_HIDE);
        ShowOverlay();
    } else {
        StartHideTimer();
    }
}

void CModernZOverlay::NotifyVideoMouseMove(const CPoint& screenPoint)
{
    if (::IsWindow(m_hWnd)) {
        GetWindowRect(&m_screenRect);
    }
    if (!m_autoHide || !m_available || m_screenRect.IsRectEmpty()) {
        return;
    }

    if (m_screenRect.PtInRect(screenPoint)) {
        ShowOverlay();
    } else if (IsWindowVisible()) {
        StartHideTimer();
    }
}

void CModernZOverlay::NotifyVideoMouseLeave()
{
    StartHideTimer();
}

void CModernZOverlay::Refresh()
{
    if (m_available && IsWindowVisible()) {
        Render();
    }
}

bool CModernZOverlay::IsReady() const
{
    return m_available && ::IsWindow(m_hWnd) && !m_videoRect.IsRectEmpty();
}

void CModernZOverlay::ShowOverlay()
{
    if (::IsWindow(m_hWnd)) {
        GetWindowRect(&m_screenRect);
    }
    if (!m_available || !::IsWindow(m_hWnd) || m_videoRect.IsRectEmpty()) {
        return;
    }

    KillTimer(TIMER_HIDE);
    if (!Render()) {
        m_available = false;
        HideOverlay();
        return;
    }

    ShowWindow(SW_SHOWNOACTIVATE);
    SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CModernZOverlay::HideOverlay()
{
    KillTimer(TIMER_HIDE);
    m_trackingMouseLeave = false;
    m_hotButtonIndex = -1;
    if (::IsWindow(m_hWnd)) {
        ShowWindow(SW_HIDE);
    }
}

void CModernZOverlay::StartHideTimer()
{
    if (::IsWindow(m_hWnd)) {
        GetWindowRect(&m_screenRect);
    }
    if (!m_autoHide || m_draggingSeek || m_draggingVolume || m_inMenu || !IsWindowVisible()) {
        return;
    }
    CPoint cursor;
    if (GetCursorPos(&cursor) && m_screenRect.PtInRect(cursor)) {
        return;
    }
    SetTimer(TIMER_HIDE, HIDE_DELAY_MS, nullptr);
}

int CModernZOverlay::HitTestButton(const CPoint& point) const
{
    for (const auto& hit : m_buttonHits) {
        if (hit.rect.PtInRect(point)) {
            return hit.index;
        }
    }
    return -1;
}

void CModernZOverlay::UpdateVolumeFromPoint(int x)
{
    if (m_volumeTrackRect.Width() > 0) {
        m_pToolBar->SetOverlayVolumeFromPoint(x, m_volumeTrackRect);
        Render();
    }
}

void CModernZOverlay::EndActiveDrag()
{
    if (m_draggingSeek) {
        m_pSeekBar->EndOverlayDrag();
        m_draggingSeek = false;
    }
    m_draggingVolume = false;
    m_pressedButtonIndex = -1;
    m_pressedCommand = 0;
    m_pressedButtonRect.SetRectEmpty();
}

bool CModernZOverlay::Render()
{
    if (!::IsWindow(m_hWnd) || !m_pVideoParent || !::IsWindow(m_pVideoParent->GetSafeHwnd())) {
        return false;
    }

    CRect clientRect;
    GetClientRect(&clientRect);
    const int width = clientRect.Width();
    const int height = clientRect.Height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    HDC screenDC = ::GetDC(nullptr);
    if (!screenDC) {
        return false;
    }

    HDC memoryDC = ::CreateCompatibleDC(screenDC);
    if (!memoryDC) {
        ::ReleaseDC(nullptr, screenDC);
        return false;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(screenDC, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) {
            ::DeleteObject(bitmap);
        }
        ::DeleteDC(memoryDC);
        ::ReleaseDC(nullptr, screenDC);
        return false;
    }

    HGDIOBJ oldBitmap = ::SelectObject(memoryDC, bitmap);
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    auto* pixels = static_cast<DWORD*>(bits);
    std::fill(pixels, pixels + pixelCount, 0x00010101);

    CDC dc;
    dc.Attach(memoryDC);
    dc.SetBkMode(TRANSPARENT);

    const int inset = m_pMainFrame->m_dpi.ScaleX(6);
    CRect panel(inset, m_pMainFrame->m_dpi.ScaleY(3),
                width - inset, height - m_pMainFrame->m_dpi.ScaleY(3));
    CBrush panelBrush(RGB(30, 30, 34));
    CPen panelPen(PS_SOLID, 1, RGB(45, 45, 50));
    CBrush* oldBrush = dc.SelectObject(&panelBrush);
    CPen* oldPen = dc.SelectObject(&panelPen);
    dc.RoundRect(&panel, CPoint(m_pMainFrame->m_dpi.ScaleY(14), m_pMainFrame->m_dpi.ScaleY(14)));
    dc.SelectObject(oldPen);
    dc.SelectObject(oldBrush);

    m_seekHitRect.SetRect(m_pMainFrame->m_dpi.ScaleX(20), m_pMainFrame->m_dpi.ScaleY(7),
                          width - m_pMainFrame->m_dpi.ScaleX(20), m_pMainFrame->m_dpi.ScaleY(25));
    m_pSeekBar->DrawOverlay(dc, CRect(m_seekHitRect.left, m_pMainFrame->m_dpi.ScaleY(15),
                                      m_seekHitRect.right, m_pMainFrame->m_dpi.ScaleY(16)));

    const int rowTop = m_pMainFrame->m_dpi.ScaleY(34);
    const int rowHeight = m_pMainFrame->m_dpi.ScaleY(30);
    const int volumeWidth = m_pMainFrame->m_dpi.ScaleX(124);
    m_volumeBounds.SetRect(width - volumeWidth - m_pMainFrame->m_dpi.ScaleX(18), rowTop,
                           width - m_pMainFrame->m_dpi.ScaleX(18), rowTop + rowHeight);
    const int volumeIconSize = std::min(rowHeight, m_pMainFrame->m_dpi.ScaleY(18));
    m_volumeIconRect.SetRect(m_volumeBounds.left, rowTop, m_volumeBounds.left + volumeIconSize, rowTop + rowHeight);
    m_volumeTrackRect.SetRect(m_volumeBounds.left + volumeIconSize + m_pMainFrame->m_dpi.ScaleX(8),
                              rowTop + m_pMainFrame->m_dpi.ScaleY(6), m_volumeBounds.right,
                              rowTop + rowHeight - m_pMainFrame->m_dpi.ScaleY(6));
    m_pToolBar->DrawOverlayVolume(dc, m_volumeBounds);

    struct PendingButton {
        int index;
        UINT command;
        int width;
        int gap;
    };
    std::vector<PendingButton> pending;
    int totalWidth = 0;
    m_volumeButtonIndex = -1;
    for (int i = 0; i < m_pToolBar->GetOverlayButtonCount(); ++i) {
        TBBUTTON button = {};
        if (!m_pToolBar->GetOverlayButton(i, button)) {
            continue;
        }
        if (button.idCommand == ID_VOLUME_MUTE) {
            m_volumeButtonIndex = i;
            continue;
        }
        CRect nativeRect;
        int buttonWidth = m_pToolBar->GetOverlayButtonRect(i, nativeRect) ? nativeRect.Width() : 0;
        if (button.fsStyle & TBSTYLE_SEP) {
            const int gap = std::clamp(buttonWidth, m_pMainFrame->m_dpi.ScaleX(5), m_pMainFrame->m_dpi.ScaleX(18));
            if (!pending.empty()) {
                pending.back().gap = gap;
                totalWidth += gap;
            }
            continue;
        }
        buttonWidth = std::clamp(buttonWidth, m_pMainFrame->m_dpi.ScaleX(28), m_pMainFrame->m_dpi.ScaleX(42));
        pending.push_back({ i, button.idCommand, buttonWidth, 0 });
        totalWidth += buttonWidth;
    }

    const int buttonAreaRight = m_volumeBounds.left - m_pMainFrame->m_dpi.ScaleX(12);
    int buttonX = std::max(m_pMainFrame->m_dpi.ScaleX(12),
                           (buttonAreaRight - totalWidth) / 2);
    m_buttonHits.clear();
    for (const auto& item : pending) {
        CRect buttonRect(buttonX, rowTop, buttonX + item.width, rowTop + rowHeight);
        m_pToolBar->DrawOverlayButton(dc, item.index, buttonRect,
                                      m_hotButtonIndex == item.index,
                                      m_pressedButtonIndex == item.index);
        TBBUTTON stateButton = {};
        if (m_pToolBar->GetOverlayButton(item.index, stateButton) && (stateButton.fsState & TBSTATE_ENABLED)) {
            m_buttonHits.push_back({ item.index, item.command, buttonRect });
        }
        buttonX += item.width + item.gap;
    }

    dc.Detach();

    // UpdateLayeredWindow requires premultiplied alpha. The key color is reserved
    // for transparent pixels; all painted pixels receive the panel's translucency.
    constexpr BYTE alpha = 226;
    for (size_t i = 0; i < pixelCount; ++i) {
        DWORD pixel = pixels[i];
        if ((pixel & 0x00FFFFFF) == 0x00010101) {
            pixels[i] = 0;
            continue;
        }
        const BYTE blue = static_cast<BYTE>(pixel & 0xFF);
        const BYTE green = static_cast<BYTE>((pixel >> 8) & 0xFF);
        const BYTE red = static_cast<BYTE>((pixel >> 16) & 0xFF);
        pixels[i] = (static_cast<DWORD>(alpha) << 24)
                    | (static_cast<DWORD>(red * alpha / 255) << 16)
                    | (static_cast<DWORD>(green * alpha / 255) << 8)
                    | static_cast<DWORD>(blue * alpha / 255);
    }

    POINT destination = { m_screenRect.left, m_screenRect.top };
    SIZE size = { width, height };
    POINT sourcePoint = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    const BOOL updated = ::UpdateLayeredWindow(m_hWnd, screenDC, &destination, &size,
                                                memoryDC, &sourcePoint, 0, &blend, ULW_ALPHA);

    ::SelectObject(memoryDC, oldBitmap);
    ::DeleteObject(bitmap);
    ::DeleteDC(memoryDC);
    ::ReleaseDC(nullptr, screenDC);
    return updated != FALSE;
}

void CModernZOverlay::OnMouseMove(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    if (!m_trackingMouseLeave) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        m_trackingMouseLeave = ::TrackMouseEvent(&tme) != FALSE;
    }

    KillTimer(TIMER_HIDE);
    if (m_draggingSeek) {
        m_pSeekBar->UpdateOverlayDrag(m_pSeekBar->PositionFromOverlayPoint(point.x, m_seekHitRect));
    } else if (m_draggingVolume) {
        UpdateVolumeFromPoint(point.x);
    } else {
        const int hot = HitTestButton(point);
        if (hot != m_hotButtonIndex) {
            m_hotButtonIndex = hot;
            Render();
        }
    }

}

void CModernZOverlay::OnMouseLeave()
{
    m_trackingMouseLeave = false;
    m_hotButtonIndex = -1;
    Render();
    StartHideTimer();
}

void CModernZOverlay::OnLButtonDown(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    if (m_seekHitRect.PtInRect(point)) {
        if (m_pSeekBar->BeginOverlayDrag()) {
            m_draggingSeek = true;
            SetCapture();
            m_pSeekBar->UpdateOverlayDrag(m_pSeekBar->PositionFromOverlayPoint(point.x, m_seekHitRect));
        }
        return;
    }

    if (m_volumeTrackRect.PtInRect(point)) {
        m_draggingVolume = true;
        SetCapture();
        UpdateVolumeFromPoint(point.x);
        return;
    }

    if (m_volumeIconRect.PtInRect(point) && m_volumeButtonIndex >= 0) {
        CRect screenRect(m_volumeIconRect);
        ClientToScreen(&screenRect);
        m_pToolBar->DispatchOverlayButton(ID_VOLUME_MUTE, m_volumeButtonIndex, screenRect);
        Render();
        return;
    }

    m_pressedButtonIndex = HitTestButton(point);
    if (m_pressedButtonIndex >= 0) {
        const auto it = std::find_if(m_buttonHits.begin(), m_buttonHits.end(),
                                     [&](const ButtonHit& hit) { return hit.index == m_pressedButtonIndex; });
        if (it != m_buttonHits.end()) {
            m_pressedCommand = it->command;
            m_pressedButtonRect = it->rect;
            SetCapture();
            Render();
        }
    }
}

void CModernZOverlay::OnLButtonUp(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    if (m_draggingSeek || m_draggingVolume) {
        EndActiveDrag();
        if (GetCapture() == this) {
            ReleaseCapture();
        }
        Render();
        StartHideTimer();
        return;
    }

    const int releasedIndex = HitTestButton(point);
    const UINT command = m_pressedCommand;
    const int pressedIndex = m_pressedButtonIndex;
    const CRect buttonRect = m_pressedButtonRect;
    EndActiveDrag();
    if (GetCapture() == this) {
        ReleaseCapture();
    }

    if (pressedIndex >= 0 && releasedIndex == pressedIndex && command != 0) {
        CRect screenRect(buttonRect);
        ClientToScreen(&screenRect);
        m_inMenu = true;
        m_pToolBar->DispatchOverlayButton(command, pressedIndex, screenRect);
        m_inMenu = false;
    }

    Render();
    StartHideTimer();
}

void CModernZOverlay::OnRButtonUp(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    if (m_volumeIconRect.PtInRect(point) && m_volumeButtonIndex >= 0) {
        CRect screenRect(m_volumeIconRect);
        ClientToScreen(&screenRect);
        m_inMenu = true;
        m_pToolBar->DispatchOverlayButton(ID_VOLUME_MUTE, m_volumeButtonIndex, screenRect, true);
        m_inMenu = false;
        StartHideTimer();
        return;
    }

    const int index = HitTestButton(point);
    if (index < 0) {
        return;
    }

    const auto it = std::find_if(m_buttonHits.begin(), m_buttonHits.end(),
                                 [&](const ButtonHit& hit) { return hit.index == index; });
    if (it == m_buttonHits.end()) {
        return;
    }

    CRect screenRect(it->rect);
    ClientToScreen(&screenRect);
    m_inMenu = true;
    m_pToolBar->DispatchOverlayButton(it->command, index, screenRect, true);
    m_inMenu = false;
    Render();
    StartHideTimer();
}

BOOL CModernZOverlay::OnMouseWheel(UINT nFlags, short zDelta, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    ScreenToClient(&point);
    if (!m_volumeBounds.PtInRect(point)) {
        return FALSE;
    }
    if (zDelta > 0) {
        m_pToolBar->m_volctrl.IncreaseVolume();
    } else if (zDelta < 0) {
        m_pToolBar->m_volctrl.DecreaseVolume();
    }
    Render();
    return TRUE;
}

void CModernZOverlay::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_HIDE) {
        KillTimer(TIMER_HIDE);
        if (m_autoHide && !m_draggingSeek && !m_draggingVolume && !m_inMenu) {
            HideOverlay();
        }
        return;
    }
    __super::OnTimer(nIDEvent);
}

void CModernZOverlay::OnCaptureChanged(CWnd* pWnd)
{
    EndActiveDrag();
    __super::OnCaptureChanged(pWnd);
    Render();
    StartHideTimer();
}

BOOL CModernZOverlay::OnEraseBkgnd(CDC* pDC)
{
    UNREFERENCED_PARAMETER(pDC);
    return TRUE;
}

void CModernZOverlay::OnPaint()
{
    CPaintDC dc(this);
    UNREFERENCED_PARAMETER(dc);
    Render();
}

void CModernZOverlay::OnDestroy()
{
    KillTimer(TIMER_HIDE);
    m_trackingMouseLeave = false;
    m_available = false;
    m_pVideoParent = nullptr;
    __super::OnDestroy();
}
