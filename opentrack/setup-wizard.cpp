#include "setup-wizard.hpp"

#include "gui/key-capture.hpp"
#include "options/globals.hpp"
#include "video/camera.hpp"
#include "spline/spline.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QSlider>
#include <QStyle>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace options;
using namespace options::globals;

static constexpr const char* done_key = "trackhat-setup-wizard-done";

// The step 2 calibration needs this much head movement (degrees, max - min of the raw pose).
static constexpr double calib_yaw_span = 50, calib_pitch_span = 30;
static constexpr unsigned calib_max_samples = 3000;

// ---------------------------------------------------------------------------------------------
// curve preview: the selected preset drawn the way the Mapping window shows it (input -> output)

class curve_preview final : public QWidget
{
public:
    explicit curve_preview(QWidget* parent) : QWidget(parent) { setMinimumSize(300, 150); }

    void set(QList<QPointF> pts, double max_in, double max_out, const QString& unit)
    {
        points = std::move(pts); in = max_in; out = max_out; unit_ = unit;
        // sample opentrack's own curve through the points, so this matches the Mapping window
        spline sp;
        for (const QPointF& pt : points)
            sp.add_point(pt);
        curve.clear();
        constexpr int n = 240;
        for (int i = 0; i <= n; i++)
        {
            const double x = in * i / n;
            curve.push_back(QPointF(x, points.isEmpty() ? x : sp.get_value_no_save(x)));
        }
        update();
    }

protected:
    static int divisions(double range)
    {
        for (int d : { 4, 3, 5, 6 })
            if (std::fabs(range / d - std::round(range / d)) < 1e-6)
                return d;
        return 4;
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(34, 8, -10, -22);
        p.fillRect(rect(), QColor(0x11, 0x11, 0x16));
        p.fillRect(frame, QColor(0x1C, 0x17, 0x30));

        QPen grid(QColor(167, 139, 250, 60));
        grid.setWidth(1);
        QFont f = font(); f.setPointSizeF(7.5); p.setFont(f);
        const int dx = divisions(in), dy = divisions(out);
        for (int i = 0; i <= dx; i++)
        {
            const double x = frame.left() + frame.width() * i / dx;
            p.setPen(grid);
            p.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
            p.setPen(QColor(0x9A, 0x9A, 0xA8));
            p.drawText(QRectF(x - 20, frame.bottom() + 3, 40, 14), Qt::AlignCenter, QString::number(qRound(in * i / dx)));
        }
        for (int i = 0; i <= dy; i++)
        {
            const double y = frame.bottom() - frame.height() * i / dy;
            p.setPen(grid);
            p.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
            p.setPen(QColor(0x9A, 0x9A, 0xA8));
            p.drawText(QRectF(0, y - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(qRound(out * i / dy)));
        }
        p.setPen(QColor(0x9A, 0x9A, 0xA8));
        p.drawText(QRectF(frame.right() - 160, frame.top() + 2, 156, 14), Qt::AlignRight,
                   setup_wizard::tr("head %1 -> game %1").arg(unit_));

        auto map = [&](double x, double y) {
            return QPointF(frame.left() + frame.width() * std::clamp(x / in, 0., 1.),
                           frame.bottom() - frame.height() * std::clamp(y / out, 0., 1.));
        };
        // 1:1 reference line
        p.setPen(QPen(QColor(120, 120, 130, 110), 1, Qt::DashLine));
        p.drawLine(map(0, 0), map(std::min(in, out), std::min(in, out)));

        QPainterPath path(map(0, 0));
        for (const QPointF& pt : curve)
            path.lineTo(map(pt.x(), pt.y()));
        p.setPen(QPen(QColor(0xE0, 0x2E, 0x2E), 2.2));
        p.drawPath(path);
        p.setBrush(QColor(0x9A, 0x9A, 0xA8));
        p.setPen(Qt::NoPen);
        for (const QPointF& pt : points)
            p.drawEllipse(map(pt.x(), pt.y()), 3.2, 3.2);
    }

private:
    QList<QPointF> points, curve;
    double in = 180, out = 180;
    QString unit_;
};

// ---------------------------------------------------------------------------------------------
// presets

const std::array<setup_wizard::preset, 4>& setup_wizard::presets()
{
    // Curves supplied by the owner (2026-11-18), measured from his Mapping window screenshots. The small
    // first point of each was dropped on 2026-11-19 so they start straight from 0; deadzones are step 6.
    static const std::array<preset, 4> ret {{
        { QT_TR_NOOP("1:1"),    {} },
        { QT_TR_NOOP("Mild"),   { { 25, 31.5 }, { 100, 180 } } },
        { QT_TR_NOOP("Medium"), { { 21, 35 }, { 63.5, 180 } } },
        { QT_TR_NOOP("Strong"), { { 15.5, 39.5 }, { 40.5, 180 } } },
    }};
    return ret;
}

const std::array<int, 4>& setup_wizard::deadzones()
{
    static const std::array<int, 4> ret { 0, 10, 20, 30 };
    return ret;
}

QList<QPointF> setup_wizard::fit_curve(const preset& p, int deadzone_percent, double max_in, double max_out)
{
    std::vector<QPointF> pts = p.points;
    // the deadzone is a percentage of the preset's bend (its first point); 1:1 has none, so 20 deg / cm
    const double bend = pts.empty() ? 20 : pts.front().x();
    if (pts.empty())
    {
        // 1:1 up to the end of the axis range
        const double m = std::min(max_in, max_out);
        pts.push_back(QPointF(m, m));
    }
    if (deadzone_percent > 0)
        pts.insert(pts.begin(), QPointF(std::round(bend * deadzone_percent / 10.) / 10, 0));

    QList<QPointF> ret;
    QPointF prev(0, 0);
    for (const QPointF& pt : pts)
    {
        if (pt.x() <= max_in && pt.y() <= max_out)
        {
            ret.push_back(pt);
            prev = pt;
            continue;
        }
        // the segment prev -> pt leaves the range: end the curve where it does (straight-line estimate)
        const double tx = pt.x() > max_in ? (max_in - prev.x()) / (pt.x() - prev.x()) : 1;
        const double ty = pt.y() > max_out ? (max_out - prev.y()) / (pt.y() - prev.y()) : 1;
        const double t = std::clamp(std::min(tx, ty), 0., 1.);
        const QPointF end(prev.x() + t * (pt.x() - prev.x()), prev.y() + t * (pt.y() - prev.y()));
        ret.push_back(QPointF(std::round(end.x() * 10) / 10, std::round(end.y() * 10) / 10));
        break;
    }
    return ret;
}

// ---------------------------------------------------------------------------------------------

bool setup_wizard::done()
{
    return with_global_settings_object([](QSettings& s) { return s.value(done_key, false).toBool(); });
}

void setup_wizard::set_done(bool value)
{
    with_global_settings_object([value](QSettings& s) { s.setValue(done_key, value); mark_global_ini_modified(); });
}

static QLabel* label(const QString& text, const char* object_name = nullptr, QWidget* parent = nullptr)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    if (object_name)
        l->setObjectName(object_name);
    return l;
}

