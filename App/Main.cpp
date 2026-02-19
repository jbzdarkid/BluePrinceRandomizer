#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"

#include <unordered_set>
#include <map>
#include "RoomList.h"

#define SET_SEED_UNKNOWN            0x401
#define SET_SEED_DONOTTAMPER        0x402
#define SET_SEED_BIRDPATHING        0x403
#define SET_SEED_RARITY             0x404
#define SET_SEED_DRAFTING           0x405
#define SET_SEED_ITEMS              0x406
#define SET_SEED_DOGSWAPPER         0x407
#define SET_SEED_TRADING            0x408
#define SET_SEED_DERIGIBLOCK        0x409
#define SET_SEED_SLOTMACHINE        0x40A
#define SET_SEED_ALL                0x40B


#define SET_BEHAVIOR_UNKNOWN        0x411
#define SET_BEHAVIOR_DONOTTAMPER    0x412
#define SET_BEHAVIOR_BIRDPATHING    0x413
#define SET_BEHAVIOR_RARITY         0x414
#define SET_BEHAVIOR_DRAFTING       0x415
#define SET_BEHAVIOR_ITEMS          0x416
#define SET_BEHAVIOR_DOGSWAPPER     0x417
#define SET_BEHAVIOR_TRADING        0x418
#define SET_BEHAVIOR_DERIGIBLOCK    0x419
#define SET_BEHAVIOR_SLOTMACHINE    0x41A
#define SET_BEHAVIOR_ALL            0x41B

#define LOAD_DECKLISTS              0x420
#define FORCE_SLOT_1                0x421
#define FORCE_SLOT_2                0x422
#define FORCE_SLOT_3                0x423

// Globals
HWND g_hwnd;
HWND g_seedInputs[Trainer::RngClass::NumEntries + 1] = {};
HWND g_behaviorInputs[Trainer::RngClass::NumEntries + 1] = {};
HWND g_deckLists[3] = {};
HWND g_forcedSlots[3] = {};
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;

#define SetWindowTextA(...) static_assert(false, "Call SetStringText instead of SetWindowTextA");
#define SetWindowTextW(...) static_assert(false, "Call SetStringText instead of SetWindowTextW");
#undef SetWindowText
#define SetWindowText(...) static_assert(false, "Call SetStringText instead of SetWindowText");

void SetStringText(HWND hwnd, const std::string& text) {
    static std::unordered_map<HWND, std::string> hwndText;
    auto search = hwndText.find(hwnd);
    if (search != hwndText.end()) {
        if (search->second == text) return;
        search->second = text;
    } else {
        hwndText[hwnd] = text;
    }

#pragma push_macro("SetWindowTextA")
#undef SetWindowTextA
    SetWindowTextA(hwnd, text.c_str());
#pragma pop_macro("SetWindowTextA")
}

void SetStringText(HWND hwnd, const std::wstring& text) {
    static std::unordered_map<HWND, std::wstring> hwndText;
    auto search = hwndText.find(hwnd);
    if (search != hwndText.end()) {
        if (search->second == text) return;
        search->second = text;
    } else {
        hwndText[hwnd] = text;
    }

#pragma push_macro("SetWindowTextW")
#undef SetWindowTextW
    SetWindowTextW(hwnd, text.c_str());
#pragma pop_macro("SetWindowTextW")
}

