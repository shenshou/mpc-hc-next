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
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MBUTTONDOWN()
    ON_WM_XBUTTONDOWN()
    ON_WM_XBUTTONDBLCLK()
    ON_WM_LBUTTONUP()
    ON_WM_RBUTTONDOWN()
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
    if (m_volumeTooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
        m_volumeToolInfo.cbSize = sizeof(TOOLINFO);
        m_volumeToolInfo.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        m_volumeToolInfo.hwnd = m_hWnd;
        m_volumeToolInfo.uId = 1;
        m_volumeToolInfo.hinst = AfxGetInstanceHandle();
        m_volumeToolInfo.lpszText = const_cast<LPTSTR>(_T(""));
        m_volumeTooltip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&m_volumeToolInfo);
    }
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
        if (m_autoHide) {
            StartHideTimer();
        }
    }
    return true;
}

bool CModernZOverlay::SetBounds(const CRect& videoRect)
{
    m_videoRect = videoRect;
    if (!::IsWindow(m_hWnd) || videoRect.IsRectEmpty()) {
        m_available = false;
        m_screenRect.SetRectEmpty();
        HideOverlay();
        return false;
    }

    const int height = m_pMainFrame->m_dpi.ScaleY(76);
    int requiredButtonWidth = 0;
    bool haveButton = false;
    for (int i = 0; i < m_pToolBar->GetOverlayButtonCount(); ++i) {
        TBBUTTON button = {};
        if (!m_pToolBar->GetOverlayButton(i, button) || button.idCommand == ID_VOLUME_MUTE) {
            continue;
        }

        CRect nativeRect;
        const int nativeWidth = m_pToolBar->GetOverlayButtonRect(i, nativeRect) ? nativeRect.Width() : 0;
        if (button.fsStyle & TBSTYLE_SEP) {
            if (haveButton) {
                requiredButtonWidth += std::clamp(nativeWidth,
                                                  m_pMainFrame->m_dpi.ScaleX(5),
                                                  m_pMainFrame->m_dpi.ScaleX(18));
            }
        } else {
            requiredButtonWidth += std::clamp(nativeWidth,
                                              m_pMainFrame->m_dpi.ScaleX(28),
                                              m_pMainFrame->m_dpi.ScaleX(42));
            haveButton = true;
        }
    }

    // Keep the button row clear of the fixed-width volume control. If the user's
    // customized toolbar cannot fit, fall back to the native controls instead
    // of allowing the overlay buttons to overlap the volume slider.
    const int requiredVideoWidth = std::max(m_pMainFrame->m_dpi.ScaleX(260),
                                            requiredButtonWidth + m_pMainFrame->m_dpi.ScaleX(166))
                                   + m_pSeekBar->GetOverlayTimeWidth();
    if (videoRect.Height() <= height || videoRect.Width() <= requiredVideoWidth) {
        m_available = false;
        m_screenRect.SetRectEmpty();
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
    } else if (IsWindowVisible()) {
        StartHideTimer();
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
        KillTimer(TIMER_BUTTON_TOOLTIP);
        m_hoverTooltipButtonIndex = -1;
        KillTimer(TIMER_VOLUME_TOOLTIP);
        m_hoveringVolume = false;
        HideVolumeTooltip();
        KillTimer(TIMER_TOOLTIP);
        m_hoveringSeek = false;
        m_pSeekBar->HideOverlayTooltip();
        m_pMainFrame->PreviewWindowHide();
        StartHideTimer();
    }
}

void CModernZOverlay::NotifyVideoMouseLeave()
{
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    m_hoveringSeek = false;
    m_hoveringVolume = false;
    KillTimer(TIMER_VOLUME_TOOLTIP);
    HideVolumeTooltip();
    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
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
    // A geometry fallback or parent switch can hide the overlay mid-interaction.
    // End the seek/volume drag and release capture before making the window invisible.
    EndActiveDrag();
    if (GetCapture() == this) {
        ReleaseCapture();
    }

    KillTimer(TIMER_HIDE);
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_VOLUME_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    m_hoveringSeek = false;
    m_hoveringVolume = false;
    HideVolumeTooltip();
    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
    m_trackingMouseLeave = false;
    m_hotButtonIndex = -1;
    if (::IsWindow(m_hWnd)) {
        ShowWindow(SW_HIDE);
    }
}