static QPushButton* primary(const QString& text)
{
    auto* b = new QPushButton(text);
    b->setObjectName("wizardPrimary");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

setup_wizard::setup_wizard(hooks h_, QWidget* parent) :
    QDialog(parent),
    h(std::move(h_)),
    nn_bundle(make_bundle("neuralnet-tracker")),
    off_fwd(nn_bundle, "offset-fwd", -70),
    off_up(nn_bundle, "offset-up", -25),
    off_right(nn_bundle, "offset-right", 0)
{
    setWindowTitle(tr("TrackHat setup"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(false);
    // fixed size: the pages are laid out for it (larger text, see midnight.qss)
    setFixedSize(900, 800);

    static const char* const spline_names[6] = { "spline-X", "spline-Y", "spline-Z", "spline-yaw", "spline-pitch", "spline-roll" };
    for (int i = 0; i < 6; i++)
    {
        curves[i].b = make_bundle(spline_names[i]);
        curves[i].points = std::make_unique<value<QList<QPointF>>>(curves[i].b, "points", QList<QPointF>{});
        curves[i].axis = Axis(i);
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 16);
    root->setSpacing(12);

    step_label = label({}, "wizardStep");
    root->addWidget(step_label);

    pages = new QStackedWidget(this);
    root->addWidget(pages, 1);

    nav = new QWidget(this);
    auto* nav_l = new QHBoxLayout(nav);
    nav_l->setContentsMargins(0, 0, 0, 0);
    auto* skip = new QPushButton(tr("Exit setup"), nav);
    skip->setToolTip(tr("Close the setup. You can run it again from Options."));
    back_button = new QPushButton(tr("Back"), nav);
    next_button = primary(tr("Next"));
    nav_l->addWidget(skip);
    nav_l->addStretch(1);
    nav_l->addWidget(back_button);
    nav_l->addWidget(next_button);
    root->addWidget(nav);

    connect(skip, &QPushButton::clicked, this, &setup_wizard::skip_all);
    connect(back_button, &QPushButton::clicked, this, &setup_wizard::back);
    connect(next_button, &QPushButton::clicked, this, &setup_wizard::next);

    build_welcome();
    build_sensor();
    build_calibration();
    build_hotkeys();
    build_presets(true);
    build_presets(false);
    build_deadzone();

    connect(&sensor_timer, &QTimer::timeout, this, &setup_wizard::refresh_sensor_status);
    sensor_timer.setInterval(2500);
    connect(&calib_timer, &QTimer::timeout, this, &setup_wizard::calibration_step);
    calib_timer.setInterval(33);

    go_to(p_welcome);
}

setup_wizard::~setup_wizard()
{
    if (calibrating)
        calibration_restore();
}

// ---------------------------------------------------------------------------------------------
// pages

void setup_wizard::build_welcome()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setSpacing(14);
    l->addStretch(1);
    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(QStringLiteral(":/images/setup-logo.png")));
    l->addWidget(logo, 0, Qt::AlignLeft);
    l->addSpacing(6);
    l->addWidget(label(tr("Welcome to TrackHat"), "wizardTitle"));
    l->addWidget(label(tr("This short setup gets your head tracking working well in about five minutes:")));
    l->addWidget(label(tr("<ol style='margin-left:-20px'>"
                          "<li>Place the TrackHat sensor</li>"
                          "<li>Calibrate your head center</li>"
                          "<li>Choose your center and pause hotkeys</li>"
                          "<li>Pick how far the view turns when you turn your head</li>"
                          "<li>Pick how far the view moves when you lean</li>"
                          "<li>Choose your deadzones</li></ol>")));
    l->addWidget(label(tr("Each step can be changed later, and you can run this setup again from Options."), "wizardMuted"));
    l->addSpacing(8);
    auto* row = new QHBoxLayout;
    auto* start = primary(tr("Start setup"));
    auto* skip = new QPushButton(tr("Skip setup"));
    skip->setToolTip(tr("Use the default settings. You can run the setup later from Options."));
    row->addWidget(start);
    row->addWidget(skip);
    row->addStretch(1);
    l->addLayout(row);
    l->addStretch(2);
    connect(start, &QPushButton::clicked, this, [this] { go_to(p_sensor); });
    connect(skip, &QPushButton::clicked, this, &setup_wizard::skip_all);
    pages->addWidget(w);
}

void setup_wizard::build_sensor()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setSpacing(10);
    l->addWidget(label(tr("Connect and place the sensor"), "wizardHeading"));
    l->addWidget(label(tr("Plug the TrackHat sensor V2 into a USB port on your PC.")));

    sensor_status = label({}, "wizardWarn");
    l->addWidget(sensor_status);

    l->addWidget(label(tr("Put the sensor <b>above or below your monitor, whichever is closer to the height of your eyes</b>. "
                          "For most people that is on top of the monitor. Point it straight at your face.")));

    auto* imgs = new QHBoxLayout;
    imgs->setSpacing(14);
    struct { const char* image; const char* caption; } const pics[] = {
        { ":/images/setup-sensor-top.png",
          QT_TR_NOOP("Eyes nearer the top of the monitor: put the sensor on top (most people).") },
        { ":/images/setup-sensor-bottom.png",
          QT_TR_NOOP("Eyes nearer the bottom of the monitor: put the sensor below it.") },
    };
    for (const auto& pic : pics)
    {
        auto* col = new QVBoxLayout;
        auto* img = new QLabel;
        img->setObjectName("wizardImage");
        img->setPixmap(QPixmap(QString::fromLatin1(pic.image)).scaledToHeight(220, Qt::SmoothTransformation));
        img->setAlignment(Qt::AlignCenter);
        img->setFixedSize(img->pixmap().size() + QSize(8, 8));
        col->addWidget(img, 0, Qt::AlignHCenter);
        auto* cap = label(tr(pic.caption), "wizardMuted");
        cap->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        // no alignment flag: the label then takes the column width and its wrapped height is laid out fully
        col->addWidget(cap);
        col->addStretch(1);
        imgs->addLayout(col, 1);
    }
    l->addLayout(imgs);
    l->addWidget(label(tr("The green line is the better position: the sensor sees your face from closer to straight on."), "wizardMuted"));
    l->addStretch(1);
    pages->addWidget(w);
}

