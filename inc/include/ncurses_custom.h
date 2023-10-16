#pragma once
#include <ncurses.h>
#include <string>
#include <map>
#include <vector>
#include <future>

#include "utils.h"
#include "fps.h"

#define WM_UPDATE 1
#define WM_KEY 10
#define WM_PRESENT 4
#define WM_SCREEN_RESIZE 5

struct Msg
{
    unsigned int u_iMessage;
    unsigned int u_iParam;
    void *p_vParam;
    unsigned int u_iTime;

    Msg() : u_iMessage(0), u_iParam(0), p_vParam(nullptr), u_iTime(0) {}

    Msg(unsigned int message) : u_iMessage(message), u_iParam(0), p_vParam(nullptr)
    {
        u_iTime = static_cast<unsigned int>(std::time(nullptr));
    }

    Msg(unsigned int message, unsigned int param)
        : u_iMessage(message), u_iParam(param), p_vParam(nullptr)
    {
        u_iTime = static_cast<unsigned int>(std::time(nullptr));
    }

    Msg(unsigned int message, unsigned int param, void *vparam)
        : u_iMessage(message), u_iParam(param), p_vParam(vparam)
    {
        u_iTime = static_cast<unsigned int>(std::time(nullptr));
    }
};

static std::mutex c_mtxScreenMutex;
struct AWindow
{
    WINDOW *p_wndWindow = nullptr;
    WINDOW *p_wndBuffer = nullptr;
    int iCols = 0, iLines = 0;
    int iWindowPosX = 0, iWindowPosY = 0;

    bool bSkipFirst = true;
    bool bUseBuffer = true;
    const char *c_p_strTitle = "";
    int i_title_attr = A_BOLD | A_UNDERLINE | COLOR_PAIR(2);
    int iServerLine = 1;
    BlockingQueue<Msg> bq_msgMessages;

    AWindow(WINDOW *wnd)
    {
        p_wndWindow = wnd;

        iCols = getmaxx(p_wndWindow);
        iLines = getmaxy(p_wndWindow);
        getbegyx(p_wndWindow, iWindowPosY, iWindowPosX);
        p_wndBuffer = dupwin(p_wndWindow);
    }

    ~AWindow()
    {
        if (p_wndWindow)
        {
            Lock();

            delwin(p_wndWindow);
            p_wndWindow = nullptr;
            delwin(p_wndBuffer);
            p_wndBuffer = nullptr;

            Unlock();
        }
    }

    operator WINDOW *() const
    {
        return p_wndWindow;
    }

    void ResetBuffer()
    {
        Lock();

        delwin(p_wndBuffer);
        p_wndBuffer = nullptr;
        p_wndBuffer = dupwin(p_wndWindow);

        Unlock();
    }

    void Build()
    {
        AttrOn(i_title_attr);

        Move(0, 0);
        HLine(' ', iCols);
        MVPrint(0, GetTextStartXCentered(p_wndBuffer, c_p_strTitle), c_p_strTitle);

        AttrOff(i_title_attr);
    }

    void Flip(bool bClearAll = false)
    {
        NoOutRefreshBuffer();

        Lock();

        copywin(p_wndBuffer, p_wndWindow, 0, 0, 0, 0, iCols - 1, iLines - 1, bClearAll);

        Unlock();
    }

    void Present(bool bDoBuffer = false, bool bNoOut = false)
    {
        if (bSkipFirst)
        {
            if (SIR_u_iFrameSkipping > 0)
            {
                --SIR_u_iFrameSkipping;
                return;
            }

            if (bNoFrame)
            {
                if (SIR_u_iExternFrame > 0)
                {
                    --SIR_u_iExternFrame;
                }
                else
                    return;
            }
        }
        else
        {
            if (bNoFrame)
            {
                if (SIR_u_iExternFrame > 0)
                {
                    --SIR_u_iExternFrame;
                }
                else
                    return;
            }

            if (SIR_u_iFrameSkipping > 0)
            {
                --SIR_u_iFrameSkipping;
                return;
            }
        }

        if (bDoBuffer)
        {
            if (bNoOut)
                NoOutRefreshBuffer();
            else
                RefreshBuffer();
        }
        else
        {
            if (bNoOut)
                NoOutRefresh();
            else
                Refresh();
        }
        fcWindowFrameCounter.count();
    }