std::wstring GetWindowString(HWND hwnd) {
    SetLastError(0); // GetWindowTextLength does not clear LastError.
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length, L'\0');
    length = GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size() + 1)); // Length includes the null terminator
    text.resize(length);
    return text;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            if (g_trainer) {
                auto trainer = g_trainer;
                g_trainer = nullptr; // Close the trainer, which undoes any modifications to the game.

                // Wait to actually quit until all background threads have finished their work.
                // Note that we do need to pump messages here, since said work may require the message pump,
                // which we are currently holding hostage.
                while (trainer.use_count() > 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    MSG msg;
                    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
        {
            RECT rc;
            ::GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(255,255,255));
            FillRect((HDC)wParam, &rc, brush);
            DeleteObject(brush);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            // Get rid of the gross gray background. https://stackoverflow.com/a/4495814
            SetTextColor((HDC)wParam, RGB(0, 0, 0));
            SetBkColor((HDC)wParam, RGB(255, 255, 255));
            SetBkMode((HDC)wParam, OPAQUE);
            static HBRUSH s_solidBrush = CreateSolidBrush(RGB(255, 255, 255));
            return (LRESULT)s_solidBrush;
        case WM_TIMER:
        case WM_COMMAND:
            // All commands should execute on a background thread, to avoid hanging the UI.
            std::thread t([trainer = g_trainer, wParam, lParam] {
#pragma warning (suppress: 4101)
                void* g_trainer; // Prevent access to the global variable in this scope
#pragma warning (default: 4101)
                if (!trainer) return;
                SetCurrentThreadName(L"Command Helper");

                WORD command = LOWORD(wParam);
                if (command >= SET_SEED_UNKNOWN && command < SET_SEED_UNKNOWN + Trainer::RngClass::NumEntries) {
                    Trainer::RngClass rngClass = (Trainer::RngClass)(command - SET_SEED_UNKNOWN);
                    __int64 seed = std::stoull(GetWindowString(g_seedInputs[rngClass]));
                    trainer->SetSeed(rngClass, seed);
                } else if (command == SET_SEED_ALL) {
                    __int64 seed = std::stoull(GetWindowString(g_seedInputs[Trainer::RngClass::NumEntries]));
                    trainer->SetAllSeeds(seed);
                    for (HWND hwnd : g_seedInputs) SetStringText(hwnd, std::to_string(seed));
                } else if (command >= SET_BEHAVIOR_UNKNOWN && command < SET_BEHAVIOR_UNKNOWN + Trainer::RngClass::NumEntries) {
                    Trainer::RngClass rngClass = (Trainer::RngClass)(command - SET_BEHAVIOR_UNKNOWN);
                    std::wstring behavior = GetWindowString(g_behaviorInputs[rngClass]);
                    if (behavior == L"Constant") trainer->SetRngBehavior(rngClass, Trainer::RngBehavior::Constant);
                    else if (behavior == L"Increment") trainer->SetRngBehavior(rngClass, Trainer::RngBehavior::Increment);
                    else if (behavior == L"Randomize") trainer->SetRngBehavior(rngClass, Trainer::RngBehavior::Randomize);
                    else {
                        MessageBoxW(g_hwnd, L"Valid RNG behaviors are:\nConstant, Increment, Randomize", L"Invalid RNG behavior", MB_TASKMODAL | MB_ICONHAND | MB_OK | MB_SETFOREGROUND);
                        return;
                    }
                } else if (command == SET_BEHAVIOR_ALL) {
                    std::wstring behavior = GetWindowString(g_behaviorInputs[Trainer::RngClass::NumEntries]);
                    if (behavior == L"Constant") trainer->SetAllBehaviors(Trainer::RngBehavior::Constant);
                    else if (behavior == L"Increment") trainer->SetAllBehaviors(Trainer::RngBehavior::Increment);
                    else if (behavior == L"Randomize") trainer->SetAllBehaviors(Trainer::RngBehavior::Randomize);
                    else {
                        MessageBoxW(g_hwnd, L"Valid RNG behaviors are:\nConstant, Increment, Randomize", L"Invalid RNG behavior", MB_TASKMODAL | MB_ICONHAND | MB_OK | MB_SETFOREGROUND);
                        return;
                    }
                    for (HWND hwnd : g_behaviorInputs) SetStringText(hwnd, behavior);
                } else if (command == LOAD_DECKLISTS) {
                    std::vector<std::vector<std::wstring>> decks = trainer->GetDecks();
                    int i = 0;
                    for (const auto& deck : decks) {
                        std::wstring list;
                        for (const std::wstring& card : decks[i]) {
                            if (card[0] == L'<') { // Weirdly, some cards have color labels. Remove them.
                                list += card.substr(15, card.size() - 23) + L'\n';
                            } else {
                                list += card + L'\n';
                            }
                        }
                        SetStringText(g_deckLists[i], list);
                        i++;
                        if (i >= 3) break;
                    }
                } else if (command >= FORCE_SLOT_1 && command <= FORCE_SLOT_3) {
                    auto action = HIWORD(wParam);
                    int slot = command - FORCE_SLOT_1 + 1;
                    if (action == CBN_SELCHANGE) {
                        int selectedIndex = (int)SendMessage((HWND)lParam, (UINT)CB_GETCURSEL, NULL, NULL);
                        if (selectedIndex == 0) {
                            trainer->ForceRoomDraft(L"", slot);
                        } else {
                            const std::wstring internalName = ROOM_NAMES[selectedIndex - 1].second;
                            trainer->ForceRoomDraft(internalName, slot);
                        }
                    }
                }
            });
            t.detach();
            break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

HWND CreateLabel(int& x, int y, int width, int height, LPCWSTR text = L"", __int64 message = 0) {
    HWND label = CreateWindow(L"STATIC", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOTIFY,
        x, y, width, height,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    x += width;
    return label;
}

HWND CreateLabel(int& x, int y, int width, LPCWSTR text, __int64 message = 0) {
    return CreateLabel(x, y, width, 16, text, message);
}

HWND CreateButton(int& x, int& y, int width, LPCWSTR text, __int64 message) {
    HWND button = CreateWindow(L"BUTTON", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, width, 26,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    x += width;
    return button;
}

HWND CreateCheckbox(int& x, int& y, __int64 message) {
    HWND checkbox = CreateWindow(L"BUTTON", NULL,
        WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
        x, y + 2, 12, 12,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    return checkbox;
}

// The same arguments as Button.
std::pair<HWND, HWND> CreateLabelAndCheckbox(int x, int& y, int width, LPCWSTR text, __int64 message) {
    // We need a distinct message (HMENU) for the label so that when we call CheckDlgButton it targets the checkbox, not the label.
    // However, we only use the low word (bottom 2 bytes) for logic, so we can safely modify the high word to make it distinct.
    auto label = CreateLabel(x, y, width, text, message + 0x10000);

    auto checkbox = CreateCheckbox(x, y, message);
    return {label, checkbox};
}

HWND CreateText(int& x, int& y, int width, LPCWSTR defaultText = L"", __int64 message = NULL) {
    HWND text = CreateWindow(MSFTEDIT_CLASS, defaultText,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER,
        x, y, width, 26,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    x += width;
    return text;
}

HWND CreateDropdown(int& x, int& y, int width, const std::vector<std::wstring>& options, __int64 message) {
    HWND hwnd = CreateWindow(WC_COMBOBOX, L"", 
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE | WS_VSCROLL,
        x, y, width - 5, 100,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    x += width;

    for (const auto& option : options) {
        SendMessage(hwnd, CB_ADDSTRING, NULL, (LPARAM)option.data()); 
    }
    SendMessage(hwnd, CB_SETCURSEL, (WPARAM)0, NULL);

    return hwnd;
}

void CreateComponents() {
    int y = 10;
#if DERANDOMIZE
    std::vector<const wchar_t*> categories = {L"Unknown", L"DoNotTamper", L"BirdPathing", L"Rarity", L"Drafting", L"Items", L"DogSwapper", L"Trading", L"Derigiblock", L"SlotMachine", L"All"};
    for (int i = 0; i < categories.size(); i++) {
        int x = 10;
        CreateLabel(x, y + 5, 100, categories[i]);
        CreateLabel(x, y + 5, 40, L"Seed:");
        g_seedInputs[i] = CreateText(x, y, 100, L"42");
        CreateButton(x, y, 40, L"Set", SET_SEED_UNKNOWN + i);

        x += 10;

        CreateLabel(x, y + 5, 65, L"Behavior:");
        g_behaviorInputs[i] = CreateText(x, y, 80, L"Constant");
        CreateButton(x, y, 40, L"Set", SET_BEHAVIOR_UNKNOWN + i);

        y += 30;
    }
    y += 30;
#endif

    constexpr int deckWidth = 160;
    int x = 10;
    CreateLabel(x, y, deckWidth, L"Deck 1");
    CreateLabel(x, y, deckWidth, L"Deck 2");
    CreateLabel(x, y, deckWidth, L"Deck 3");
    y += 20;

    // TODO: Dropdowns?
    x = 10;
    std::vector<std::wstring> dropdownOptions;
    dropdownOptions.push_back(L"(no override)");
    for (const auto& [key, value] : ROOM_NAMES) dropdownOptions.push_back(key);

    g_forcedSlots[0] = CreateDropdown(x, y, deckWidth, dropdownOptions, FORCE_SLOT_1);
    g_forcedSlots[0] = CreateDropdown(x, y, deckWidth, dropdownOptions, FORCE_SLOT_2);
    g_forcedSlots[0] = CreateDropdown(x, y, deckWidth, dropdownOptions, FORCE_SLOT_3);
    y += 25;

    x = 10;
    for (int i = 0; i < 3; i++) {
        g_deckLists[i] = CreateWindow(L"STATIC", L"",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOTIFY,
            x + i * deckWidth, y, 120, 300,
            g_hwnd, (HMENU)NULL, g_hInstance, NULL);
    }
    y += 30;

    // SetTimer(g_hwnd, LOAD_DECKLISTS, 1000, (TIMERPROC)NULL); // Reload decklists every second
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return hr;
    LoadLibrary(L"Msftedit.dll");
    WNDCLASS wndClass = {
        CS_HREDRAW | CS_VREDRAW,
        WndProc,
        0,
        0,
        hInstance,
        NULL,
        NULL,
        NULL,
        WINDOW_CLASS,
        WINDOW_CLASS,
    };
    RegisterClass(&wndClass);

    int height = 400;
#if DERANDOMIZE
    height += 300;
#endif

    RECT rect;
    GetClientRect(GetDesktopWindow(), &rect);
    g_hwnd = CreateWindow(WINDOW_CLASS, WINDOW_TITLE,
        WS_SYSMENU | WS_MINIMIZEBOX,
        rect.right - 750, 200, 650, height,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    g_hInstance = hInstance;

    CreateComponents();

    auto bluePrince = std::make_shared<Memory>(L"BLUE PRINCE.exe");
    g_trainer = Trainer::Create(bluePrince);
    if (!g_trainer) {
        MessageBoxW(g_hwnd, L"Game is not running or already injected", L"Trainer failed to start", MB_TASKMODAL | MB_ICONHAND | MB_OK | MB_SETFOREGROUND);
    } else {
#if DERANDOMIZE
        g_trainer->SetAllSeeds(42); // A seed value of 0 will stay stuck at 0, I think.
        g_trainer->SetAllBehaviors(Trainer::RngBehavior::Constant);
#endif
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // g_trainer is freed during WM_DESTROY (to delay in case there are background tasks)

    CoUninitialize();
    return (int)msg.wParam;
}