void setup_wizard::build_calibration()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setSpacing(10);
    l->addWidget(label(tr("Calibrate your head center"), "wizardHeading"));
    l->addWidget(label(tr("Your head turns around a point in your neck, not around your face. "
                          "This step measures where that point is, so the view turns naturally and doesn't slide "
                          "sideways when you look around.")));
    l->addWidget(label(tr("<b>Sit the way you normally play</b> and keep your shoulders still. When you press Start:"
                          "<ol style='margin-left:-20px'>"
                          "<li><b>Turn your head slowly left and right</b>, as far as you comfortably can, a few times.</li>"
                          "<li><b>Nod slowly up and down</b> a few times.</li></ol>"
                          "Don't tilt your head to the side. It finishes by itself when it has enough movement.")));

    auto* card = new QWidget;
    card->setObjectName("wizardCard");
    card->setAttribute(Qt::WA_StyledBackground);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(14, 12, 14, 12);
    auto add_bar = [&](const QString& text) {
        auto* row = new QHBoxLayout;
        auto* t = new QLabel(text);
        t->setMinimumWidth(170);
        auto* bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFormat(QStringLiteral("%p%"));
        row->addWidget(t);
        row->addWidget(bar, 1);
        cl->addLayout(row);
        return bar;
    };
    yaw_bar = add_bar(tr("Turn left and right"));
    pitch_bar = add_bar(tr("Nod up and down"));
    calib_status = label({}, "wizardMuted");
    cl->addWidget(calib_status);
    calib_result = label({});
    cl->addWidget(calib_result);
    l->addWidget(card);

    auto* row = new QHBoxLayout;
    calib_button = primary(tr("Start calibration"));
    calib_defaults_button = new QPushButton(tr("Use the default offset"));
    calib_defaults_button->setToolTip(tr("Forward -70 mm, up -25 mm, right 0 mm. Works for most people."));
    row->addWidget(calib_button);
    row->addWidget(calib_defaults_button);
    row->addStretch(1);
    l->addLayout(row);
    l->addStretch(1);

    connect(calib_button, &QPushButton::clicked, this, [this] {
        if (calibrating)
            calibration_stop(false);
        else
            calibration_start();
    });
    connect(calib_defaults_button, &QPushButton::clicked, this, [this] {
        if (calibrating)
            calibration_stop(false);
        off_fwd = -70; off_up = -25; off_right = 0;
        nn_bundle->save();
        calibrated = true;
        calib_result->setObjectName("wizardOk");
        calib_result->setText(tr("Using the default offset: forward -70 mm, up -25 mm, right 0 mm."));
        calib_result->style()->unpolish(calib_result); calib_result->style()->polish(calib_result);
        update_nav();
    });
    pages->addWidget(w);
}

