#include <ncurses.h>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>
#include <shared_mutex> // for sync

#include "fps.hpp"
#include "utils.hpp"
#include "ncurses_custom.hpp"
#include "defs.hpp"

#define UE_SCREENSIZE_UPDATE 10

void ExitHandler();
void QueueHandler();
void TimerHandler();
void MainWindowHandler();
void InfoWindowHandler();
void DebugConsoleWindowHandler();

// Datas
WINDOW *p_wndHostWindow = nullptr;
WINDOW *p_wndHostWindowBuffer = nullptr;
AWindow *p_awndHostWindow = nullptr;
AWindow *p_awndHostWindowBuffer = nullptr;
BlockingQueue<int> bq_iEvents;
BlockingQueue<int> bq_iUpdateEvents;
WindowManager *p_wmgrWindows;
frame_rater<60> frFrameRater;
frame_counter fcFrameCounter;
RangeQueue<std::string> strDebugLog{10}; // 10 Lines

// Program Main Entry
int main(int argc, char *argv[])
{
    // Registers
    atexit(ExitHandler);

    // init ncurses
    p_wndHostWindow = initscr(); // screen
    raw();                       // raw keyboard input
    cbreak();                    // for better keyboard processing
    noecho();                    // for better echo controlling
    keypad(stdscr, TRUE);        // input processing
    start_color();               // enable color support
    curs_set(FALSE);             // hide cursor

    init_pair(1, COLOR_WHITE, COLOR_BLUE);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    init_pair(3, COLOR_RED, COLOR_YELLOW);
    init_pair(4, COLOR_GREEN, COLOR_RED);

    fcFrameCounter.noUpdateDelay = true;

    p_wndHostWindowBuffer = newwin(COLS, LINES, 0, 0);
    p_awndHostWindow = new AWindow{p_wndHostWindow};
    p_awndHostWindowBuffer = new AWindow{p_wndHostWindowBuffer};

    p_wmgrWindows = new WindowManager{p_awndHostWindow, p_awndHostWindowBuffer};

    // screen check
    if (!has_colors())
    {
        // TODO: Process None-Color Screen
        printw("(Warn) Screen Not Support Colors.\n");
    }

    //Create Window
    {
        // Create Main Window
        {
            AWindow *p_wndWindow{};
            p_wndWindow = new AWindow(subwin(p_wndHostWindow, 12, 32, 1, 0));
            p_wndWindow->c_p_strTitle = "Main Window";
            p_wndWindow->bNoFrame = true;
            p_wndWindow->fcWindowReqFrameCounter.noUpdateDelay = true;
            p_wndWindow->BKGDSet(COLOR_PAIR(1));
            p_wndWindow->ResetBuffer();
            p_wmgrWindows->Add("p_wndMainWindow", p_wndWindow);
        }
        // Create Info Window
        {
            AWindow *p_wndWindow{};
            p_wndWindow = new AWindow(subwin(p_wndHostWindow, 14, 42, 20, 1));
            p_wndWindow->c_p_strTitle = "Info Window";
            p_wndWindow->BKGDSet(COLOR_PAIR(3));
            p_wndWindow->ResetBuffer();
            p_wmgrWindows->Add("p_wndInfoWindow", p_wndWindow);
        }
        // Create Debug Console Window
        {
            AWindow *p_wndWindow{};
            p_wndWindow = new AWindow(subwin(p_wndHostWindow, 14, 20, 3, 33));
            p_wndWindow->c_p_strTitle = "Debug Console Window";
            p_wndWindow->BKGDSet(COLOR_PAIR(4));
            p_wndWindow->ResetBuffer();
            p_wmgrWindows->Add("p_wndDebugConsoleWindow", p_wndWindow);
        }
    }

    // Start Handlers
    std::thread QueueHandlerTh(QueueHandler);
    std::thread TimerHandlerTh(TimerHandler);
    // Start Windows
    std::thread MainWindowTh(MainWindowHandler);
    std::thread InfoWindowTh(InfoWindowHandler);
    std::thread DebugConsoleWindowTh(DebugConsoleWindowHandler);

    while (1)
    {
        // loop start
        while (!bq_iUpdateEvents.empty())
        {
            switch (bq_iUpdateEvents.pop())
            {
            case UE_SCREENSIZE_UPDATE:
            {
                resize_term(LINES, COLS);
                p_wmgrWindows->UpdateScreenSize();
                break;
            }
            default:
                break;
            }
        }

        // process input
        std::queue<unsigned int> keys;
        while (!bq_iEvents.empty())
            keys.push(bq_iEvents.pop());

        // update events
        {
            p_wmgrWindows->BroadcastMessage(Msg{WM_UPDATE});
            p_wmgrWindows->BroadcastMessage(Msg{WM_PRESENT});

            AWindow *wndFrontWindow = nullptr; // Get Top Window
            if (p_wmgrWindows->GetFront(&wndFrontWindow))
            {
                for (std::queue<unsigned int> keys_ = keys; !keys_.empty(); keys_.pop())
                    p_wmgrWindows->SendMessage(wndFrontWindow, Msg{WM_KEY, keys.front()}); // key
            }
        }

        // update windows
        p_wmgrWindows->PresentWindows();

        // update screen
        {
            p_wmgrWindows->Flip();
            fcFrameCounter.count();
        }

        // fps control
        {
            frFrameRater.sleep();
        }
    }

    exit(0);
}

