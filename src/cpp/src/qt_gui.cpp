#include "qt_gui.hpp"

#include "carrier_convert.hpp"
#include "gui_audio_engine.hpp"
#include "linux_pulse_env.hpp"
#include "pa_duplex.hpp"
#include "vocoder.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(__linux__)
#include <unistd.h>
#endif

#include <portaudio.h>

#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <QtCore/QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<double> load_carrier_f32(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error(std::string("open carrier: ") + path);
    }
    f.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    if (sz % sizeof(float) != 0) {
        throw std::runtime_error("carrier file size not multiple of float32");
    }
    const std::size_t n = sz / sizeof(float);
    std::vector<float> tmp(n);
    f.read(reinterpret_cast<char*>(tmp.data()), static_cast<std::streamsize>(sz));
    std::vector<double> c(n);
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = static_cast<double>(tmp[i]);
    }
    return c;
}

std::filesystem::path resolve_exe_dir(char* argv0) {
#if defined(_WIN32)
    wchar_t wbuf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, wbuf, MAX_PATH) != 0U) {
        return std::filesystem::path(wbuf).parent_path();
    }
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[static_cast<size_t>(n)] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#endif
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
    if (!ec) {
        return p.parent_path();
    }
    return std::filesystem::path(argv0).parent_path();
}

std::filesystem::path documents_livevocoder_dir() {
#if defined(_WIN32)
    {
        const std::filesystem::path shell_docs = carrier_win32_documents_folder();
        if (!shell_docs.empty()) {
            return shell_docs / "LiveVocoderCarriers";
        }
    }
    const char* up = std::getenv("USERPROFILE");
    if (up != nullptr && up[0] != '\0') {
        return std::filesystem::path(up) / "Documents" / "LiveVocoderCarriers";
    }
    const char* h = std::getenv("HOME");
    if (h != nullptr && h[0] != '\0') {
        return carrier_win32_localize_path_for_filesystem(std::filesystem::path(h) / "Documents" / "LiveVocoderCarriers");
    }
    return {};
#else
    const char* h = std::getenv("HOME");
    if (h == nullptr || h[0] == '\0') {
        return {};
    }
    return std::filesystem::path(h) / "Documents" / "LiveVocoderCarriers";
#endif
}

std::filesystem::path carrier_library_dir(const std::filesystem::path& exe_dir) {
    std::filesystem::path d = documents_livevocoder_dir();
    if (!d.empty()) {
        return d;
    }
    return exe_dir / "LiveVocoderCarriers";
}

constexpr std::uintmax_t kMinCarrierF32Bytes = sizeof(float) * 64;

bool carrier_f32_file_usable(const std::filesystem::path& p, std::error_code& ec) {
    ec.clear();
    if (!std::filesystem::is_regular_file(p, ec)) {
        return false;
    }
    if (!carrier_path_is_raw_f32(p)) {
        return false;
    }
    ec.clear();
    const std::uintmax_t sz = std::filesystem::file_size(p, ec);
    return !ec && sz >= kMinCarrierF32Bytes;
}

std::filesystem::path pick_default_carrier_f32(const std::filesystem::path& lib, const std::filesystem::path& exe_dir) {
    std::error_code ec;
    const auto lib_carrier = lib / "carrier.f32";
    if (carrier_f32_file_usable(lib_carrier, ec)) {
        return lib_carrier;
    }
    std::filesystem::path best;
    std::filesystem::file_time_type best_t{};
    bool have = false;
    ec.clear();
    if (std::filesystem::is_directory(lib, ec)) {
        for (const std::filesystem::directory_entry& ent : std::filesystem::directory_iterator(lib, ec)) {
            if (ec) {
                break;
            }
            if (!ent.is_regular_file()) {
                continue;
            }
            const std::filesystem::path& path = ent.path();
            if (!carrier_path_is_raw_f32(path)) {
                continue;
            }
            if (!carrier_f32_file_usable(path, ec)) {
                continue;
            }
            ec.clear();
            const auto wt = std::filesystem::last_write_time(path, ec);
            if (ec) {
                continue;
            }
            if (!have || wt > best_t) {
                best = path;
                best_t = wt;
                have = true;
            }
        }
    }
    if (have) {
        return best;
    }
    const auto def = exe_dir / "carrier.f32";
    ec.clear();
    if (carrier_f32_file_usable(def, ec)) {
        return def;
    }
    return {};
}

}  // namespace