void setup_wizard::build_hotkeys()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setSpacing(10);
    l->addWidget(label(tr("Set your hotkeys"), "wizardHeading"));
    l->addWidget(label(tr("Two hotkeys make TrackHat much easier to live with. Press <b>Set</b>, then press the key "
                          "or button you want to use.")));

    auto add_row = [&](const QString& title, const QString& text, int which) -> QLabel* {
        auto* card = new QWidget;
        card->setObjectName("wizardCard");
        card->setAttribute(Qt::WA_StyledBackground);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 10, 14, 12);
        auto* top = new QHBoxLayout;
        auto* t = label(QStringLiteral("<b>%1</b>").arg(title));
        auto* key = new QLabel;
        key->setObjectName("wizardKey");
        key->setMinimumWidth(130);
        key->setAlignment(Qt::AlignCenter);
        auto* set = new QPushButton(tr("Set..."));
        auto* clear = new QPushButton(tr("Clear"));
        top->addWidget(t, 1);
        top->addWidget(key);
        top->addWidget(set);
        top->addWidget(clear);
        cl->addLayout(top);
        cl->addWidget(label(text, "wizardMuted"));
        l->addWidget(card);
        connect(set, &QPushButton::clicked, this, [this, which] { bind(which); });
        connect(clear, &QPushButton::clicked, this, [this, which] {
            key_capture::clear(which == 0 ? main.key_center1 : main.key_toggle1);
            main.b->save();
            refresh_hotkey_labels();
        });
        return key;
    };
    center_key_label = add_row(tr("Center"),
        tr("Sets \"looking straight ahead\" to wherever your head is now. <b>Press it whenever you sit differently</b>, "
           "lean back in your chair, or move the sensor. Otherwise your view slowly ends up pointing off to one side."), 0);
    toggle_key_label = add_row(tr("Pause / resume"),
        tr("Freezes the view where it is, so you can look at your keyboard, phone or another screen without the "
           "game view moving. Press it again to carry on."), 1);

    l->addWidget(label(tr("Buttons on a <b>controller, gamepad, joystick, wheel or button box</b> work too: "
                          "press the button when asked instead of a key."), "wizardMuted"));
    l->addWidget(label(tr("You can skip this and set hotkeys later in Options > Shortcuts."), "wizardMuted"));
    l->addStretch(1);
    refresh_hotkey_labels();
    pages->addWidget(w);
}

void setup_wizard::build_presets(bool rotation)
{
    const int k = rotation ? 1 : 0;
    detect_curves(rotation);

    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setSpacing(9);
    if (rotation)
    {
        l->addWidget(label(tr("Turning: yaw, pitch and roll"), "wizardHeading"));
        l->addWidget(label(tr("This sets how far the game view turns when you turn your head. With <b>1:1</b> the view "
                              "turns exactly as much as your head. The other presets turn it further, so a small head "
                              "turn looks a long way round while your eyes stay on the screen.")));
    }
    else
    {
        l->addWidget(label(tr("Moving: left/right, up/down and forward/back"), "wizardHeading"));
        l->addWidget(label(tr("This sets how far the game view moves when you lean or move your head, for example to "
                              "lean round a pillar or closer to an instrument. <b>1:1</b> moves it exactly as far as "
                              "your head moves; the other presets move it further.")));
    }

    auto* pic = new QLabel;
    pic->setPixmap(QPixmap(rotation ? QStringLiteral(":/images/setup-axes-rotation.png")
                                    : QStringLiteral(":/images/setup-axes-position.png")));
    pic->setAlignment(Qt::AlignCenter);
    l->addWidget(pic);

    auto* row = new QHBoxLayout;
    auto* hint = label(tr("Look at the screen and press <b>Center</b>, then %1 and move the slider. "
                          "The <b>test window</b> shows the result.")
                       .arg(rotation ? tr("turn your head") : tr("lean and move your head")));
    auto* center = new QPushButton(tr("Center"));
    center->setToolTip(tr("Look straight at the screen, then press this."));
    auto* test = new QPushButton(tr("Show test window"));
    row->addWidget(hint, 1);
    row->addWidget(center);
    row->addWidget(test);
    l->addLayout(row);
    connect(center, &QPushButton::clicked, this, [this] { if (h.center) h.center(); });
    connect(test, &QPushButton::clicked, this, [this] { if (h.show_test_window) h.show_test_window(); });

    auto* card = new QWidget;
    card->setObjectName("wizardCard");
    card->setAttribute(Qt::WA_StyledBackground);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(14, 8, 14, 10);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Preset:")));
    preset_name[k] = label({}, "wizardHeading");
    top->addWidget(preset_name[k], 1);
    cl->addLayout(top);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, int(presets().size()) - 1);
    slider->setPageStep(1);
    slider->setValue(std::max(sel_preset[k], 0));
    preset_slider[k] = slider;
    cl->addWidget(slider);
    // labels under the slider stops: the end ones take half a cell, so the middle ones centre on their stop
    auto* ticks = new QHBoxLayout;
    ticks->setSpacing(0);
    for (int i = 0; i < int(presets().size()); i++)
    {
        const bool first = i == 0, last = i == int(presets().size()) - 1;
        auto* t = new QLabel(tr(presets()[i].name));
        t->setObjectName("wizardMuted");
        t->setAlignment(first ? Qt::AlignLeft : last ? Qt::AlignRight : Qt::AlignHCenter);
        ticks->addWidget(t, first || last ? 1 : 2);
    }
    cl->addLayout(ticks);
    preview[k] = new curve_preview(card);
    cl->addWidget(preview[k], 1);
    preset_note[k] = label(rotation ? tr("Shown: yaw. Pitch and roll get the same curve, cut off at their own range.")
                                    : tr("Shown: left/right. Up/down and forward/back get the same curve."), "wizardMuted");
    cl->addWidget(preset_note[k]);
    l->addWidget(card, 1);

    if (!rotation)
        l->addWidget(label(tr("These presets are only a starting point. You can fine-tune every axis later with "
                              "<b>Mapping</b> (the curve editor) and check it with <b>Test head tracking</b>.")));

    connect(slider, &QSlider::valueChanged, this, [this, rotation, k](int v) {
        sel_preset[k] = v;
        apply_curves(rotation);
    });
    refresh_curve_views(rotation);
    pages->addWidget(w);
}