    void PresentVirtual()
    {
        Present(false, true);
    }

    void PresentBuffer(bool bNoOut = false)
    {
        Present(true, bNoOut);
    }

    void PresentBufferVirtual()
    {
        PresentBuffer(true);
    }

    void RequestPresent()
    {
        SIR_u_iExternFrame++;
        fcWindowReqFrameCounter.count();
    }

    void SkipFrame()
    {
        SIR_u_iFrameSkipping++;
    }

    void GetMessage(Msg *msgMessage)
    {
        *msgMessage = bq_msgMessages.pop();
    }

    bool PeekMessage(Msg *msgMessage, bool bNoRemove)
    {
        return bq_msgMessages.peek(msgMessage, bNoRemove);
    }

    void PushMessage(Msg msgMessage)
    {
        bq_msgMessages.push(msgMessage);
    }

    bool MessageEmpty()
    {
        return bq_msgMessages.empty();
    }

    bool IsLocked()
    {
        return smtxWindowLocking.is_locked;
    }

    void Lock()
    {
        smtxWindowLocking.lock();
    }

    void Unlock()
    {
        smtxWindowLocking.unlock();
    }

    void SetParent(WINDOW *p_wndParentWindow)
    {
        Lock();

        SwitchParentWindow(&p_wndWindow, p_wndParentWindow);

        Unlock();
    }

    void ClearOK(bool bBf)
    {
        Lock();

        clearok(p_wndWindow, bBf);

        Unlock();
    }

    void BKGDSet(chtype chtBKGD)
    {
        Lock();

        wbkgdset(p_wndWindow, chtBKGD);

        Unlock();
    }

    // ASync
  public:
    void MVPrint(int y, int x, const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        auto draw_func = [this, &y, &x, &fmt, &args]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wmove(p_wndBuffer, y, x);
                VPrintW(p_wndBuffer, fmt, args);
            }
            else
            {
                wmove(p_wndWindow, y, x);
                VPrintW(p_wndWindow, fmt, args);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);