/** Scrolling peak graph (mic vs output). */
class LevelGraphWidget final : public QWidget {
public:
    explicit LevelGraphWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    }

    void pushSample(float in_peak, float out_peak) {
        constexpr int kMax = 480;
        if (static_cast<int>(in_.size()) >= kMax) {
            in_.erase(in_.begin());
            out_.erase(out_.begin());
        }
        in_.push_back(in_peak);
        out_.push_back(out_peak);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter g(this);
        g.fillRect(rect(), QColor(0x12, 0x14, 0x1e));

        const int w = width();
        const int h = height();
        const int pad = 8;
        const QRect plot(pad, pad, std::max(1, w - 2 * pad), std::max(1, h - 2 * pad));

        g.setPen(QColor(0x3a, 0x3f, 0x55));
        for (int i = 0; i <= 4; ++i) {
            const int y = plot.top() + (plot.height() * i) / 4;
            g.drawLine(plot.left(), y, plot.right(), y);
        }

        auto draw_series = [&](const std::vector<float>& s, const QColor& col) {
            if (s.size() < 2) {
                return;
            }
            g.setPen(QPen(col, 2));
            const int n = static_cast<int>(s.size());
            for (int i = 1; i < n; ++i) {
                const float x0 = static_cast<float>(plot.left()) +
                                 static_cast<float>(plot.width()) * static_cast<float>(i - 1) / static_cast<float>(n - 1);
                const float x1 = static_cast<float>(plot.left()) +
                                 static_cast<float>(plot.width()) * static_cast<float>(i) / static_cast<float>(n - 1);
                const auto amp = [](float p) {
                    p = std::clamp(p, 0.f, 1.f);
                    return 20.f * std::log10(p + 1e-7f);
                };
                const float db0 = amp(s[static_cast<std::size_t>(i - 1)]);
                const float db1 = amp(s[static_cast<std::size_t>(i)]);
                const float t0 = std::clamp((db0 + 60.f) / 60.f, 0.f, 1.f);
                const float t1 = std::clamp((db1 + 60.f) / 60.f, 0.f, 1.f);
                const float y0 = static_cast<float>(plot.bottom()) - t0 * static_cast<float>(plot.height());
                const float y1 = static_cast<float>(plot.bottom()) - t1 * static_cast<float>(plot.height());
                g.drawLine(QPointF(static_cast<double>(x0), static_cast<double>(y0)),
                           QPointF(static_cast<double>(x1), static_cast<double>(y1)));
            }
        };

        draw_series(in_, QColor(0x5e, 0xe0, 0xff));
        draw_series(out_, QColor(0xff, 0x7a, 0xe8));

        g.setPen(QColor(0x98, 0xa0, 0xb8));
        g.setFont(font());
        g.drawText(plot.adjusted(4, 2, -4, -4), Qt::AlignTop | Qt::AlignLeft, QStringLiteral("Mic in (cyan) · Output (pink)"));
    }