void setup_wizard::build_deadzone()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setSpacing(9);
    l->addWidget(label(tr("Deadzones"), "wizardHeading"));
    l->addWidget(label(tr("A deadzone is a small area around the center where head movement doesn't move the game "
                          "view, so the view stays still while your eyes look around the screen. "
                          "<b>Most people prefer a small deadzone on both.</b>")));
    l->addWidget(label(tr("Hold your head still and look around the screen, then make small head movements, and "
                          "compare in the test window."), "wizardMuted"));

    for (int k : { 1, 0 })
    {
        const bool rotation = k == 1;
        auto* card = new QWidget;
        card->setObjectName("wizardCard");
        card->setAttribute(Qt::WA_StyledBackground);
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(14, 8, 14, 10);
        cl->setSpacing(16);
        auto* left = new QVBoxLayout;
        left->addWidget(label(rotation ? tr("<b>Turning</b> (yaw, pitch, roll)") : tr("<b>Moving</b> (left/right, up/down, forward/back)")));
        dz_name[k] = label({}, "wizardHeading");
        left->addWidget(dz_name[k]);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, int(deadzones().size()) - 1);
        slider->setPageStep(1);
        slider->setValue(sel_dz[k]);
        dz_slider[k] = slider;
        left->addWidget(slider);
        auto* ticks = new QHBoxLayout;
        ticks->setSpacing(0);
        for (int i = 0; i < int(deadzones().size()); i++)
        {
            const bool first = i == 0, last = i == int(deadzones().size()) - 1;
            auto* t = new QLabel(i == 0 ? tr("None") : QStringLiteral("%1%").arg(deadzones()[i]));
            t->setObjectName("wizardMuted");
            t->setAlignment(first ? Qt::AlignLeft : last ? Qt::AlignRight : Qt::AlignHCenter);
            ticks->addWidget(t, first || last ? 1 : 2);
        }
        left->addLayout(ticks);
        left->addStretch(1);
        cl->addLayout(left, 1);
        dz_preview[k] = new curve_preview(card);
        dz_preview[k]->setMinimumSize(330, 150);
        cl->addWidget(dz_preview[k], 1);
        l->addWidget(card, 1);
        connect(slider, &QSlider::valueChanged, this, [this, rotation, k](int v) {
            sel_dz[k] = v;
            dz_known[k] = true;
            apply_curves(rotation);
        });
    }
    l->addWidget(label(tr("The deadzone is a share of where the curve bends, so it suits the preset you picked. "
                          "You can change it later in <b>Mapping</b>."), "wizardMuted"));
    refresh_curve_views(true);
    refresh_curve_views(false);
    pages->addWidget(w);
}

// ---------------------------------------------------------------------------------------------
// navigation

void setup_wizard::go_to(int p)
{
    const int prev = pages->currentIndex();
    if (prev == p_calibration && p != p_calibration && calibrating)
        calibration_stop(false);

    pages->setCurrentIndex(p);
    if (p == p_sensor)
    {
        refresh_sensor_status();
        sensor_timer.start();
    }
    else
        sensor_timer.stop();

    if (p == p_rotation || p == p_position || p == p_deadzone)
    {
        if (ensure_tracking() && h.show_test_window)
            h.show_test_window();
        raise();
        activateWindow();
    }
    // a profile without curves gets 1:1 written (opentrack's empty X/Y/Z curve is 30 -> 75 cm, not 1:1)
    if (p == p_rotation || p == p_position)
    {
        const int k = p == p_rotation ? 1 : 0;
        if (own_empty[k])
        {
            own_empty[k] = false;
            apply_curves(k == 1);
        }
    }
    // the deadzone step starts on 10% (small) unless the profile already had one of ours
    if (p == p_deadzone)
        for (int k : { 1, 0 })
            if (sel_preset[k] >= 0 && !dz_known[k])
            {
                dz_known[k] = true;
                dz_slider[k]->setValue(1);      // applies through valueChanged
                if (sel_dz[k] != 1) { sel_dz[k] = 1; apply_curves(k == 1); }
            }
    update_nav();
}

void setup_wizard::update_nav()
{
    const int p = pages->currentIndex();
    nav->setVisible(p != p_welcome);
    step_label->setVisible(p != p_welcome);
    step_label->setText(tr("Step %1 of %2").arg(p).arg(int(page_count) - 1));
    back_button->setEnabled(p > p_sensor);
    if (p == p_deadzone)
        next_button->setText(tr("Finish"));
    else if (p == p_calibration && !calibrated)
        next_button->setText(tr("Skip"));
    else if (p == p_hotkeys && main.key_center1.keycode->isEmpty() && main.key_center1.guid->isEmpty()
             && main.key_toggle1.keycode->isEmpty() && main.key_toggle1.guid->isEmpty())
        next_button->setText(tr("Skip"));
    else
        next_button->setText(tr("Next"));
}

void setup_wizard::next()
{
    const int p = pages->currentIndex();
    if (p == p_rotation)
        save_presets(true);
    if (p == p_position)
        save_presets(false);
    if (p == p_deadzone)
    {
        save_presets(true);
        save_presets(false);
        finish();
        return;
    }
    go_to(p + 1);
}

void setup_wizard::back()
{
    const int p = pages->currentIndex();
    if (p > p_sensor)
        go_to(p - 1);
}

void setup_wizard::finish()
{
    finishing = true;
    set_done(true);
    close();
}

