#include "key-capture.hpp"
#include "listener.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QObject>

namespace key_capture {

static QString tr(const char* s) { return QCoreApplication::translate("key_capture", s); }

void clear(key_opts& k)
{
    k.keycode = {};
    k.guid = {};
    k.button = -1;
}

QString describe(const key_opts& k)
{
    if (!k.guid->isEmpty())
    {
        const int btn = k.button & ~Qt::KeyboardModifierMask;
        const int mods = k.button & Qt::KeyboardModifierMask;
        QString mm;
        if (mods & Qt::ControlModifier) mm += QStringLiteral("Control+");
        if (mods & Qt::AltModifier) mm += QStringLiteral("Alt+");
        if (mods & Qt::ShiftModifier) mm += QStringLiteral("Shift+");
        const QString str = *k.guid == QStringLiteral("mouse") ? tr("Mouse %1")
                          : k.guid->startsWith(QStringLiteral("GI!")) ? tr("Gamepad button %1")
                          : tr("Joy button %1");
        return mm + str.arg(btn);
    }
    if (k.keycode->isEmpty())
        return tr("None");
    return *k.keycode;
}

bool capture(key_opts& k, const std::function<void(bool)>& pause_keybindings)
{
    clear(k);

    auto* dlg = new keyboard_listener;
    dlg->setAttribute(Qt::WA_DeleteOnClose, false);
    dlg->setWindowModality(Qt::ApplicationModal);
    {
        QObject ctx;
        QObject::connect(dlg, &keyboard_listener::key_pressed, &ctx, [&](const QKeySequence& s) {
            k.keycode = s.toString(QKeySequence::PortableText);
            k.guid = {};
            k.button = -1;
            dlg->close();
        });
        QObject::connect(dlg, &keyboard_listener::joystick_button_pressed, &ctx, [&](const QString& guid, int idx, bool held) {
            if (held)
                return;
            k.guid = guid;
            k.keycode = {};
            k.button = idx;
            dlg->close();
        });
        if (pause_keybindings)
            pause_keybindings(true);
        dlg->exec();
        if (pause_keybindings)
            pause_keybindings(false);
    }
    delete dlg;

    // same check as Options > Shortcuts: a non-printable key code is a failed capture
    for (const QChar& c : *k.keycode)
        if (!c.isPrint())
        {
            clear(k);
            return false;
        }
    return !k.keycode->isEmpty() || !k.guid->isEmpty();
}

} // ns key_capture