private:
    std::vector<float> in_;
    std::vector<float> out_;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(std::vector<char> argv0_owned, const char* carrier_opt, QWidget* parent = nullptr)
        : QMainWindow(parent), argv0_owned_(std::move(argv0_owned)) {
        if (!argv0_owned_.empty()) {
            argv0_owned_.push_back('\0');
        }
        exe_dir_ = resolve_exe_dir(argv0_owned_.data());
        lib_ = carrier_library_dir(exe_dir_);
        std::error_code ec;
        std::filesystem::create_directories(lib_, ec);
        carrier_convert_audio_in_folder(lv_gui::kSampleRate, lib_);

        if (carrier_opt != nullptr && carrier_opt[0] != '\0') {
            carrier_path_ = carrier_opt;
        } else if (const char* env_c = std::getenv("LIVE_VOCODER_START_CARRIER");
                   env_c != nullptr && env_c[0] != '\0') {
            carrier_path_ = env_c;
        } else {
            const auto picked = pick_default_carrier_f32(lib_, exe_dir_);
            if (!picked.empty()) {
                carrier_path_ = picked.string();
            }
        }

        buildUi();
        refreshCarrierLabel();
        refreshStatusFoot();

        meter_timer_ = new QTimer(this);
        connect(meter_timer_, &QTimer::timeout, this, &MainWindow::onMeterTick);
        meter_timer_->start(16);

        lv_linux_ensure_pipewire_virt_mic_stack();
        lv_apply_pulse_sink_env_before_portaudio();
        PaError pa_err = Pa_Initialize();
        if (pa_err != paNoError) {
            QMessageBox::critical(this, QStringLiteral("PortAudio"), QString::fromUtf8(Pa_GetErrorText(pa_err)));
        } else {
            pa_initialized_ = true;
            pa_log_all_devices_if_requested(stderr);
        }
    }

    ~MainWindow() override {
        stopStream();
        if (pa_initialized_) {
            Pa_Terminate();
        }
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        stopStream();
        QMainWindow::closeEvent(e);
    }

private slots:
    void onMeterTick() {
        float in_pk = 0.f;
        float out_pk = 0.f;
        if (streaming_) {
            in_pk = app_.meter_in_peak.load(std::memory_order_relaxed);
            out_pk = app_.meter_out_peak.load(std::memory_order_relaxed);
            meter_hold_in_ = std::max(in_pk, meter_hold_in_ * 0.92f);
            meter_hold_out_ = std::max(out_pk, meter_hold_out_ * 0.92f);
        } else {
            meter_hold_in_ *= 0.85f;
            meter_hold_out_ *= 0.85f;
        }
        graph_->pushSample(meter_hold_in_, meter_hold_out_);

        peak_label_->setText(QStringLiteral("Peak  Mic %1  ·  Out %2")
                                 .arg(meter_hold_in_, 0, 'f', 3)
                                 .arg(meter_hold_out_, 0, 'f', 3));
    }

    void onStartClicked() {
        (void)tryStart();
    }

    void onStopClicked() {
        stopStream();
        refreshTitle();
    }

    void onMonitorToggled(bool on) {
        app_.monitor_on.store(on, std::memory_order_relaxed);
        if (streaming_) {
            syncVirtSinkSpeakerLoopback();
        }
        refreshTitle();
    }

    void onCleanMicToggled(bool on) {
        app_.clean_mic.store(on, std::memory_order_relaxed);
        if (streaming_) {
            (void)tryStart();
        }
        refreshTitle();
    }

    void onPickCarrier() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Carrier audio or .f32"), QString::fromStdString(lib_.string()),
            QStringLiteral("Audio (*.f32 *.wav *.flac *.mp3 *.ogg);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        carrier_path_ = path.toStdString();
        refreshCarrierLabel();
        if (streaming_ && !app_.clean_mic.load(std::memory_order_relaxed)) {
            (void)tryStart();
        }
    }

    void onTestBeep() {
        if (streaming_) {
            app_.test_beep_frames_left.store(lv_gui::kSampleRate / 2, std::memory_order_relaxed);
        }
    }