void setup_wizard::skip_all()
{
    set_done(true);
    close();
}

void setup_wizard::closeEvent(QCloseEvent* e)
{
    if (calibrating)
        calibration_stop(false);
    // a preset that was tried but not confirmed with Next/Finish is put back
    revert_presets(true);
    revert_presets(false);
    sensor_timer.stop();
    set_done(true);
    if (started_tracking && h.stop_tracking && h.is_tracking && h.is_tracking())
        h.stop_tracking();
    started_tracking = false;
    e->accept();
    emit wizard_closed();
}

bool setup_wizard::ensure_tracking()
{
    if (h.is_tracking && h.is_tracking())
        return true;
    if (!h.start_tracking)
        return false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    step_label->setText(tr("Starting the sensor..."));
    step_label->repaint();
    h.start_tracking();
    QApplication::restoreOverrideCursor();
    const bool ok = h.is_tracking && h.is_tracking();
    started_tracking |= ok;
    return ok;
}

void setup_wizard::showEvent(QShowEvent* e)
{
    // place once; after that the window stays wherever the user moves it
    if (!placed)
    {
        placed = true;
        place_initially();
    }
    QDialog::showEvent(e);
}

void setup_wizard::place_initially()
{
    // the test window takes the right of the screen (head_test_window::show_beside); start on the left
    QScreen* s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect avail = s->availableGeometry();
    move(avail.left() + avail.width() / 40, avail.top() + std::max(0, (avail.height() - height()) / 2));
}

// ---------------------------------------------------------------------------------------------
// step 1

void setup_wizard::refresh_sensor_status()
{
    bool found = false;
    for (const QString& name : video::camera_names())
        if (name.contains(QStringLiteral("TrackHat sensor"), Qt::CaseInsensitive))
            found = true;
    sensor_status->setObjectName(found ? "wizardOk" : "wizardWarn");
    sensor_status->setText(found ? tr("TrackHat sensor found.")
                                 : tr("No TrackHat sensor found yet. Plug it in; this updates by itself."));
    sensor_status->style()->unpolish(sensor_status);
    sensor_status->style()->polish(sensor_status);
}

// ---------------------------------------------------------------------------------------------
// step 2: head center offset
//
// With the offset at 0 the tracker reports the face position f. The neck pivot J is fixed and the
// offset o (forward, up, right in mm, in the head's own frame) satisfies J = f + R o for every sample,
// so [R  -I] [o; J] = -f is solved by least squares (6 unknowns). R is rebuilt from the tracker's
// yaw/pitch/roll: R = Ry(-yaw) Rz(pitch) Rx(-roll) inverts NeuralNetTracker::data(), and the
// tracker frame is x forward, y up, z right: f = (-TZ, TY, -TX) * 10 mm.

static void rotation_from_angles(double yaw_deg, double pitch_deg, double roll_deg, double* R)
{
    const double d = 3.14159265358979323846 / 180;
    const double a = -yaw_deg * d, b = pitch_deg * d, c = -roll_deg * d;
    const double ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
    // Ry(a) * Rz(b) * Rx(c), row major
    R[0] = ca * cb; R[1] = -ca * sb * cc + sa * sc; R[2] =  ca * sb * sc + sa * cc;
    R[3] = sb;      R[4] =  cb * cc;                R[5] = -cb * sc;
    R[6] = -sa * cb; R[7] = sa * sb * cc + ca * sc; R[8] = -sa * sb * sc + ca * cc;
}

static bool solve6(double A[6][6], double b[6], double x[6])
{
    for (int c = 0; c < 6; c++)
    {
        int piv = c;
        for (int r = c + 1; r < 6; r++)
            if (std::fabs(A[r][c]) > std::fabs(A[piv][c]))
                piv = r;
        if (std::fabs(A[piv][c]) < 1e-9)
            return false;
        std::swap(A[piv], A[c]);
        std::swap(b[piv], b[c]);
        for (int r = c + 1; r < 6; r++)
        {
            const double f = A[r][c] / A[c][c];
            for (int k = c; k < 6; k++)
                A[r][k] -= f * A[c][k];
            b[r] -= f * b[c];
        }
    }
    for (int r = 5; r >= 0; r--)
    {
        double s = b[r];
        for (int k = r + 1; k < 6; k++)
            s -= A[r][k] * x[k];
        x[r] = s / A[r][r];
    }
    return true;
}

void setup_wizard::calibration_start()
{
    calib_result->clear();
    calib_status->setText(tr("Starting the sensor..."));
    calib_status->repaint();
    if (!ensure_tracking())
    {
        calib_status->setText(tr("Tracking didn't start. Check that the sensor is plugged in, then try again."));
        update_nav();
        return;
    }
    saved_off[0] = *off_fwd; saved_off[1] = *off_up; saved_off[2] = *off_right;
    off_fwd = 0; off_up = 0; off_right = 0;     // the tracker must run without an offset while measuring
    samples.clear();
    yaw_min = pitch_min = 1e9;
    yaw_max = pitch_max = -1e9;
    yaw_bar->setValue(0);
    pitch_bar->setValue(0);
    calibrating = true;
    calib_button->setText(tr("Stop"));
    calib_status->setText(tr("Turn your head slowly left and right..."));
    calib_timer.start();
    update_nav();
}