void CModernZOverlay::StartHideTimer()
{
    KillTimer(TIMER_HIDE);
    if (::IsWindow(m_hWnd)) {
        GetWindowRect(&m_screenRect);
    }
    if (!m_autoHide || m_draggingSeek || m_draggingVolume || m_pressedVolumeIcon
            || m_pressedButtonIndex >= 0 || m_rightPressedVolume || m_rightPressedTime || m_rightPressedButtonIndex >= 0
            || m_inMenu || !IsWindowVisible()) {
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
        CPoint screenPoint;
        if (GetCursorPos(&screenPoint)) {
            ShowVolumeTooltip(screenPoint);
        }
        Render();
    }
}

void CModernZOverlay::ShowVolumeTooltip(const CPoint& screenPoint)
{
    if (!m_volumeTooltip.GetSafeHwnd()) {
        return;
    }

    m_volumeTooltipText.Format(IDS_VOLUME, m_pToolBar->m_volctrl.GetPos());
    m_volumeToolInfo.lpszText = const_cast<LPTSTR>((LPCTSTR)m_volumeTooltipText);
    m_volumeTooltip.SendMessage(TTM_UPDATETIPTEXT, 0, (LPARAM)&m_volumeToolInfo);
    m_volumeTooltip.SendMessage(TTM_TRACKACTIVATE, TRUE, (LPARAM)&m_volumeToolInfo);
    m_volumeTooltip.SendMessage(TTM_TRACKPOSITION, 0,
                                MAKELPARAM(screenPoint.x - m_pMainFrame->m_dpi.ScaleX(24),
                                           screenPoint.y - m_pMainFrame->m_dpi.ScaleY(28)));
}

