/* Copyright (c) 2016, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */
#include "vjoystick.h"
#include "api/plugin-api.hpp"
#include "compat/math.hpp"

#include <cstring>
#include <QDebug>

#include <QPushButton>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

// required for api headers
#include <windows.h>

#undef PPJOY_MODE
#include <public.h>
#include <vjoyinterface.h>

#include <QDir>
#include <QSettings>

#define OPENTRACK_VJOYSTICK_ID 1

// Trackhat: vJoyInterface.dll has to match the installed vJoy driver. The SDK's copy that used to be
// shipped in modules/ corrupts the heap with driver 2.1.9.1, and this protocol is enabled by default.
// With MSVC the DLL is delay-loaded (see CMakeLists.txt), so load the driver's own copy before the first
// vJoy call. If there is none, no vJoy function may be called.
static bool load_vjoy_interface()
{
#ifdef _MSC_VER
    static const bool loaded = [] {
        if (GetModuleHandleW(L"vJoyInterface.dll"))
            return true;

        QStringList dirs;
        // The vJoy installer (Inno Setup) records where it installed to.
        const QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                                           "{8E31F76F-74C3-47F1-9550-E041EEDC5FBB}_is1"),
                            QSettings::Registry64Format);
        if (const QString dir = reg.value(QStringLiteral("InstallLocation")).toString(); !dir.isEmpty())
            dirs << dir;
        for (const char* var : { "ProgramW6432", "ProgramFiles" })
            if (const QString pf = qEnvironmentVariable(var); !pf.isEmpty())
                dirs << pf + QStringLiteral("/vJoy");

        const QString arch = sizeof(void*) == 8 ? QStringLiteral("x64") : QStringLiteral("x86");
        for (const QString& dir : dirs)
        {
            const QString path = QDir::toNativeSeparators(QDir(dir).filePath(arch + QStringLiteral("/vJoyInterface.dll")));
            if (LoadLibraryExW((const wchar_t*)path.utf16(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
            {
                qDebug() << "vjoystick: using" << path;
                return true;
            }
        }
        qDebug() << "vjoystick: no vJoyInterface.dll from an installed vJoy driver";
        return false;
    }();
    return loaded;
#else
    return true; // linked normally, shipped next to the module
#endif
}

const unsigned char vjoystick::axis_ids[6] =
{
    HID_USAGE_X,
    HID_USAGE_Y,
    HID_USAGE_Z,
    HID_USAGE_RX,
    HID_USAGE_RY,
    HID_USAGE_RZ,
//    HID_USAGE_SL0,
//    HID_USAGE_SL1,
//    HID_USAGE_WHL,
};

static constexpr double val_minmax[6] =
{
    50,
    50,
    50,
    180,
    180,
    180
};

bool vjoystick::init()
{
    if (!AcquireVJD(OPENTRACK_VJOYSTICK_ID))
        return false;

    unsigned cnt = 0;

    for (unsigned i = 0; i < axis_count; i++)
    {
        bool status = true;

        status &= !!GetVJDAxisExist(OPENTRACK_VJOYSTICK_ID, axis_ids[i]);
        status &= !!GetVJDAxisMin(OPENTRACK_VJOYSTICK_ID, axis_ids[i], &axis_min[i]);
        status &= !!GetVJDAxisMax(OPENTRACK_VJOYSTICK_ID, axis_ids[i], &axis_max[i]);

        if (!status)
        {
            axis_min[i] = 0;
            axis_max[i] = 0;
        }
        else
            cnt++;
    }

    if (!cnt)
    {
        RelinquishVJD(OPENTRACK_VJOYSTICK_ID);
        return false;
    }
    else
        return true;
}

int vjoystick::to_axis_value(unsigned axis_id, double val) const
{
    const double minmax = val_minmax[axis_id];
    const double min = axis_min[axis_id];
    const double max = axis_max[axis_id];

    return (int)(std::clamp((val+minmax) * max / (2*minmax) - min, min, max));
}

vjoystick::vjoystick() = default;
vjoystick::~vjoystick()
{
    if (status)
        RelinquishVJD(OPENTRACK_VJOYSTICK_ID);
}

module_status vjoystick::initialize()
{
    QString msg;

    if (!load_vjoy_interface() || !vJoyEnabled())
        msg = tr("vjoystick won't work without the driver installed.");
#if 0
    else if (WORD VerDll, VerDrv; !DriverMatch(&VerDll, &VerDrv))
        msg = tr("driver/SDK version mismatch (dll 0x%1, driver 0x%2)")
              .arg(QString::number(VerDll, 16), QString::number(VerDrv, 16));
#endif
    else
    {
        int code;
        switch (code = GetVJDStatus(OPENTRACK_VJOYSTICK_ID))
        {
        case VJD_STAT_OWN:
            msg = tr("BUG: handle leak.");
            break;
        case VJD_STAT_BUSY:
            msg = tr("Virtual joystick already in use.");
            break;
        case VJD_STAT_MISS:
            msg = tr("Device missing. Add joystick #1.");
            break;
        case VJD_STAT_UNKN:
            msg = tr("Unknown error.");
            break;
        default:
            msg = tr("Unknown error #%1.").arg(code);
            break;
        case VJD_STAT_FREE:
            // we're good
            status = true;
            break;
        }
    }

    // Trackhat: this protocol is on by default, so no modal error box when the driver is missing;
    // the reason is shown in Options > Protocols instead.
    if (!status)
        return error(msg);

    if (!status)
        return error(tr("Driver problem."));
    else
        return {};
}

void vjoystick::pose(const double *pose, const double*)
{
    if (first_run)
    {
        // Trackhat: only when initialize() succeeded; without the driver's DLL a vJoy call would crash.
        status = status && init();
        //status &= !!ResetVJD(OPENTRACK_VJOYSTICK_ID);
        first_run = false;
    }

    if (!status)
        return;

    for (unsigned i = 0; i < vjoystick::axis_count; i++)
    {
        if (axis_min[i] == axis_max[i])
            continue;

        int val = to_axis_value(i, pose[i]);
        SetAxis(val, OPENTRACK_VJOYSTICK_ID, vjoystick::axis_ids[i]);
    }
}

OPENTRACK_DECLARE_PROTOCOL(vjoystick, vjoystick_dialog, vjoystick_metadata)