void ExitHandler()
{
    // release ncurses

    nocbreak();
    curs_set(TRUE);
    keypad(stdscr, FALSE); // = nokeypad()

    endwin();
}

void QueueHandler()
{
    while (1)
    {
        int key = wgetch(p_wndHostWindow);

        switch (key)
        {
        default:
            bq_iEvents.push(key);
            break;
        }
    }
}

void TimerHandler()
{
    while (1)
    {
        // TODO: Process in times
        if (p_wmgrWindows->NewScreenSize())
        {
            bq_iUpdateEvents.push(UE_SCREENSIZE_UPDATE);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
}

void MainWindowHandler()
{
    AWindow *p_wndMainWindow = nullptr;
    if (!p_wmgrWindows->GetWindow("p_wndMainWindow", &p_wndMainWindow))
        return;

    auto fnDrawGuiLocked = [&]() {
        p_wndMainWindow->Erase();
        p_wndMainWindow->Build();
        p_wndMainWindow->MVPrint(1, 1, "Press 'Enter' to refresh me!");
        p_wndMainWindow->Flip();
        p_wndMainWindow->RequestPresent();
    };

    fnDrawGuiLocked();
    while (1)
    {
        Msg msg;
        p_wndMainWindow->GetMessage(&msg);

        static bool c_s_bLocked = true;
        static bool c_s_bFloatInverter = false;

        double fScrFps, fWndFps, fWndReqFps;

        const auto fnUpdateFps = [&]() {
            fScrFps = fcFrameCounter.fps;
            fWndFps = p_wndMainWindow->fcWindowFrameCounter.fps;
            fWndReqFps = p_wndMainWindow->fcWindowReqFrameCounter.fps;
        };

        const auto fnDrawGui = [&]() {
            if (c_s_bLocked)
            {
                fnDrawGuiLocked();
                return;
            }

            p_wndMainWindow->Erase();
            p_wndMainWindow->Build();
            if (c_s_bFloatInverter)
            {
                p_wndMainWindow->MVPrint(1, 0, "Press 'Q' to exit!");
                p_wndMainWindow->MVPrint(3, 1, "Screen FPS: %d", (int)fScrFps);
                p_wndMainWindow->MVPrint(4, 1, "Window FPS: %d", (int)fWndFps);
                p_wndMainWindow->MVPrint(5, 1, "Window Requesting FPS: %d", (int)fWndReqFps);
                p_wndMainWindow->MVPrint(7, 1, "<AWSD For Moving>");
                p_wndMainWindow->MVPrint(8, 1, "<F For Float Inverting>");
            }
            else
            {
                p_wndMainWindow->MVPrint(1, 0, "Press 'Q' to exit!");
                p_wndMainWindow->MVPrint(3, 1, "Screen FPS: %f", fScrFps);
                p_wndMainWindow->MVPrint(4, 1, "Window FPS: %f", fWndFps);
                p_wndMainWindow->MVPrint(5, 1, "Window Requesting FPS: %f", fWndReqFps);
                p_wndMainWindow->MVPrint(7, 1, "<AWSD For Moving>");
                p_wndMainWindow->MVPrint(8, 1, "<F For Float Inverting>");
            }

            p_wndMainWindow->Flip();
            p_wndMainWindow->RequestPresent(); // Let Screen Present
        };

        switch (msg.u_iMessage)
        {
        case WM_UPDATE:
            break;

        case WM_KEY:
        {
            int key = msg.u_iParam;
            if (key == 'Q' || key == 'q')
            {
                exit(0);
            }

            if (key == 'F' || key == 'f')
            {
                c_s_bFloatInverter = !c_s_bFloatInverter;

                fnDrawGui();
            }

            // movement
            {
                bool moved = false;
                int x = p_wndMainWindow->iWindowPosX;
                int y = p_wndMainWindow->iWindowPosY;

                if (key == 'A' || key == 'a')
                {
                    --x;
                    moved = true;
                }
                if (key == 'W' || key == 'w')
                {
                    --y;
                    moved = true;
                }
                if (key == 'S' || key == 's')
                {
                    ++y;
                    moved = true;
                }
                if (key == 'D' || key == 'd')
                {
                    ++x;
                    moved = true;
                }

                if (moved)
                {
                    p_wndMainWindow->MoveWindow(y, x);

                    fnUpdateFps();
                    fnDrawGui();
                }
            }

            if (key == '\n')
            {
                if (c_s_bLocked)
                    c_s_bLocked = false;

                fnUpdateFps();
                fnDrawGui();
            }

            break;
        }

        case WM_SCREEN_RESIZE:
        {
            fnUpdateFps();
            fnDrawGui();
        }

        default:
            break;
        }

        continue;
    }
}

void InfoWindowHandler()
{
    AWindow *p_wndInfoWindow = nullptr;
    if (!p_wmgrWindows->GetWindow("p_wndInfoWindow", &p_wndInfoWindow))
        return;

    p_wndInfoWindow->fcWindowFrameCounter.noUpdateDelay = true;
    p_wndInfoWindow->fcWindowReqFrameCounter.noUpdateDelay = true;

    frame_rater<60> frFpsLimiter;

    double fScrFps, fWndFps, fWndReqFps;

    const auto fnUpdateFps = [&]() {
        fScrFps = fcFrameCounter.fps;
        fWndFps = p_wndInfoWindow->fcWindowFrameCounter.fps;
        fWndReqFps = p_wndInfoWindow->fcWindowReqFrameCounter.fps;
    };

    const auto fnDrawGui = [&]() {

        p_wndInfoWindow->Erase();
        p_wndInfoWindow->Build();
        p_wndInfoWindow->MVPrint(2, 1, "Screen FPS: %f", fScrFps);
        p_wndInfoWindow->MVPrint(3, 1, "Window FPS: %f", fWndFps);
        p_wndInfoWindow->MVPrint(4, 1, "Window Requesting FPS: %f", fWndReqFps);

        p_wndInfoWindow->Flip();
        p_wndInfoWindow->RequestPresent(); // Let Screen Present
    };

    while (1)
    {
        Msg msg;
        if (!p_wndInfoWindow->MessageEmpty())
        {
            p_wndInfoWindow->GetMessage(&msg);

            switch (msg.u_iMessage)
            {
            default:
                break;
            }
        }

        fnUpdateFps();
        fnDrawGui();

        frFpsLimiter.sleep();

        continue;
    }
}

void DebugConsoleWindowHandler()
{
    AWindow *p_wndDebugConsoleWindow = nullptr;
    if (!p_wmgrWindows->GetWindow("p_wndDebugConsoleWindow", &p_wndDebugConsoleWindow))
        return;

    while (1)
    {
        Msg msg;
        p_wndDebugConsoleWindow->GetMessage(&msg);

        double fScrFps, fWndFps, fWndReqFps;

        const auto fnUpdateFps = [&]() {
            fScrFps = fcFrameCounter.fps;
            fWndFps = p_wndDebugConsoleWindow->fcWindowFrameCounter.fps;
            fWndReqFps = p_wndDebugConsoleWindow->fcWindowReqFrameCounter.fps;
        };

        const auto fnDrawGui = [&]() {

            p_wndDebugConsoleWindow->Erase();
            p_wndDebugConsoleWindow->Build();
            p_wndDebugConsoleWindow->Move(1, 0);
            p_wndDebugConsoleWindow->Print("Screen FPS:\n%f\n", fScrFps);
            p_wndDebugConsoleWindow->Print("Window FPS:\n%f\n", fWndFps);
            p_wndDebugConsoleWindow->Print("Window Requesting FPS:\n%f\n", fWndReqFps);

            p_wndDebugConsoleWindow->Flip();
            p_wndDebugConsoleWindow->RequestPresent(); // Let Screen Present
        };

        switch (msg.u_iMessage)
        {
        case WM_UPDATE:
            break;

        case WM_PRESENT:
        {
            fnUpdateFps();
            fnDrawGui();

            break;
        }

        default:
            break;
        }

        continue;
    }
}