void setup_wizard::calibration_step()
{
    double raw[6];
    if (!h.raw_pose || !h.raw_pose(raw))
    {
        calibration_stop(false);
        calib_status->setText(tr("Tracking stopped. Press Start calibration to try again."));
        return;
    }
    for (double v : raw)
        if (!std::isfinite(v))
            return;                              // no face this frame
    if (raw[0] == 0 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0 && raw[4] == 0 && raw[5] == 0)
        return;

    sample s;
    rotation_from_angles(raw[Yaw], raw[Pitch], raw[Roll], s.R);
    s.f[0] = -raw[TZ] * 10; s.f[1] = raw[TY] * 10; s.f[2] = -raw[TX] * 10;
    if (samples.size() < calib_max_samples)
        samples.push_back(s);

    yaw_min = std::min(yaw_min, raw[Yaw]); yaw_max = std::max(yaw_max, raw[Yaw]);
    pitch_min = std::min(pitch_min, raw[Pitch]); pitch_max = std::max(pitch_max, raw[Pitch]);
    const int yaw_pct = int(std::clamp((yaw_max - yaw_min) / calib_yaw_span, 0., 1.) * 100);
    const int pitch_pct = int(std::clamp((pitch_max - pitch_min) / calib_pitch_span, 0., 1.) * 100);
    yaw_bar->setValue(yaw_pct);
    pitch_bar->setValue(pitch_pct);
    if (yaw_pct < 100)
        calib_status->setText(tr("Turn your head slowly left and right..."));
    else if (pitch_pct < 100)
        calib_status->setText(tr("Good. Now nod slowly up and down..."));
    else if (samples.size() >= 90)
        calibration_stop(true);
}

void setup_wizard::calibration_stop(bool keep_result)
{
    calib_timer.stop();
    calibrating = false;
    calib_button->setText(tr("Start calibration"));

    bool ok = false;
    double x[6] {};
    double rms = 0;
    if (keep_result && samples.size() >= 30)
    {
        double A[6][6] {}, b[6] {};
        for (const sample& s : samples)
            for (int r = 0; r < 3; r++)
            {
                double row[6] = { s.R[r * 3 + 0], s.R[r * 3 + 1], s.R[r * 3 + 2], 0, 0, 0 };
                row[3 + r] = -1;
                const double rhs = -s.f[r];
                for (int i = 0; i < 6; i++)
                {
                    for (int j = 0; j < 6; j++)
                        A[i][j] += row[i] * row[j];
                    b[i] += row[i] * rhs;
                }
            }
        if (solve6(A, b, x))
        {
            double sum = 0;
            for (const sample& s : samples)
                for (int r = 0; r < 3; r++)
                {
                    const double e = s.f[r] + s.R[r * 3] * x[0] + s.R[r * 3 + 1] * x[1] + s.R[r * 3 + 2] * x[2] - x[3 + r];
                    sum += e * e;
                }
            rms = std::sqrt(sum / double(samples.size()));
            // plausible for a human head, and the pivot stays put
            ok = x[0] > -250 && x[0] < 100 && x[1] > -150 && x[1] < 100 && std::fabs(x[2]) < 60 && rms < 25;
        }
    }

    if (ok)
    {
        off_fwd = int(std::lround(x[0]));
        off_up = int(std::lround(x[1]));
        off_right = int(std::lround(x[2]));
        nn_bundle->save();
        calibrated = true;
        calib_status->setText(tr("Done: %1 samples.").arg(samples.size()));
        calib_result->setObjectName("wizardOk");
        calib_result->setText(tr("Head center: forward %1 mm, up %2 mm, right %3 mm.")
                                  .arg(*off_fwd).arg(*off_up).arg(*off_right));
    }
    else
    {
        calibration_restore();
        if (keep_result)
        {
            calib_result->setObjectName("wizardWarn");
            calib_result->setText(tr("That didn't give a reliable result (the shoulders may have moved). "
                                     "Try again, or use the default offset."));
        }
        else
            calib_status->setText(tr("Stopped. Your previous offset is kept."));
    }
    calib_result->style()->unpolish(calib_result);
    calib_result->style()->polish(calib_result);
    samples.clear();
    update_nav();
}

void setup_wizard::calibration_restore()
{
    off_fwd = saved_off[0]; off_up = saved_off[1]; off_right = saved_off[2];
    calibrating = false;
}

// ---------------------------------------------------------------------------------------------
// step 3

void setup_wizard::bind(int which)
{
    key_opts& k = which == 0 ? main.key_center1 : main.key_toggle1;
    key_capture::capture(k, h.pause_keybindings);
    main.b->save();                              // the main window re-registers its shortcuts on save
    refresh_hotkey_labels();
    update_nav();
    raise();
    activateWindow();
}

void setup_wizard::refresh_hotkey_labels()
{
    center_key_label->setText(key_capture::describe(main.key_center1));
    toggle_key_label->setText(key_capture::describe(main.key_toggle1));
}

// ---------------------------------------------------------------------------------------------
// steps 4, 5

void setup_wizard::detect_curves(bool rotation)
{
    const int k = rotation ? 1 : 0;
    const int lo = rotation ? Yaw : TX, hi = rotation ? Roll : TZ;
    bool all_empty = true;
    for (int a = lo; a <= hi; a++)
        if (!(**curves[a].points).isEmpty())
            all_empty = false;
    if (all_empty)
    {
        sel_preset[k] = 0; sel_dz[k] = 0; dz_known[k] = false; own_empty[k] = true;
        return;
    }
    for (int i = 0; i < int(presets().size()); i++)
        for (int j = 0; j < int(deadzones().size()); j++)
        {
            bool all = true;
            for (int a = lo; a <= hi && all; a++)
            {
                const axis_opts& o = *main.all_axis_opts[a];
                const QList<QPointF> want = fit_curve(presets()[i], deadzones()[j], o.max_clamp_x(), o.max_clamp_y());
                const QList<QPointF> have = **curves[a].points;
                all = have.size() == want.size();
                for (int q = 0; all && q < have.size(); q++)
                    all = std::fabs(have[q].x() - want[q].x()) < 0.15 && std::fabs(have[q].y() - want[q].y()) < 0.15;
            }
            if (all)
            {
                sel_preset[k] = i; sel_dz[k] = j; dz_known[k] = true; own_empty[k] = false;
                return;
            }
        }
    sel_preset[k] = -1; sel_dz[k] = 0; dz_known[k] = true; own_empty[k] = false;   // the user's own: left alone
}

