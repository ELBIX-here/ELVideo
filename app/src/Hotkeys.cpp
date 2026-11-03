#include "Hotkeys.h"

#include <QKeySequence>

#include <spdlog/spdlog.h>

namespace {

constexpr wchar_t kWindowClass[] = L"ELVideoMessageWindow";
constexpr UINT kShowMessage = WM_APP + 1;

// Qt key -> Windows virtual key for the keys a global hotkey can use; 0 otherwise.
UINT virtualKey(int key) {
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        return static_cast<UINT>(key);  // same codes as VK_A.. / VK_0..
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return VK_F1 + static_cast<UINT>(key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    default: return 0;
    }
}

UINT modifierFlags(Qt::KeyboardModifiers mods) {
    UINT flags = 0;
    if (mods & Qt::ControlModifier) flags |= MOD_CONTROL;
    if (mods & Qt::ShiftModifier) flags |= MOD_SHIFT;
    if (mods & Qt::AltModifier) flags |= MOD_ALT;
    if (mods & Qt::MetaModifier) flags |= MOD_WIN;
    return flags;
}

}  // namespace

Hotkeys::Hotkeys(QObject* parent) : QObject(parent) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Hotkeys::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, kWindowClass, L"ELVideo", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd_) {
        spdlog::error("cannot create hotkey window: {}", GetLastError());
        return;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

Hotkeys::~Hotkeys() {
    if (!hwnd_) return;
    for (int id : {StartStop, Pause})
        if (registered_[id]) UnregisterHotKey(hwnd_, id);
    DestroyWindow(hwnd_);
}

bool Hotkeys::activateRunningInstance() {
    const HWND other = FindWindowExW(HWND_MESSAGE, nullptr, kWindowClass, nullptr);
    if (!other) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);  // we got the user's click; let the other process take focus
    return PostMessageW(other, kShowMessage, 0, 0) != FALSE;
}

LRESULT CALLBACK Hotkeys::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Hotkeys*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        if (msg == WM_HOTKEY) {
            emit self->activated(static_cast<int>(wp));
            return 0;
        }
        if (msg == kShowMessage) {
            emit self->showRequested();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Hotkeys::setSequences(const QString& startStop, const QString& pause) {
    if (sequences_[StartStop] == startStop && sequences_[Pause] == pause) return;
    sequences_[StartStop] = startStop;
    sequences_[Pause] = pause;
    apply();
}

void Hotkeys::setSuspended(bool suspended) {
    if (suspended == suspended_) return;
    suspended_ = suspended;
    apply();
    emit suspendedChanged();
}

void Hotkeys::apply() {
    if (!hwnd_) return;
    for (int id : {StartStop, Pause}) {
        if (registered_[id]) UnregisterHotKey(hwnd_, id);
        registered_[id] = false;
    }
    if (!suspended_) {
        errors_[StartStop] = registerOne(StartStop, sequences_[StartStop]);
        errors_[Pause] = sequences_[Pause] == sequences_[StartStop] && !sequences_[Pause].isEmpty()
                             ? tr("시작/정지와 같은 단축키입니다.")
                             : registerOne(Pause, sequences_[Pause]);
    }
    emit errorsChanged();
}

QString Hotkeys::registerOne(Id id, const QString& sequence) {
    if (sequence.isEmpty()) return {};  // disabled
    const QKeySequence keys = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    if (keys.isEmpty()) return tr("알 수 없는 단축키입니다.");
    const QKeyCombination combo = keys[0];
    const UINT vk = virtualKey(combo.key());
    if (!vk) return tr("이 키는 단축키로 쓸 수 없습니다.");

    if (!RegisterHotKey(hwnd_, id, modifierFlags(combo.keyboardModifiers()) | MOD_NOREPEAT, vk)) {
        spdlog::warn("RegisterHotKey({}) failed: {}", sequence.toStdString(), GetLastError());
        return tr("다른 프로그램이 이미 쓰고 있습니다.");
    }
    registered_[id] = true;
    return {};
}

QString Hotkeys::sequenceFromKey(int key, int modifiers) const {
    if (!virtualKey(key)) return {};
    const auto mods = Qt::KeyboardModifiers(modifiers) &
                      (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    // A bare letter as a global hotkey would swallow normal typing everywhere.
    const bool functionKey = key >= Qt::Key_F1 && key <= Qt::Key_F24;
    if (!mods && !functionKey && key != Qt::Key_Print && key != Qt::Key_Pause && key != Qt::Key_ScrollLock) return {};
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}
