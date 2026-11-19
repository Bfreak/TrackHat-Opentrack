/* Trackhat: first-run setup wizard (2026.1).
 *
 * Welcome -> 1 sensor placement -> 2 head center offset calibration -> 3 center / pause hotkeys
 * -> 4 rotation curve preset -> 5 position curve preset -> 6 deadzones. Shown once on first start (global setting
 * "trackhat-setup-wizard-done"), and again from Options. See docs/trackhat/SETUP-WIZARD.md. */
#pragma once

#include "logic/main-settings.hpp"
#include "options/options.hpp"

#include <QDialog>
#include <QList>
#include <QPointF>
#include <QTimer>

#include <array>
#include <functional>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QProgressBar;
class QSlider;
class QStackedWidget;
class curve_preview;

class setup_wizard final : public QDialog
{
    Q_OBJECT

public:
    struct hooks
    {
        std::function<bool()> is_tracking;
        std::function<void()> start_tracking;            // blocks while the camera opens
        std::function<void()> stop_tracking;
        std::function<bool(double* raw6)> raw_pose;      // tracker output before centering/mapping
        std::function<void()> center;
        std::function<void()> show_test_window;
        std::function<void(bool)> pause_keybindings;     // true: main window shortcuts off
    };

    explicit setup_wizard(hooks h, QWidget* parent = nullptr);
    ~setup_wizard() override;

    static bool done();
    static void set_done(bool value = true);

    // Development aid: saves every page (and the calibration states) as PNGs into `dir`.
    void snapshot(const QString& dir);

    // A curve preset: points on the 0..180 deg (or cm) scale of the owner's curves. Empty = 1:1.
    struct preset { const char* name; std::vector<QPointF> points; };
    static const std::array<preset, 4>& presets();
    static const std::array<int, 4>& deadzones();       // percent of the preset's bend
    // The preset with a deadzone, fitted to an axis: cut at max_in, capped at max_out (outputs unchanged below).
    static QList<QPointF> fit_curve(const preset& p, int deadzone_percent, double max_in, double max_out);

signals:
    void wizard_closed();

private:
    enum page { p_welcome, p_sensor, p_calibration, p_hotkeys, p_rotation, p_position, p_deadzone, page_count };

    void build_welcome();
    void build_sensor();
    void build_calibration();
    void build_hotkeys();
    void build_presets(bool rotation);
    void build_deadzone();

    void go_to(int page);
    void next();
    void back();
    void finish();
    void skip_all();
    void update_nav();
    void closeEvent(class QCloseEvent* e) override;
    void showEvent(class QShowEvent* e) override;

    bool ensure_tracking();
    void refresh_sensor_status();

    void calibration_start();
    void calibration_step();
    void calibration_stop(bool keep_result);
    void calibration_restore();

    void bind(int which);
    void refresh_hotkey_labels();

    void detect_curves(bool rotation);          // sets sel_preset / sel_dz from the profile
    void apply_curves(bool rotation);           // writes the selected preset + deadzone into the live mapping
    void refresh_curve_views(bool rotation);
    void save_presets(bool rotation);
    void revert_presets(bool rotation);
    void place_initially();

    hooks h;
    main_settings main;
    bool started_tracking = false;
    bool finishing = false;
    bool placed = false;

    QStackedWidget* pages = nullptr;
    QLabel* step_label = nullptr;
    QPushButton* back_button = nullptr;
    QPushButton* next_button = nullptr;
    QWidget* nav = nullptr;

    // step 1
    QLabel* sensor_status = nullptr;
    QTimer sensor_timer;

    // step 2
    options::bundle nn_bundle;
    options::value<int> off_fwd, off_up, off_right;
    int saved_off[3] {};
    bool calibrating = false, calibrated = false;
    QTimer calib_timer;
    QLabel* calib_status = nullptr;
    QLabel* calib_result = nullptr;
    QProgressBar* yaw_bar = nullptr;
    QProgressBar* pitch_bar = nullptr;
    QPushButton* calib_button = nullptr;
    QPushButton* calib_defaults_button = nullptr;
    struct sample { double R[9]; double f[3]; };
    std::vector<sample> samples;
    double yaw_min = 0, yaw_max = 0, pitch_min = 0, pitch_max = 0;

    // step 3
    QLabel* center_key_label = nullptr;
    QLabel* toggle_key_label = nullptr;

    // steps 4, 5, 6 ([0] position, [1] rotation)
    struct axis_curve
    {
        options::bundle b;
        std::unique_ptr<options::value<QList<QPointF>>> points;
        Axis axis;
    };
    std::array<axis_curve, 6> curves;          // TX TY TZ Yaw Pitch Roll
    bool presets_dirty[2] {};
    int sel_preset[2] {};                      // -1: the user's own curves (left alone)
    int sel_dz[2] {};
    bool dz_known[2] {};                       // the deadzone came from the profile (re-run)
    bool own_empty[2] {};                      // the profile had no curves: 1:1 is applied on the page
    QSlider* preset_slider[2] {};
    QLabel* preset_name[2] {};
    QLabel* preset_note[2] {};
    curve_preview* preview[2] {};
    QSlider* dz_slider[2] {};
    QLabel* dz_name[2] {};
    curve_preview* dz_preview[2] {};
};
