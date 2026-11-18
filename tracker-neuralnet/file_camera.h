/* Copyright (c) 2026 TrackHat
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#pragma once

#include "video/camera.hpp"

#include <chrono>

#include <QString>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

namespace neuralnet_tracker_ns
{

/** Development aid: plays a video file in place of a camera, at the file's frame rate.
 *
 * Lets the tracker run on recorded footage, e.g. to compare pose models on the same clip
 * or to test without a sensor. Enabled with the environment variable OPENTRACK_VIDEO_FILE.
 */
class file_camera final : public video::impl::camera
{
public:
    // With `loop` the file starts over at its end. Otherwise get_frame() fails from then on.
    file_camera(const QString& path, bool loop);
    ~file_camera() override;

    bool start(info& args) override;
    void stop() override;
    bool is_open() override;
    std::tuple<const video::frame&, bool> get_frame() override;
    bool show_dialog() override { return false; }

private:
    using clock = std::chrono::steady_clock;

    QString path_;
    bool loop_;
    cv::VideoCapture cap_;
    cv::Mat mat_;
    video::frame frame_;
    clock::time_point next_frame_time_ = {};
    clock::duration frame_interval_ = {};
};

} // namespace neuralnet_tracker_ns