        va_end(args);
    }

    void Print(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        auto draw_func = [this, &fmt, &args]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                VPrintW(p_wndBuffer, fmt, args);
            }
            else
            {
                VPrintW(p_wndWindow, fmt, args);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);

        va_end(args);
    }

    void Box(chtype chtVerCh, chtype chtHorCh)
    {
        auto draw_func = [this, &chtVerCh, &chtHorCh]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                box(p_wndBuffer, chtVerCh, chtHorCh);
            }
            else
            {
                box(p_wndWindow, chtVerCh, chtHorCh);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void Erase()
    {
        // clear and fill the window with backcolor
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                werase(p_wndBuffer);
            }
            else
            {
                werase(p_wndWindow);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void Clear()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wclear(p_wndBuffer);
            }
            else
            {
                wclear(p_wndWindow);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void TouchClient()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            untouchwin(p_wndWindow);
            touchline(p_wndWindow, iServerLine, iLines - iServerLine);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void MoveWindow(int y, int x)
    {
        auto draw_func = [this, &y, &x]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            mvwin(p_wndWindow, y, x);
            mvwin(p_wndBuffer, y, x);

            iWindowPosX = x;
            iWindowPosY = y;

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void Resize(int lines, int cols)
    {
        auto draw_func = [this, &lines, &cols]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            wresize(p_wndWindow, lines, cols);
            wresize(p_wndBuffer, lines, cols);

            iLines = lines;
            iCols = cols;

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void Move(int y, int x)
    {
        auto draw_func = [this, &y, &x]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wmove(p_wndBuffer, y, x);
            }
            else
            {
                wmove(p_wndWindow, y, x);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void Refresh()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            wrefresh(p_wndWindow);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void NoOutRefresh()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            wnoutrefresh(p_wndWindow);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void RefreshBuffer()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            wrefresh(p_wndBuffer);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void NoOutRefreshBuffer()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            wnoutrefresh(p_wndBuffer);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    template <typename attrT>
    void AttrOn(attrT attrTargs)
    {
        auto draw_func = [this, &attrTargs]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wattron(p_wndBuffer, attrTargs);
            }
            else
            {
                wattron(p_wndWindow, attrTargs);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    template <typename attrT>
    void AttrOff(attrT attrTargs)
    {
        auto draw_func = [this, &attrTargs]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wattroff(p_wndBuffer, attrTargs);
            }
            else
            {
                wattroff(p_wndWindow, attrTargs);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    template <typename attrT>
    void AttrSet(attrT attrTargs)
    {
        auto draw_func = [this, &attrTargs]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wattrset(p_wndBuffer, attrTargs);
            }
            else
            {
                wattrset(p_wndWindow, attrTargs);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void HLine(chtype chtCh, int iNum)
    {
        auto draw_func = [this, &iNum, &chtCh]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                whline(p_wndBuffer, chtCh, iNum);
            }
            else
            {
                whline(p_wndWindow, chtCh, iNum);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void VLine(chtype chtCh, int iNum)
    {
        auto draw_func = [this, &iNum, &chtCh]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            if (bUseBuffer)
            {
                wvline(p_wndBuffer, chtCh, iNum);
            }
            else
            {
                wvline(p_wndWindow, chtCh, iNum);
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void EraseRect(int iX, int iY, int iX1, int iY1, char chBackCh = ' ')
    {
        auto draw_func = [this, &iX, &iY, &iX1, &iY1, &chBackCh]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            for (int i = iY; i <= iY1; i++)
            {
                for (int j = iX; j <= iX1; j++)
                {
                    if (bUseBuffer)
                    {
                        mvwaddch(p_wndBuffer, i, j, chBackCh);
                    }
                    else
                    {
                        mvwaddch(p_wndWindow, i, j, chBackCh);
                    }
                }
            }

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void UnTouch()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            untouchwin(p_wndWindow);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void Touch()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            touchwin(p_wndWindow);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void TouchLine(int iStart, int iCount)
    {
        auto draw_func = [this, &iStart, &iCount]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            touchline(p_wndWindow, iStart, iCount);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void UnTouchBuffer()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            untouchwin(p_wndBuffer);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void TouchBuffer()
    {
        auto draw_func = [this]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            touchwin(p_wndBuffer);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

    void TouchBufferLine(int iStart, int iCount)
    {
        auto draw_func = [this, &iStart, &iCount]() {
            Lock();

            std::lock_guard<std::mutex> lock(c_mtxScreenMutex);

            touchline(p_wndBuffer, iStart, iCount);

            Unlock();
        };

        std::async(std::launch::async, draw_func);
    }

  public:
    StayInRange<unsigned int> SIR_u_iFrameSkipping = StayInRange<unsigned int>(0);
    StayInRange<unsigned int> SIR_u_iExternFrame = StayInRange<unsigned int>(0);
    bool bNoFrame = false;
    frame_counter fcWindowFrameCounter;
    frame_counter fcWindowReqFrameCounter;
    SharedMutex smtxWindowLocking;

  private:
    static void SwitchParentWindow(WINDOW **subwin, WINDOW *new_parent)
    {
        WINDOW *new_subwin = dupwin(*subwin);

        wclear(new_parent);
        wrefresh(new_parent);
        wresize(new_subwin, getmaxy(*subwin), getmaxx(*subwin));
        touchwin(new_subwin);
        wrefresh(new_subwin);
        delwin(*subwin);

        *subwin = new_subwin;
    }

    static int GetTextStartXCentered(const WINDOW *const p_wndWindow, const char *const c_strText)
    {
        int text_len = strlen(c_strText);
        int start_x = (getmaxx(p_wndWindow) - text_len) / 2;

        return start_x;
    }

    static void VPrintW(WINDOW *p_wndWindow, const char *fmt, va_list args)
    {
        while (*fmt != '\0')
        {
            if (*fmt == '%')
            {
                ++fmt;
                switch (*fmt)
                {
                case 'd':
                    wprintw(p_wndWindow, "%d", va_arg(args, int));
                    break;
                case 'f':
                    wprintw(p_wndWindow, "%f", va_arg(args, double));
                    break;
                case 's':
                    wprintw(p_wndWindow, "%s", va_arg(args, char *));
                    break;
                default:
                    wprintw(p_wndWindow, "?");
                    break;
                }
            }
            else
            {
                wprintw(p_wndWindow, "%c", *fmt);
            }
            ++fmt;
        }
    }
};

struct WindowManager
{
  private:
    int iBufferClears = 0;
    int iBufferErases = 0;

  public:
    std::mutex c_mtxScreenBufferMutex;

  public:
    int iScreenCols, iScreenLines;
    int x, y;

  public:
    WindowManager(AWindow *p_wndScreen, AWindow *p_wndScreenBuffer)
    {
        iScreenCols = COLS;
        iScreenLines = LINES;

        SetScreenBuffer(p_wndScreenBuffer);
        SetScreen(p_wndScreen);
    }

    void SetScreen(AWindow *p_wndScreen)
    {
        Lock();

        this->p_wndScreen = p_wndScreen;

        Unlock();
    }

    void SetScreenBuffer(AWindow *p_wndScreenBuffer)
    {
        Lock();

        if (this->p_wndScreenBuffer)
        {
            this->p_wndScreenBuffer->bUseBuffer = bLstWndBUseBuffer;
        }

        this->p_wndScreenBuffer = p_wndScreenBuffer;
        bLstWndBUseBuffer = this->p_wndScreenBuffer->bUseBuffer;
        this->p_wndScreenBuffer->bUseBuffer = false;

        Unlock();

        SetWindowsParent(*p_wndScreenBuffer);
    }

    void Flip()
    {
        Lock();

        copywin(*p_wndScreenBuffer, *p_wndScreen, 0, 0, 0, 0, iScreenCols - 1, iScreenLines - 1, TRUE);
        p_wndScreen->Present();

        if (iBufferClears > 0)
        {
            p_wndScreenBuffer->Clear();
            p_wndScreenBuffer->PresentVirtual();

            --iBufferClears;
        }
        if (iBufferErases > 0)
        {
            p_wndScreenBuffer->Erase();
            p_wndScreenBuffer->PresentVirtual();

            --iBufferErases;
        }

        Unlock();
    }

    void ClearBuffer()
    {
        ++iBufferClears;
    }

    void EraseBuffer()
    {
        ++iBufferErases;
    }

    void Lock()
    {
        c_mtxScreenBufferMutex.lock();
    }

    void Unlock()
    {
        c_mtxScreenBufferMutex.unlock();
    }

    bool NewScreenSize()
    {
        if (iScreenCols != COLS || iScreenLines != LINES)
            return true;
        return false;
    }

    void UpdateScreenSize(bool bPresentWindows = false)
    {
        iScreenCols = COLS;
        iScreenLines = LINES;

        Lock();

        p_wndScreenBuffer->Resize(iScreenLines, iScreenCols);
        p_wndScreen->Resize(iScreenLines, iScreenCols);

        Unlock();

        BroadcastMessage(Msg{WM_SCREEN_RESIZE});

        if (bPresentWindows)
        {
            PresentWindows(true, true);
        }
    }

    void UpdatePos(bool bPresentWindows = false)
    {
        Lock();

        p_wndScreenBuffer->MoveWindow(y, x);
        p_wndScreen->MoveWindow(y, x);

        Unlock();

        BroadcastMessage(Msg{WM_SCREEN_RESIZE});

        if (bPresentWindows)
        {
            PresentWindows(true, true);
        }
    }

    void PresentWindows(bool bNewFrame = true, bool bSendUpdateMsg = false, bool bSendPresentMsg = false)
    {
        if (bSendUpdateMsg)
            UpdateWindows();
        if (bSendPresentMsg)
            BroadcastMessage(Msg{WM_PRESENT});

        Lock();
        
        if (bNewFrame)
            p_wndScreenBuffer->Erase();

        for (int i = WindowsList.size() - 1; i >= 0; --i)
        {
            WindowsList[i]->PresentVirtual();
        }

        p_wndScreenBuffer->Touch();

        Unlock();
    }

    void UpdateWindows()
    {
        BroadcastMessage(Msg{WM_UPDATE});
    }

    void Add(const char *c_strName, AWindow *p_awndWindow)
    {
        Lock();

        p_awndWindow->SetParent(*p_wndScreenBuffer);

        Unlock();

        Windows[c_strName] = p_awndWindow;

        WindowsList.push_back(p_awndWindow);
    }

    /*void WindowNoEcho(const char *c_strName)
    {
        int x, y, x1, y1;
        x = Windows[c_strName]->iWindowPosX;
        y = Windows[c_strName]->iWindowPosY;
        x1 = x + Windows[c_strName]->iCols;
        y1 = y + Windows[c_strName]->iLines;
        
        Rect r{x, y, x1, y1};

        bq_msgPrePresentEvents.push(Msg{
            WMGR_ERASE_RECT,
            0,
            &r});
    }

    void WindowNoEcho(AWindow *p_awndWindow)
    {
        int x, y, x1, y1;
        x = p_awndWindow->iWindowPosX;
        y = p_awndWindow->iWindowPosY;
        x1 = x + p_awndWindow->iCols;
        y1 = y + p_awndWindow->iLines;

        Rect r{x, y, x1, y1};

        bq_msgPrePresentEvents.push(Msg{
            WMGR_ERASE_RECT,
            0,
            &r});
    }*/

    void RemoveWindow(const char *c_strName)
    {
        // erase from windows
        auto it = Windows.find(c_strName);
        if (it != Windows.end())
        {
            Windows.erase(it);
        }

        // erase from windows list
        for (auto vit = WindowsList.begin(); vit != WindowsList.end(); vit++)
        {
            if (*vit == Windows[c_strName])
            {
                WindowsList.erase(vit);
                break;
            }
        }
    }

    bool GetWindow(const char *c_strName, AWindow **p_awndWindow)
    {
        if (!IsWindow(c_strName))
        {
            return false;
        }

        *p_awndWindow = Windows[c_strName];
        return true;
    }

    bool IsWindow(const char *c_strName)
    {
        /*auto it = Windows.find(c_strName);
        if (it != Windows.end())
        {
            return true;
        }
        
        return false;*/

        return Windows.count(c_strName);
    }

    void MakeFront(const char *c_strName)
    {
        auto it = std::find(WindowsList.begin(), WindowsList.end(), Windows[c_strName]);
        if (it != WindowsList.end())
        {
            WindowsList.erase(it);
            WindowsList.insert(WindowsList.begin(), Windows[c_strName]);
        }
    }

    void MakeFront(AWindow *p_awndWindow)
    {
        auto it = std::find(WindowsList.begin(), WindowsList.end(), p_awndWindow);
        if (it != WindowsList.end())
        {
            WindowsList.erase(it);
            WindowsList.insert(WindowsList.begin(), p_awndWindow);
        }
    }

    bool GetFront(AWindow **p_awndWindow)
    {
        if (Windows.empty() && WindowsList.empty())
            return false;

        *p_awndWindow = WindowsList.front();
        return true;
    }

    void BroadcastMessage(Msg msgMessage)
    {
        for (const auto & [ key, value ] : Windows)
        {
            if (value != nullptr)
            {
                value->PushMessage(msgMessage);
            }
        }
    }

    void SendMessage(const char *c_strName, Msg msgMessage)
    {
        Windows[c_strName]->PushMessage(msgMessage);
    }
    void SendMessage(AWindow *p_awndWindow, Msg msgMessage)
    {
        p_awndWindow->PushMessage(msgMessage);
    }

    const std::map<const char *, AWindow *> *GetWindowsMap()
    {
        return &Windows;
    }

    const std::vector<AWindow *> *GetWindowsList()
    {
        return &WindowsList;
    }

    AWindow **GetScreen()
    {
        return &p_wndScreen;
    }

    AWindow **GetScreenBuffer()
    {
        return &p_wndScreenBuffer;
    }

  private:
    void SetWindowsParent(WINDOW *p_wndParentWindow)
    {
        Lock();

        for (int i = WindowsList.size() - 1; i >= 0; --i)
        {
            WindowsList[i]->SetParent(p_wndParentWindow);
        }

        Unlock();
    }

  private:
    std::map<const char *, AWindow *> Windows;
    std::vector<AWindow *> WindowsList;
    AWindow *p_wndScreenBuffer = nullptr;
    AWindow *p_wndScreen = nullptr;

  private:
    bool bLstWndBUseBuffer = 0;
    BlockingQueue<Msg> bq_msgPrePresentEvents;
};
