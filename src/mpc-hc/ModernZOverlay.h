/*
 * ModernZ video control overlay.
 *
 * This file is part of MPC-HC.
 * Licensed under the GNU General Public License version 3 or later.
 */

#pragma once

#include <vector>

class CMainFrame;
class CPlayerSeekBar;
class CPlayerToolBar;

class CModernZOverlay : public CWnd
{
public:
    CModernZOverlay(CMainFrame* pMainFrame, CPlayerSeekBar* pSeekBar, CPlayerToolBar* pToolBar);
    virtual ~CModernZOverlay();

    bool Bind(CWnd* pVideoParent);
    bool SetBounds(const CRect& videoRect);
    void SetAutoHide(bool autoHide);
    void NotifyVideoMouseMove(const CPoint& screenPoint);
    void NotifyVideoMouseLeave();
    void Refresh();
    bool IsReady() const;

protected:
    DECLARE_MESSAGE_MAP()

    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
    afx_msg BOOL OnXButtonDown(UINT nFlags, UINT nButton, CPoint point);
    afx_msg BOOL OnXButtonDblClk(UINT nFlags, UINT nButton, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint point);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg void OnDestroy();

private:
    struct ButtonHit {
        int index;
        UINT command;
        CRect rect;
    };

    static constexpr UINT_PTR TIMER_HIDE = 1;
    static constexpr UINT_PTR TIMER_TOOLTIP = 2;
    static constexpr UINT_PTR TIMER_VOLUME_TOOLTIP = 3;
    static constexpr UINT HIDE_DELAY_MS = 2000;
    static constexpr UINT TOOLTIP_DELAY_MS = 500;

    CMainFrame* m_pMainFrame;
    CPlayerSeekBar* m_pSeekBar;
    CPlayerToolBar* m_pToolBar;
    CWnd* m_pVideoParent = nullptr;

    CRect m_videoRect;
    CRect m_screenRect;
    CRect m_seekHitRect;
    CRect m_volumeBounds;
    CRect m_volumeIconRect;
    CRect m_volumeTrackRect;
    std::vector<ButtonHit> m_buttonHits;

    int m_volumeButtonIndex = -1;
    int m_hotButtonIndex = -1;
    int m_pressedButtonIndex = -1;
    int m_rightPressedButtonIndex = -1;
    bool m_rightPressedVolume = false;
    UINT m_pressedCommand = 0;
    CRect m_pressedButtonRect;

    bool m_autoHide = true;
    bool m_draggingSeek = false;
    bool m_draggingVolume = false;
    bool m_trackingMouseLeave = false;
    bool m_inMenu = false;
    bool m_hoveringSeek = false;
    bool m_hoveringVolume = false;
    REFERENCE_TIME m_hoverSeekPosition = 0;
    CPoint m_hoverSeekScreenPoint;
    CPoint m_hoverVolumeScreenPoint;
    CString m_volumeTooltipText;
    TOOLINFO m_volumeToolInfo = {};
    CToolTipCtrl m_volumeTooltip;
    bool m_available = false;

    bool CreateOverlayWindow(CWnd* parent);
    void ShowOverlay();
    void HideOverlay();
    void StartHideTimer();
    bool Render();
    int HitTestButton(const CPoint& point) const;
    void UpdateVolumeFromPoint(int x);
    void ShowVolumeTooltip(const CPoint& screenPoint);
    void HideVolumeTooltip();
    void EndActiveDrag();
};