private:
    void buildUi() {
        auto* central = new QWidget(this);
        auto* v = new QVBoxLayout(central);

        graph_ = new LevelGraphWidget(this);
        v->addWidget(graph_, 2);

        peak_label_ = new QLabel(QStringLiteral("Peak  Mic —  ·  Out —"), this);
        v->addWidget(peak_label_);

        auto* row = new QHBoxLayout();
        btn_start_ = new QPushButton(QStringLiteral("Start"), this);
        btn_stop_ = new QPushButton(QStringLiteral("Stop"), this);
        btn_stop_->setEnabled(false);
        chk_monitor_ = new QCheckBox(QStringLiteral("Monitor"), this);
        chk_monitor_->setChecked(true);
        chk_clean_ = new QCheckBox(QStringLiteral("Clean mic"), this);
        btn_carrier_ = new QPushButton(QStringLiteral("Carrier…"), this);
        btn_beep_ = new QPushButton(QStringLiteral("Test beep"), this);

        for (QPushButton* b : {btn_start_, btn_stop_, btn_carrier_, btn_beep_}) {
            b->setAutoDefault(false);
            b->setDefault(false);
        }

        row->addWidget(btn_start_);
        row->addWidget(btn_stop_);
        row->addWidget(chk_monitor_);
        row->addWidget(chk_clean_);
        row->addWidget(btn_carrier_);
        row->addWidget(btn_beep_);
        row->addStretch(1);
        v->addLayout(row);

        carrier_label_ = new QLabel(QStringLiteral("No carrier selected"), this);
        carrier_label_->setWordWrap(true);
        v->addWidget(carrier_label_);

        status_foot_ = new QLabel(this);
        status_foot_->setWordWrap(true);
        status_foot_->setStyleSheet(QStringLiteral("color: #8890a8; font-size: 11px;"));
        v->addWidget(status_foot_);

        setCentralWidget(central);
        resize(720, 520);

        connect(btn_start_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
        connect(btn_stop_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
        connect(chk_monitor_, &QCheckBox::toggled, this, &MainWindow::onMonitorToggled);
        connect(chk_clean_, &QCheckBox::toggled, this, &MainWindow::onCleanMicToggled);
        connect(btn_carrier_, &QPushButton::clicked, this, &MainWindow::onPickCarrier);
        connect(btn_beep_, &QPushButton::clicked, this, &MainWindow::onTestBeep);
    }

    void refreshCarrierLabel() {
        if (carrier_path_.empty()) {
            carrier_label_->setText(QStringLiteral("No carrier — pick a file or use Clean mic."));
        } else {
            carrier_label_->setText(
                QStringLiteral("Carrier: %1").arg(QString::fromStdString(std::filesystem::path(carrier_path_).filename().string())));
        }
    }

    void refreshStatusFoot() {
        QString t;
        {
            const std::string pl = lv_linux_pulse_virt_mic_status_line();
            const std::string pa = pa_portaudio_virt_capture_hint();
            if (!pl.empty() && !pa.empty()) {
                t = QString::fromStdString(pl + " · " + pa);
            } else {
                t = QString::fromStdString(!pl.empty() ? pl : pa);
            }
        }
#if defined(_WIN32)
        {
            const std::string wh = pa_windows_virt_mic_route_hint();
            if (!wh.empty()) {
                if (!t.isEmpty()) {
                    t += QStringLiteral(" · ");
                }
                t += QString::fromStdString(wh);
            }
        }
#endif
        status_foot_->setText(t.isEmpty() ? QStringLiteral("PortAudio / PipeWire status unavailable.") : t);
    }

    void refreshTitle() {
        QString s = QStringLiteral("Live Vocoder — ");
        const bool clean = app_.clean_mic.load(std::memory_order_relaxed);
        const bool mon = app_.monitor_on.load(std::memory_order_relaxed);
        const bool virt = app_.pulse_virt_sink_output.load(std::memory_order_relaxed);
        if (!streaming_) {
            s += QStringLiteral("stopped");
        } else if (clean) {
            s += QStringLiteral("mic (dry)");
        } else {
            s += QStringLiteral("vocoding");
        }
        if (streaming_ && !mon) {
            s += virt ? QStringLiteral(" · to virtual sink (speakers off)") : QStringLiteral(" · muted");
        }
        setWindowTitle(s);
    }

    void syncVirtSinkSpeakerLoopback() {
        auto pulse_sink_for_monitor = [this]() -> const char* {
            const char* ps = std::getenv("PULSE_SINK");
            if (ps != nullptr && ps[0] != '\0') {
                return ps;
            }
            const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK");
            if (lv != nullptr && lv[0] != '\0') {
                return lv;
            }
#if defined(_WIN32)
            DWORD n = GetEnvironmentVariableA("PULSE_SINK", pulse_sink_buf_, sizeof(pulse_sink_buf_));
            if (n > 0U && n < sizeof(pulse_sink_buf_)) {
                return pulse_sink_buf_;
            }
            n = GetEnvironmentVariableA("LIVE_VOCODER_PULSE_SINK", pulse_sink_buf_, sizeof(pulse_sink_buf_));
            if (n > 0U && n < sizeof(pulse_sink_buf_)) {
                return pulse_sink_buf_;
            }
#endif
            return nullptr;
        };
#if defined(__linux__)
        auto sync_linux_physical_monitor_mute = [this]() {
            const bool v = app_.pulse_virt_sink_output.load(std::memory_order_relaxed);
            const bool mon = app_.monitor_on.load(std::memory_order_relaxed);
            if (v || !streaming_ || mon) {
                lv_linux_pulse_set_own_playback_muted(false);
            } else {
                lv_linux_pulse_set_own_playback_muted(true);
            }
        };
#elif defined(_WIN32)
        auto sync_linux_physical_monitor_mute = [this]() {
            lv_win32_wine_pulse_sync_monitor_mute(streaming_, app_.monitor_on.load(std::memory_order_relaxed),
                                                  app_.pulse_virt_sink_output.load(std::memory_order_relaxed));
        };
#else
        auto sync_linux_physical_monitor_mute = []() {};
#endif
        if (!streaming_) {
            lv_linux_sync_speaker_monitor_loopback(false, nullptr);
            sync_linux_physical_monitor_mute();
            return;
        }
        if (!app_.monitor_on.load(std::memory_order_relaxed)) {
            lv_linux_sync_speaker_monitor_loopback(false, nullptr);
            sync_linux_physical_monitor_mute();
            return;
        }
        if (!app_.pulse_virt_sink_output.load(std::memory_order_relaxed)) {
#if defined(__linux__)
            {
                const std::string ps_r = lv_linux_monitor_pulse_sink_base_for_loopback();
                if (!ps_r.empty()) {
                    lv_linux_sync_speaker_monitor_loopback(true, ps_r.c_str());
                } else {
                    lv_linux_sync_speaker_monitor_loopback(false, nullptr);
                }
            }
#endif
            sync_linux_physical_monitor_mute();
            return;
        }
        const char* ps = pulse_sink_for_monitor();
#if defined(__linux__)
        std::string ps_resolved;
        if (ps == nullptr || ps[0] == '\0') {
            ps_resolved = lv_linux_monitor_pulse_sink_base_for_loopback();
            if (!ps_resolved.empty()) {
                ps = ps_resolved.c_str();
            }
        }
#elif defined(_WIN32)
        std::string ps_wine_resolved;
        if (ps == nullptr || ps[0] == '\0') {
            ps_wine_resolved = lv_wine_monitor_pulse_sink_base_for_loopback();
            if (!ps_wine_resolved.empty()) {
                ps = ps_wine_resolved.c_str();
            }
        }
#endif
        if (ps == nullptr || ps[0] == '\0') {
            lv_linux_sync_speaker_monitor_loopback(false, nullptr);
            sync_linux_physical_monitor_mute();
            return;
        }
        lv_linux_sync_speaker_monitor_loopback(true, ps);
        sync_linux_physical_monitor_mute();
    }

    void stopStream() {
        if (stream_ != nullptr) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            stream_ = nullptr;
            pa_duplex_note_stream_closed();
        }
        app_.pulse_virt_sink_output.store(false, std::memory_order_relaxed);
        app_.voc.reset();
        app_.reverb.reset();
        app_.meter_in_peak.store(0.f, std::memory_order_relaxed);
        app_.meter_out_peak.store(0.f, std::memory_order_relaxed);
        app_.test_beep_frames_left.store(0, std::memory_order_relaxed);
        app_.test_beep_phase = 0.f;
        streaming_ = false;
        syncVirtSinkSpeakerLoopback();
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
    }

    bool tryStart() {
        if (!pa_initialized_) {
            QMessageBox::warning(this, QStringLiteral("PortAudio"), QStringLiteral("PortAudio failed to initialize."));
            return false;
        }

        app_.monitor_on.store(chk_monitor_->isChecked(), std::memory_order_relaxed);
        app_.clean_mic.store(chk_clean_->isChecked(), std::memory_order_relaxed);

        if (app_.clean_mic.load(std::memory_order_relaxed)) {
            stopStream();
            app_.voc.reset();
            PaError err = pa_open_livevocoder_duplex(&stream_, static_cast<double>(lv_gui::kSampleRate),
                                                     static_cast<unsigned long>(lv_gui::kHop), lv_gui::livevocoder_gui_pa_callback, &app_);
            if (err != paNoError) {
                QMessageBox::critical(this, QStringLiteral("PortAudio"), QString::fromUtf8(Pa_GetErrorText(err)));
                return false;
            }
            pa_log_stream_devices(stream_);
            app_.pulse_virt_sink_output.store(pa_duplex_output_targets_virt_sink_route(), std::memory_order_relaxed);
            err = Pa_StartStream(stream_);
            if (err != paNoError) {
                Pa_CloseStream(stream_);
                stream_ = nullptr;
                pa_duplex_note_stream_closed();
                app_.pulse_virt_sink_output.store(false, std::memory_order_relaxed);
                QMessageBox::critical(this, QStringLiteral("PortAudio"), QString::fromUtf8(Pa_GetErrorText(err)));
                return false;
            }
#if defined(__linux__)
            lv_linux_move_livevocoder_sink_input_after_pa_start();
#elif defined(_WIN32)
            lv_linux_wine_move_livevocoder_sink_input_after_pa_start();
#endif
            app_.reverb.configure(lv_gui::kSampleRate, 0.55f);
            app_.reverb_mix.store(ui_reverb_mix_, std::memory_order_relaxed);
            streaming_ = true;
            syncVirtSinkSpeakerLoopback();
            btn_start_->setEnabled(false);
            btn_stop_->setEnabled(true);
            refreshTitle();
            return true;
        }

        if (carrier_path_.empty()) {
            QMessageBox::warning(this, QStringLiteral("Live Vocoder"),
                                 QStringLiteral("Choose a carrier file or enable Clean mic."));
            return false;
        }

        std::filesystem::path cp(carrier_path_);
#if defined(_WIN32)
        cp = carrier_win32_localize_path_for_filesystem(cp);
#endif
        std::error_code fsec;
        if (!std::filesystem::is_regular_file(cp, fsec)) {
            QMessageBox::critical(this, QStringLiteral("Live Vocoder"), QStringLiteral("Carrier file is not available."));
            return false;
        }

        if (!carrier_path_is_raw_f32(cp)) {
            std::filesystem::create_directories(lib_, fsec);
            std::filesystem::path stem = cp.stem();
            if (stem.empty()) {
                stem = "carrier";
            }
            const std::filesystem::path dest = lib_ / (stem.string() + ".f32");
            std::string conv_err;
            if (!carrier_ffmpeg_to_f32(lv_gui::kSampleRate, cp, dest, conv_err)) {
                QMessageBox::critical(this, QStringLiteral("ffmpeg"),
                                      QStringLiteral("Could not convert to f32:\n%1").arg(QString::fromStdString(conv_err)));
                return false;
            }
            carrier_path_ = dest.string();
            refreshCarrierLabel();
        }

        std::vector<double> carrier;
        try {
            carrier = load_carrier_f32(carrier_path_.c_str());
        } catch (const std::exception& e) {
            QMessageBox::critical(this, QStringLiteral("Carrier"), QString::fromUtf8(e.what()));
            return false;
        }

        stopStream();
        try {
            app_.voc = std::make_unique<StreamingVocoderCpp>(std::move(carrier), lv_gui::kSampleRate, 2048, lv_gui::kHop, preset_wet_,
                                                             36, 0.62, 0.88, preset_presence_, 1800.0, 1.0);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, QStringLiteral("Vocoder"), QString::fromUtf8(e.what()));
            return false;
        }
        app_.voc->set_wet_level(preset_wet_);
        app_.voc->set_mod_presence_db(ui_clarity_db_);
        app_.reverb.configure(lv_gui::kSampleRate, 0.55f);
        app_.reverb_mix.store(ui_reverb_mix_, std::memory_order_relaxed);

        PaError err = pa_open_livevocoder_duplex(&stream_, static_cast<double>(lv_gui::kSampleRate),
                                                 static_cast<unsigned long>(lv_gui::kHop), lv_gui::livevocoder_gui_pa_callback, &app_);
        if (err != paNoError) {
            app_.voc.reset();
            QMessageBox::critical(this, QStringLiteral("PortAudio"), QString::fromUtf8(Pa_GetErrorText(err)));
            return false;
        }
        pa_log_stream_devices(stream_);
        app_.pulse_virt_sink_output.store(pa_duplex_output_targets_virt_sink_route(), std::memory_order_relaxed);
        err = Pa_StartStream(stream_);
        if (err != paNoError) {
            Pa_CloseStream(stream_);
            stream_ = nullptr;
            pa_duplex_note_stream_closed();
            app_.pulse_virt_sink_output.store(false, std::memory_order_relaxed);
            app_.voc.reset();
            QMessageBox::critical(this, QStringLiteral("PortAudio"), QString::fromUtf8(Pa_GetErrorText(err)));
            return false;
        }
#if defined(__linux__)
        lv_linux_move_livevocoder_sink_input_after_pa_start();
#elif defined(_WIN32)
        lv_linux_wine_move_livevocoder_sink_input_after_pa_start();
#endif
        streaming_ = true;
        syncVirtSinkSpeakerLoopback();
        btn_start_->setEnabled(false);
        btn_stop_->setEnabled(true);
        refreshTitle();
        return true;
    }

    std::vector<char> argv0_owned_;
    std::filesystem::path exe_dir_;
    std::filesystem::path lib_;
    std::string carrier_path_;

    lv_gui::LiveVocoderAudioApp app_;
    PaStream* stream_{nullptr};
    bool streaming_{false};
    bool pa_initialized_{false};
    char pulse_sink_buf_[256]{};

    double preset_wet_{1.0};
    double preset_presence_{4.0};
    float ui_clarity_db_{4.f};
    float ui_reverb_mix_{0.f};

    float meter_hold_in_{0.f};
    float meter_hold_out_{0.f};

    LevelGraphWidget* graph_{nullptr};
    QTimer* meter_timer_{nullptr};
    QLabel* peak_label_{nullptr};
    QLabel* carrier_label_{nullptr};
    QLabel* status_foot_{nullptr};
    QPushButton* btn_start_{nullptr};
    QPushButton* btn_stop_{nullptr};
    QPushButton* btn_carrier_{nullptr};
    QPushButton* btn_beep_{nullptr};
    QCheckBox* chk_monitor_{nullptr};
    QCheckBox* chk_clean_{nullptr};
};

#include "qt_gui.moc"

int run_qt_gui(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Live Vocoder"));
    QApplication::setApplicationDisplayName(QStringLiteral("Live Vocoder"));

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1a1d28; color: #e8e8f0; }"
        "QPushButton { padding: 8px 14px; border: 1px solid #5c5f77; border-radius: 4px; background-color: #3b3e54; "
        "color: #e8e8f0; }"
        "QPushButton:hover { background-color: #4a4d68; }"
        "QPushButton:pressed { background-color: #252838; border-color: #7a7fa0; }"
        "QPushButton:disabled { color: #666; background-color: #2a2d3a; border-color: #444; }"
        "QCheckBox { spacing: 8px; }"));

    std::vector<char> av;
    if (argv != nullptr && argv[0] != nullptr) {
        const char* p = argv[0];
        av.assign(p, p + std::strlen(p));
    }

    const char* carrier_arg = nullptr;
    if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') {
        std::error_code ec;
        if (std::filesystem::is_regular_file(argv[1], ec)) {
            carrier_arg = argv[1];
        }
    }

    MainWindow w(std::move(av), carrier_arg);
    w.show();
    return QApplication::exec();
}