void CModernZOverlay::ShowButtonTooltip(int buttonIndex, const CPoint& screenPoint)
{
    if (!m_volumeTooltip.GetSafeHwnd()) {
        return;
    }

    TBBUTTON button = {};
    CString tooltipText;
    if (!m_pToolBar->GetOverlayButton(buttonIndex, button)
            || !m_pToolBar->GetOverlayButtonTooltipText(button.idCommand, tooltipText)) {
        HideVolumeTooltip();
        return;
    }

    m_volumeTooltipText = tooltipText;
    m_volumeToolInfo.lpszText = const_cast<LPTSTR>((LPCTSTR)m_volumeTooltipText);
    m_volumeTooltip.SendMessage(TTM_UPDATETIPTEXT, 0, (LPARAM)&m_volumeToolInfo);
    m_volumeTooltip.SendMessage(TTM_TRACKACTIVATE, TRUE, (LPARAM)&m_volumeToolInfo);

    const DWORD bubbleSizeValue = static_cast<DWORD>(m_volumeTooltip.SendMessage(TTM_GETBUBBLESIZE, 0, (LPARAM)&m_volumeToolInfo));
    const CSize bubbleSize(LOWORD(bubbleSizeValue), HIWORD(bubbleSizeValue));
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    GetMonitorInfoW(MonitorFromPoint(screenPoint, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    CPoint position(screenPoint.x - bubbleSize.cx / 2,
                    screenPoint.y - bubbleSize.cy - m_pMainFrame->m_dpi.ScaleY(8));
    position.x = std::max(monitorInfo.rcWork.left + 5,
                          std::min(position.x, monitorInfo.rcWork.right - bubbleSize.cx - 5));
    position.y = std::max(monitorInfo.rcWork.top + 5,
                          std::min(position.y, monitorInfo.rcWork.bottom - bubbleSize.cy - 5));
    m_volumeTooltip.SendMessage(TTM_TRACKPOSITION, 0, MAKELPARAM(position.x, position.y));
}

void CModernZOverlay::HideVolumeTooltip()
{
    if (m_volumeTooltip.GetSafeHwnd()) {
        m_volumeTooltip.SendMessage(TTM_TRACKACTIVATE, FALSE, (LPARAM)&m_volumeToolInfo);
    }
}

void CModernZOverlay::EndActiveDrag()
{
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_VOLUME_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    m_hoveringSeek = false;
    m_hoveringVolume = false;
    HideVolumeTooltip();
    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
    if (m_draggingSeek) {
        m_pSeekBar->EndOverlayDrag();
        m_draggingSeek = false;
    }
    m_draggingVolume = false;
    m_pressedVolumeIcon = false;
    m_pressedButtonIndex = -1;
    m_rightPressedButtonIndex = -1;
    m_rightPressedVolume = false;
    m_rightPressedTime = false;
    m_pressedCommand = 0;
    m_pressedButtonRect.SetRectEmpty();
}

bool CModernZOverlay::Render()
{
    if (!::IsWindow(m_hWnd) || !m_pVideoParent || !::IsWindow(m_pVideoParent->GetSafeHwnd())) {
        return false;
    }

    // The video parent can move without changing the overlay's client bounds.
    // Refresh the screen-space destination before every UpdateLayeredWindow call,
    // otherwise a later repaint can move the child overlay back to stale coordinates.
    if (!GetWindowRect(&m_screenRect)) {
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

    CRect seekBounds(m_pMainFrame->m_dpi.ScaleX(20), m_pMainFrame->m_dpi.ScaleY(7),
                     width - m_pMainFrame->m_dpi.ScaleX(20), m_pMainFrame->m_dpi.ScaleY(25));
    CRect seekTrackBounds;
    m_pSeekBar->GetOverlayTimeLayout(seekBounds, seekTrackBounds, m_timeHitRect);
    m_seekHitRect = seekTrackBounds;
    m_pSeekBar->DrawOverlay(dc, CRect(m_seekHitRect.left, m_pMainFrame->m_dpi.ScaleY(15),
                                      m_seekHitRect.right, m_pMainFrame->m_dpi.ScaleY(16)));
    if (!m_timeHitRect.IsRectEmpty()) {
        m_pSeekBar->DrawOverlayTime(dc, m_timeHitRect);
    }

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
        if (m_pToolBar->GetOverlayButton(item.index, stateButton)) {
            const bool enabled = (stateButton.fsState & TBSTATE_ENABLED) != 0;
            m_buttonHits.push_back({ item.index, item.command, buttonRect, enabled });
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
        const REFERENCE_TIME position = m_pSeekBar->PositionFromOverlayPoint(point.x, m_seekHitRect);
        m_pSeekBar->UpdateOverlayDrag(position);
        if (AfxGetAppSettings().fUseSeekbarHover) {
            CPoint screenPoint(point);
            ClientToScreen(&screenPoint);
            if (m_pMainFrame->CanPreviewUse()) {
                m_pSeekBar->PreviewWindowShowAtOverlay(position, screenPoint);
            } else {
                m_pSeekBar->ShowOverlayTooltip(position, screenPoint);
            }
        }
    } else if (m_draggingVolume) {
        UpdateVolumeFromPoint(point.x);
    } else {
        const int hot = HitTestButton(point);
        if (hot != m_hotButtonIndex) {
            m_hotButtonIndex = hot;
            Render();
        }

        const int tooltipButton = m_volumeIconRect.PtInRect(point) ? m_volumeButtonIndex : hot;
        if (tooltipButton != m_hoverTooltipButtonIndex) {
            KillTimer(TIMER_BUTTON_TOOLTIP);
            HideVolumeTooltip();
            m_hoverTooltipButtonIndex = tooltipButton;
            if (tooltipButton >= 0) {
                m_hoverButtonScreenPoint = point;
                ClientToScreen(&m_hoverButtonScreenPoint);
                SetTimer(TIMER_BUTTON_TOOLTIP, TOOLTIP_DELAY_MS, nullptr);
            }
        }

        if (m_seekHitRect.PtInRect(point) && AfxGetAppSettings().fUseSeekbarHover) {
            m_hoveringSeek = true;
            m_hoveringVolume = false;
            m_hoverTooltipButtonIndex = -1;
            KillTimer(TIMER_BUTTON_TOOLTIP);
            KillTimer(TIMER_VOLUME_TOOLTIP);
            HideVolumeTooltip();
            m_hoverSeekPosition = m_pSeekBar->PositionFromOverlayPoint(point.x, m_seekHitRect);
            m_hoverSeekScreenPoint = point;
            ClientToScreen(&m_hoverSeekScreenPoint);
            if (m_pMainFrame->CanPreviewUse()) {
                KillTimer(TIMER_TOOLTIP);
                m_pSeekBar->HideOverlayTooltip();
                m_pSeekBar->PreviewWindowShowAtOverlay(m_hoverSeekPosition, m_hoverSeekScreenPoint);
            } else {
                SetTimer(TIMER_TOOLTIP, TOOLTIP_DELAY_MS, nullptr);
            }
        } else {
            m_hoveringSeek = false;
            KillTimer(TIMER_TOOLTIP);
            m_pSeekBar->HideOverlayTooltip();
            m_pMainFrame->PreviewWindowHide();
            if (m_volumeTrackRect.PtInRect(point)) {
                m_hoveringVolume = true;
                m_hoverTooltipButtonIndex = -1;
                KillTimer(TIMER_BUTTON_TOOLTIP);
                HideVolumeTooltip();
                m_hoverVolumeScreenPoint = point;
                ClientToScreen(&m_hoverVolumeScreenPoint);
                SetTimer(TIMER_VOLUME_TOOLTIP, TOOLTIP_DELAY_MS, nullptr);
            } else {
                m_hoveringVolume = false;
                KillTimer(TIMER_VOLUME_TOOLTIP);
                if (tooltipButton < 0) {
                    HideVolumeTooltip();
                }
            }
        }
    }

}

void CModernZOverlay::OnMouseLeave()
{
    m_trackingMouseLeave = false;
    m_hotButtonIndex = -1;
    m_hoveringSeek = false;
    m_hoveringVolume = false;
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    KillTimer(TIMER_VOLUME_TOOLTIP);
    HideVolumeTooltip();
    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
    Render();
    StartHideTimer();
}

void CModernZOverlay::OnLButtonDown(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    KillTimer(TIMER_HIDE);
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_VOLUME_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    m_hoveringSeek = false;
    m_hoveringVolume = false;
    HideVolumeTooltip();
    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
    if (m_seekHitRect.PtInRect(point)) {
        KillTimer(TIMER_TOOLTIP);
        m_hoveringSeek = false;
        m_pSeekBar->HideOverlayTooltip();
        m_pMainFrame->PreviewWindowHide();
        if (m_pSeekBar->BeginOverlayDrag()) {
            m_draggingSeek = true;
            SetCapture();
            m_pSeekBar->UpdateOverlayDrag(m_pSeekBar->PositionFromOverlayPoint(point.x, m_seekHitRect));
        } else if (!m_pMainFrame->m_fFullScreen && !m_pSeekBar->DraggingThumb()) {
            ClientToScreen(&point);
            m_pMainFrame->PostMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
        }
        return;
    }

    if (m_volumeTrackRect.PtInRect(point)) {
        m_draggingVolume = true;
        m_hoveringVolume = true;
        KillTimer(TIMER_VOLUME_TOOLTIP);
        SetCapture();
        UpdateVolumeFromPoint(point.x);
        return;
    }

    if (m_volumeIconRect.PtInRect(point) && m_volumeButtonIndex >= 0) {
        m_pressedVolumeIcon = true;
        SetCapture();
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
            return;
        }
    }

    if (!m_pMainFrame->m_fFullScreen) {
        ClientToScreen(&point);
        m_pMainFrame->PostMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
    }
}

void CModernZOverlay::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    OnLButtonDown(nFlags, point);
}

void CModernZOverlay::OnMButtonDown(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    if (m_seekHitRect.PtInRect(point)) {
        m_pSeekBar->ToggleOverlayPreview();
    }
}

BOOL CModernZOverlay::OnXButtonDown(UINT nFlags, UINT nButton, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    if (m_seekHitRect.PtInRect(point)) {
        m_pSeekBar->HandleOverlayXButton(nButton);
        return TRUE;
    }
    return FALSE;
}

BOOL CModernZOverlay::OnXButtonDblClk(UINT nFlags, UINT nButton, CPoint point)
{
    return OnXButtonDown(nFlags, nButton, point);
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
    const auto releasedButton = std::find_if(m_buttonHits.begin(), m_buttonHits.end(),
                                             [&](const ButtonHit& hit) { return hit.index == releasedIndex; });
    const bool releasedButtonEnabled = releasedButton != m_buttonHits.end() && releasedButton->enabled;
    const bool pressedVolumeIcon = m_pressedVolumeIcon;
    const bool releasedVolumeIcon = m_volumeIconRect.PtInRect(point);
    const UINT command = m_pressedCommand;
    const int pressedIndex = m_pressedButtonIndex;
    const CRect buttonRect = m_pressedButtonRect;
    EndActiveDrag();
    if (GetCapture() == this) {
        ReleaseCapture();
    }

    if (pressedVolumeIcon && releasedVolumeIcon && m_volumeButtonIndex >= 0) {
        CRect screenRect(m_volumeIconRect);
        ClientToScreen(&screenRect);
        m_pToolBar->DispatchOverlayButton(ID_VOLUME_MUTE, m_volumeButtonIndex, screenRect);
    } else if (pressedIndex >= 0 && releasedIndex == pressedIndex && command != 0 && releasedButtonEnabled) {
        CRect screenRect(buttonRect);
        ClientToScreen(&screenRect);
        m_inMenu = true;
        m_pToolBar->DispatchOverlayButton(command, pressedIndex, screenRect);
        m_inMenu = false;
    }

    Render();
    StartHideTimer();
}

void CModernZOverlay::OnRButtonDown(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    KillTimer(TIMER_HIDE);
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_VOLUME_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    m_hoveringSeek = false;
    m_hoveringVolume = false;
    HideVolumeTooltip();
    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
    m_rightPressedTime = m_timeHitRect.PtInRect(point);
    m_rightPressedVolume = !m_rightPressedTime && m_volumeIconRect.PtInRect(point) && m_volumeButtonIndex >= 0;
    m_rightPressedButtonIndex = (m_rightPressedTime || m_rightPressedVolume) ? -1 : HitTestButton(point);
    if (m_rightPressedTime || m_rightPressedVolume || m_rightPressedButtonIndex >= 0) {
        SetCapture();
    } else if (!m_pMainFrame->m_fFullScreen
               && !m_seekHitRect.PtInRect(point)
               && !m_volumeTrackRect.PtInRect(point)
               && !m_volumeIconRect.PtInRect(point)
               && !m_timeHitRect.PtInRect(point)) {
        ClientToScreen(&point);
        m_pMainFrame->PostMessage(WM_NCRBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
    }
}

void CModernZOverlay::OnRButtonUp(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    const bool pressedTime = m_rightPressedTime;
    const bool releasedTime = m_timeHitRect.PtInRect(point);
    const bool pressedVolume = m_rightPressedVolume;
    const int pressedIndex = m_rightPressedButtonIndex;
    const bool releasedVolume = m_volumeIconRect.PtInRect(point) && m_volumeButtonIndex >= 0;
    const int releasedIndex = HitTestButton(point);
    m_rightPressedTime = false;
    m_rightPressedVolume = false;
    m_rightPressedButtonIndex = -1;

    if (GetCapture() == this) {
        ReleaseCapture();
    }

    if (pressedTime && releasedTime) {
        CPoint screenPoint(point);
        ClientToScreen(&screenPoint);
        m_inMenu = true;
        m_pSeekBar->ShowOverlayTimeOptionsMenu(screenPoint);
        m_inMenu = false;
    } else if (pressedVolume && releasedVolume) {
        CRect screenRect(m_volumeIconRect);
        ClientToScreen(&screenRect);
        m_inMenu = true;
        m_pToolBar->DispatchOverlayButton(ID_VOLUME_MUTE, m_volumeButtonIndex, screenRect, true);
        m_inMenu = false;
    } else if (pressedIndex >= 0 && releasedIndex == pressedIndex) {
        const auto it = std::find_if(m_buttonHits.begin(), m_buttonHits.end(),
                                     [&](const ButtonHit& hit) { return hit.index == pressedIndex; });
        if (it != m_buttonHits.end() && it->enabled) {
            CRect screenRect(it->rect);
            ClientToScreen(&screenRect);
            m_inMenu = true;
            m_pToolBar->DispatchOverlayButton(it->command, pressedIndex, screenRect, true);
            m_inMenu = false;
        }
    }

    Render();
    StartHideTimer();
}

BOOL CModernZOverlay::OnMouseWheel(UINT nFlags, short zDelta, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    ScreenToClient(&point);
    // Match the native volume slider: wheel adjustment applies to the slider, not the mute button.
    if (!m_volumeTrackRect.PtInRect(point)) {
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
    if (nIDEvent == TIMER_BUTTON_TOOLTIP) {
        KillTimer(TIMER_BUTTON_TOOLTIP);
        if (m_hoverTooltipButtonIndex >= 0 && !m_draggingSeek && !m_draggingVolume) {
            ShowButtonTooltip(m_hoverTooltipButtonIndex, m_hoverButtonScreenPoint);
        }
        return;
    }
    if (nIDEvent == TIMER_VOLUME_TOOLTIP) {
        KillTimer(TIMER_VOLUME_TOOLTIP);
        if (m_hoveringVolume && !m_draggingVolume) {
            ShowVolumeTooltip(m_hoverVolumeScreenPoint);
        }
        return;
    }
    if (nIDEvent == TIMER_TOOLTIP) {
        KillTimer(TIMER_TOOLTIP);
        if (m_hoveringSeek && !m_draggingSeek && AfxGetAppSettings().fUseSeekbarHover
                && !m_pMainFrame->CanPreviewUse()) {
            m_pSeekBar->ShowOverlayTooltip(m_hoverSeekPosition, m_hoverSeekScreenPoint);
        }
        return;
    }
    if (nIDEvent == TIMER_HIDE) {
        KillTimer(TIMER_HIDE);
        if (m_autoHide && !m_draggingSeek && !m_draggingVolume && !m_pressedVolumeIcon
                && m_pressedButtonIndex < 0 && !m_rightPressedVolume && !m_rightPressedTime && m_rightPressedButtonIndex < 0
                && !m_inMenu) {
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
    // Do not leave the native seekbar in its dragging state if its overlay is destroyed.
    EndActiveDrag();
    if (GetCapture() == this) {
        ReleaseCapture();
    }

    m_pSeekBar->HideOverlayTooltip();
    m_pMainFrame->PreviewWindowHide();
    KillTimer(TIMER_HIDE);
    KillTimer(TIMER_TOOLTIP);
    KillTimer(TIMER_VOLUME_TOOLTIP);
    KillTimer(TIMER_BUTTON_TOOLTIP);
    m_hoverTooltipButtonIndex = -1;
    HideVolumeTooltip();
    if (m_volumeTooltip.GetSafeHwnd()) {
        m_volumeTooltip.DestroyWindow();
    }
    m_trackingMouseLeave = false;
    m_available = false;
    m_pVideoParent = nullptr;
    __super::OnDestroy();
}