void setup_wizard::apply_curves(bool rotation)
{
    const int k = rotation ? 1 : 0;
    if (sel_preset[k] < 0)
        return;
    const int lo = rotation ? Yaw : TX, hi = rotation ? Roll : TZ;
    for (int a = lo; a <= hi; a++)
    {
        const axis_opts& o = *main.all_axis_opts[a];
        // live: the running mapping follows these values straight away
        *curves[a].points = fit_curve(presets()[sel_preset[k]], deadzones()[sel_dz[k]], o.max_clamp_x(), o.max_clamp_y());
    }
    presets_dirty[k] = true;
    refresh_curve_views(rotation);
}

void setup_wizard::refresh_curve_views(bool rotation)
{
    const int k = rotation ? 1 : 0;
    const int lo = rotation ? Yaw : TX;
    const axis_opts& o = *main.all_axis_opts[lo];
    const QString unit = rotation ? tr("degrees") : tr("cm");
    QList<QPointF> shown = **curves[lo].points;
    if (sel_preset[k] >= 0)
        shown = fit_curve(presets()[sel_preset[k]], deadzones()[sel_dz[k]], o.max_clamp_x(), o.max_clamp_y());
    if (preset_name[k])
        preset_name[k]->setText(sel_preset[k] >= 0 ? tr(presets()[sel_preset[k]].name)
                                                   : tr("Your own curves (unchanged unless you move the slider)"));
    if (preview[k])
        preview[k]->set(shown, o.max_clamp_x(), o.max_clamp_y(), unit);
    if (dz_name[k])
    {
        dz_name[k]->setText(sel_preset[k] < 0 ? tr("Your own curves: left as they are")
                            : sel_dz[k] == 0 ? tr("No deadzone")
                            : tr("%1% deadzone").arg(deadzones()[sel_dz[k]]));
        dz_slider[k]->setEnabled(sel_preset[k] >= 0);
    }
    if (dz_preview[k])
        dz_preview[k]->set(shown, o.max_clamp_x(), o.max_clamp_y(), unit);
}

void setup_wizard::save_presets(bool rotation)
{
    const int k = rotation ? 1 : 0;
    if (!presets_dirty[k])
        return;
    const int lo = rotation ? Yaw : TX, hi = rotation ? Roll : TZ;
    for (int a = lo; a <= hi; a++)
        curves[a].b->save();
    presets_dirty[k] = false;
}

void setup_wizard::revert_presets(bool rotation)
{
    const int k = rotation ? 1 : 0;
    if (!presets_dirty[k])
        return;
    const int lo = rotation ? Yaw : TX, hi = rotation ? Roll : TZ;
    for (int a = lo; a <= hi; a++)
        curves[a].b->reload();
    presets_dirty[k] = false;
}

// ---------------------------------------------------------------------------------------------

void setup_wizard::snapshot(const QString& dir)
{
    QDir().mkpath(dir);
    auto save = [&](const QString& name) {
        QApplication::processEvents();
        grab().save(QDir(dir).filePath(name + ".png"));
    };
    const auto keep = h;
    h.start_tracking = {};                       // screenshots only: don't touch the sensor
    h.show_test_window = {};
    go_to(p_welcome);      save("wizard-0-welcome");
    go_to(p_sensor);       save("wizard-1-sensor");
    go_to(p_calibration);  save("wizard-2-calibration");
    yaw_bar->setValue(100); pitch_bar->setValue(40);
    calib_status->setText(tr("Good. Now nod slowly up and down..."));
    save("wizard-2b-calibrating");
    calib_result->setObjectName("wizardOk");
    calib_result->setText(tr("Head center: forward %1 mm, up %2 mm, right %3 mm.").arg(-68).arg(-27).arg(1));
    calib_result->style()->unpolish(calib_result); calib_result->style()->polish(calib_result);
    pitch_bar->setValue(100);
    calib_status->setText(tr("Done: %1 samples.").arg(214));
    save("wizard-2c-calibrated");
    yaw_bar->setValue(0); pitch_bar->setValue(0); calib_status->clear(); calib_result->clear();
    go_to(p_hotkeys);      save("wizard-3-hotkeys");
    go_to(p_rotation);     save("wizard-4-rotation-initial");
    for (int i : { 1, 2, 3 }) { preset_slider[1]->setValue(i); save(QStringLiteral("wizard-4-rotation-preset%1").arg(i)); }
    preset_slider[1]->setValue(2);
    go_to(p_position);     save("wizard-5-position-initial");
    for (int i : { 1, 2, 3 }) { preset_slider[0]->setValue(i); save(QStringLiteral("wizard-5-position-preset%1").arg(i)); }
    preset_slider[0]->setValue(2);
    go_to(p_deadzone);     save("wizard-6-deadzone-initial");
    dz_slider[1]->setValue(3); dz_slider[0]->setValue(0); save("wizard-6-deadzone-30-none");
    revert_presets(true);
    revert_presets(false);
    h = keep;
}
