/* Copyright (c) 2026 TrackHat
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "file_camera.h"

#include <QFile>

#include <thread>

namespace neuralnet_tracker_ns
{

file_camera::file_camera(const QString& path, bool loop) : path_(path), loop_(loop) {}

file_camera::~file_camera()
{
    stop();
}

bool file_camera::start(info& args)
{
    stop();

    if (!cap_.open(QFile::encodeName(path_).toStdString()))
        return false;

    double fps = cap_.get(cv::CAP_PROP_FPS);
    if (!(fps >= 1. && fps <= 1000.))
        fps = args.fps > 0 ? args.fps : 30;
    frame_interval_ = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1. / fps));
    next_frame_time_ = clock::now();

    args.width = int(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    args.height = int(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    args.fps = int(fps);
    return true;
}

void file_camera::stop()
{
    cap_.release();
    mat_ = cv::Mat();
    frame_ = {};
}

bool file_camera::is_open()
{
    return cap_.isOpened();
}

std::tuple<const video::frame&, bool> file_camera::get_frame()
{
    if (!cap_.isOpened())
        return { frame_, false };

    // Keep the pace of the recording. Frames are never skipped.
    std::this_thread::sleep_until(next_frame_time_);
    next_frame_time_ = std::max(next_frame_time_ + frame_interval_, clock::now() - frame_interval_);

    if (!cap_.read(mat_))
    {
        if (!loop_ || !cap_.set(cv::CAP_PROP_POS_FRAMES, 0) || !cap_.read(mat_))
            return { frame_, false };
    }

    frame_.data = mat_.data;
    frame_.width = mat_.cols;
    frame_.height = mat_.rows;
    frame_.stride = int(mat_.step.p[0]);
    frame_.channels = mat_.channels();
    return { frame_, true };
}

} // namespace neuralnet_tracker_ns
