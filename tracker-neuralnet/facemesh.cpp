/* Copyright (c) 2026 TrackHat
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include "facemesh.h"
#include "facemesh_canonical.h"

#include "compat/timer.hpp"

#include <opencv2/core/quaternion.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace neuralnet_tracker_ns
{

namespace
{

// The existing pose network reports the head center and a head "radius". These constants map
// the fitted canonical face model to the same quantities, so that both models give about the
// same 3d position. They were found by a least-squares comparison of the two models on video.
const cv::Vec3f HEAD_CENTER_CM = { 0.f, -2.63f, -2.90f }; // In the canonical model's frame.
constexpr float HEAD_RADIUS_CM = 10.35f;

// Indices of the outer eye corners. The line between them gives the in-plane rotation.
constexpr int EYE_CORNER_A = 33;  // On the left in the image for an upright face
constexpr int EYE_CORNER_B = 263;

// Scale factor which maps 8 bit pixels to [0,1]. Dark images are brightened, like for the pose network.
float brightness_scale(const cv::Mat& image)
{
    const int brightness = find_input_intensity_quantile(image, 90.f);
    return brightness < 127 ? 0.9f * 0.5f / std::max(5, brightness) : 1.f / 255.f;
}

} // namespace

FaceDetector::FaceDetector(Ort::MemoryInfo& allocator_info, Ort::Session&& session)
    : session_{ std::move(session) }, allocator_{ session_, allocator_info }
{
    if (session_.GetInputCount() != 1 || session_.GetOutputCount() != 2)
        throw std::runtime_error("Invalid face detector model: expected one input and two outputs");

    input_names_.emplace_back(&*session_.GetInputNameAllocated(0, allocator_));
    for (size_t i = 0; i < 2; ++i)
    {
        output_names_.emplace_back(&*session_.GetOutputNameAllocated(i, allocator_));
        const auto shape = session_.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3 || shape[1] != NUM_ANCHORS)
            throw std::runtime_error("Invalid face detector model: unexpected output shape");
        // Per anchor: 16 numbers for box and keypoints, or one score
        (shape[2] == 16 ? box_output_ : score_output_) = i;
    }
    if (box_output_ == score_output_)
        throw std::runtime_error("Invalid face detector model: outputs not understood");

    for (const auto& s : input_names_)
        input_c_names_.push_back(s.c_str());
    for (const auto& s : output_names_)
        output_c_names_.push_back(s.c_str());

    // Anchor centers as in MediaPipe's SsdAnchorsCalculator for this model:
    // a 16x16 grid with 2 anchors per cell, then an 8x8 grid with 6.
    for (const auto& [grid, per_cell] : { std::pair{ 16, 2 }, std::pair{ 8, 6 } })
        for (int y = 0; y < grid; ++y)
            for (int x = 0; x < grid; ++x)
                for (int i = 0; i < per_cell; ++i)
                    anchors_.emplace_back((x + 0.5f) / grid, (y + 0.5f) / grid);
    assert(anchors_.size() == NUM_ANCHORS);

    input_mat_ = cv::Mat(INPUT_SIZE, INPUT_SIZE, CV_32FC3, cv::Scalar(0.f));
    const std::int64_t input_shape[4] = { 1, INPUT_SIZE, INPUT_SIZE, 3 };
    input_val_ = Ort::Value::CreateTensor<float>(allocator_info, input_mat_.ptr<float>(0), input_mat_.total() * 3, input_shape, 4);
}

std::optional<FaceDetector::Detection> FaceDetector::run(const cv::Mat& frame)
{
    if (frame.empty() || frame.channels() != 1)
        return {};

    // The model wants a square image: pad with black
    const int side = std::max(frame.cols, frame.rows);
    const cv::Point offset{ (side - frame.cols) / 2, (side - frame.rows) / 2 };
    if (square_.cols != side || square_.rows != side)
        square_ = cv::Mat(side, side, CV_8U);
    square_.setTo(0);
    frame.copyTo(square_(cv::Rect(offset, frame.size())));
    cv::resize(square_, scaled_, { INPUT_SIZE, INPUT_SIZE }, 0, 0, cv::INTER_AREA);

    // RGB in [-1,1]
    {
        const float alpha = brightness_scale(scaled_);
        const uint8_t* src = scaled_.ptr<uint8_t>(0);
        float* dst = input_mat_.ptr<float>(0);
        for (int i = 0; i < INPUT_SIZE * INPUT_SIZE; ++i)
        {
            const float v = 2.f * std::min(1.f, alpha * src[i]) - 1.f;
            dst[3 * i] = dst[3 * i + 1] = dst[3 * i + 2] = v;
        }
    }

    Timer t;
    t.start();

    try
    {
        auto outputs = session_.Run(Ort::RunOptions{ nullptr }, input_c_names_.data(), &input_val_, 1, output_c_names_.data(), output_c_names_.size());
        last_inference_time_ = t.elapsed_ms();

        const float* scores = outputs[score_output_].GetTensorData<float>();
        const int best = int(std::max_element(scores, scores + NUM_ANCHORS) - scores);
        // MediaPipe's threshold: sigmoid(score) > 0.5
        if (!(scores[best] > 0.f))
            return {};

        // Box center and size, then keypoints, all relative to the anchor in pixels of the model input.
        // The first two keypoints are the eyes, the one on the left in the image first.
        const float* r = outputs[box_output_].GetTensorData<float>() + 16 * best;
        const cv::Point2f anchor = anchors_[best];
        auto to_frame = [&](float x, float y) -> cv::Point2f {
            return { (x / INPUT_SIZE + anchor.x) * side - offset.x, (y / INPUT_SIZE + anchor.y) * side - offset.y };
        };
        const cv::Point2f center = to_frame(r[0], r[1]);
        const float size = std::max(r[2], r[3]) / INPUT_SIZE * side;
        const cv::Point2f eyes = to_frame(r[6], r[7]) - to_frame(r[4], r[5]);
        if (!(size >= 1.f))
            return {};

        return Detection{ { center.x - 0.5f * size, center.y - 0.5f * size, size, size }, std::atan2(eyes.y, eyes.x) };
    }
    catch (const Ort::Exception& e)
    {
        qDebug() << "Failed to run the face detector model: " << e.what();
        return {};
    }
}

FaceMeshEstimator::FaceMeshEstimator(Ort::MemoryInfo& allocator_info, Ort::Session&& session)
    : session_{ std::move(session) }, allocator_{ session_, allocator_info }
{
    if (session_.GetInputCount() != 1)
        throw std::runtime_error("Invalid face mesh model: expected one input");

    {
        const auto shape = session_.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        // Batch x height x width x channels
        if (shape.size() != 4 || shape[1] != INPUT_SIZE || shape[2] != INPUT_SIZE || shape[3] != 3)
            throw std::runtime_error("Invalid face mesh model: unexpected input shape");
    }

    input_names_.emplace_back(&*session_.GetInputNameAllocated(0, allocator_));

    bool have_landmarks = false, have_presence = false;
    for (size_t i = 0; i < session_.GetOutputCount(); ++i)
    {
        output_names_.emplace_back(&*session_.GetOutputNameAllocated(i, allocator_));
        const auto shape = session_.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
        size_t count = 1;
        for (auto d : shape)
            count *= d > 0 ? size_t(d) : 1; // The batch dimension is dynamic
        if (count == NUM_LANDMARKS * 3)
        {
            landmark_output_ = i;
            have_landmarks = true;
        }
        else if (count == 1 && shape.size() == 4 && !have_presence)
        {
            // The face presence logit is the 1x1x1x1 output. The other scalar is something else.
            presence_output_ = i;
            have_presence = true;
        }
    }
    if (!have_landmarks || !have_presence)
        throw std::runtime_error("Invalid face mesh model: outputs not understood");

    for (const auto& s : input_names_)
        input_c_names_.push_back(s.c_str());
    for (const auto& s : output_names_)
        output_c_names_.push_back(s.c_str());

    patch_ = cv::Mat(INPUT_SIZE, INPUT_SIZE, CV_8U, cv::Scalar(0));
    input_mat_ = cv::Mat(INPUT_SIZE, INPUT_SIZE, CV_32FC3, cv::Scalar(0.f));
    const std::int64_t input_shape[4] = { 1, INPUT_SIZE, INPUT_SIZE, 3 };
    input_val_ = Ort::Value::CreateTensor<float>(allocator_info, input_mat_.ptr<float>(0), input_mat_.total() * 3, input_shape, 4);

    landmarks_.resize(NUM_LANDMARKS);
}

std::optional<FaceMeshEstimator::Fit> FaceMeshEstimator::fit_canonical_model(const cv::Point3f* landmarks)
{
    // Umeyama, "Least-squares estimation of transformation parameters between two point patterns", 1991.
    // Weights and the choice of landmarks are those of MediaPipe's face geometry module.
    using namespace facemesh_canonical;

    double total_weight = 0;
    cv::Vec3d mean_src = {}, mean_dst = {};
    for (const auto& [index, weight] : weights)
    {
        const cv::Vec3d src{ vertices[index][0], vertices[index][1], vertices[index][2] };
        const cv::Vec3d dst{ landmarks[index].x, landmarks[index].y, landmarks[index].z };
        mean_src += weight * src;
        mean_dst += weight * dst;
        total_weight += weight;
    }
    mean_src /= total_weight;
    mean_dst /= total_weight;

    cv::Matx33d cov = cv::Matx33d::zeros();
    double var_src = 0;
    for (const auto& [index, weight] : weights)
    {
        const cv::Vec3d src = cv::Vec3d{ vertices[index][0], vertices[index][1], vertices[index][2] } - mean_src;
        const cv::Vec3d dst = cv::Vec3d{ landmarks[index].x, landmarks[index].y, landmarks[index].z } - mean_dst;
        const double w = weight / total_weight;
        cov += w * (dst * src.t());
        var_src += w * src.dot(src);
    }

    cv::Matx33d u, vt;
    cv::Matx31d d;
    cv::SVD::compute(cov, d, u, vt);

    // Guard against a reflection
    const double sign = cv::determinant(u) * cv::determinant(vt) < 0 ? -1. : 1.;
    const cv::Matx33d s = cv::Matx33d::diag(cv::Matx31d(1., 1., sign));
    const cv::Matx33d r = u * s * vt;
    const double scale = (d(0) + d(1) + sign * d(2)) / var_src;

    if (!(std::isfinite(scale) && scale > 1e-6))
        return {};

    const cv::Vec3d t = mean_dst - scale * (r * mean_src);

    return Fit{ float(scale), cv::Matx33f(r), cv::Vec3f(t) };
}

std::optional<PoseEstimator::Face> FaceMeshEstimator::run(const cv::Mat& frame, const cv::Rect2f& box)
{
    landmarks_2d_.clear();

    const float roi_size = ROI_SCALE * std::max(box.width, box.height);
    if (!(roi_size >= 8.f) || frame.empty())
        return {};
    const cv::Point2f roi_center{ box.x + 0.5f * box.width, box.y + 0.5f * box.height };

    // Patch pixel (u,v) -> frame pixel: roi_center + k * R(roll) * (u - c, v - c)
    const float k = roi_size / INPUT_SIZE;
    const float c = 0.5f * INPUT_SIZE;
    const float cs = std::cos(roll_), sn = std::sin(roll_);
    const cv::Matx23f patch_to_frame{ k * cs, -k * sn, roi_center.x - k * (cs * c - sn * c),
                                      k * sn, k * cs,  roi_center.y - k * (sn * c + cs * c) };

    cv::warpAffine(frame, patch_, patch_to_frame, { INPUT_SIZE, INPUT_SIZE },
                   (k > 1.f ? cv::INTER_AREA : cv::INTER_LINEAR) | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT, cv::Scalar(0));

    // The model wants RGB in [0,1]. The sensor image is greyscale, so all channels are the same.
    {
        const float alpha = brightness_scale(patch_);
        const uint8_t* src = patch_.ptr<uint8_t>(0);
        float* dst = input_mat_.ptr<float>(0);
        for (int i = 0; i < INPUT_SIZE * INPUT_SIZE; ++i)
        {
            const float v = std::min(1.f, alpha * src[i]);
            dst[3 * i] = dst[3 * i + 1] = dst[3 * i + 2] = v;
        }
    }

    Timer t;
    t.start();

    float presence_logit = 0;
    try
    {
        auto outputs = session_.Run(Ort::RunOptions{ nullptr }, input_c_names_.data(), &input_val_, 1, output_c_names_.data(), output_c_names_.size());
        const float* lm = outputs[landmark_output_].GetTensorData<float>();
        presence_logit = *outputs[presence_output_].GetTensorData<float>();
        for (int i = 0; i < NUM_LANDMARKS; ++i)
        {
            const float u = lm[3 * i], v = lm[3 * i + 1], w = lm[3 * i + 2];
            landmarks_[i] = { patch_to_frame(0, 0) * u + patch_to_frame(0, 1) * v + patch_to_frame(0, 2),
                              patch_to_frame(1, 0) * u + patch_to_frame(1, 1) * v + patch_to_frame(1, 2),
                              k * w }; // Depth has the same scale as x and y
        }
    }
    catch (const Ort::Exception& e)
    {
        qDebug() << "Failed to run the face mesh model: " << e.what();
        return {};
    }

    last_inference_time_ = t.elapsed_ms();

    // MediaPipe's threshold: sigmoid(logit) > 0.5
    if (!(presence_logit > 0.f))
        return {};

    const auto fit = fit_canonical_model(landmarks_.data());
    if (!fit)
        return {};

    landmarks_2d_.reserve(NUM_LANDMARKS);
    for (const auto& p : landmarks_)
        landmarks_2d_.emplace_back(p.x, p.y);

    // In-plane rotation for the next patch
    {
        const cv::Point3f d = landmarks_[EYE_CORNER_B] - landmarks_[EYE_CORNER_A];
        roll_ = std::atan2(d.y, d.x);
    }

    // Face box for the next frame: extent of the landmarks measured along the rotated axes of
    // the face, reported as an upright square around the same center. Enlarging and rotating
    // it gives the same patch MediaPipe would use.
    cv::Rect2f next_box;
    {
        const float cr = std::cos(roll_), sr = std::sin(roll_);
        float min_a = 1e9f, max_a = -1e9f, min_b = 1e9f, max_b = -1e9f;
        for (const auto& p : landmarks_2d_)
        {
            const float a = cr * p.x + sr * p.y;
            const float b = -sr * p.x + cr * p.y;
            min_a = std::min(min_a, a); max_a = std::max(max_a, a);
            min_b = std::min(min_b, b); max_b = std::max(max_b, b);
        }
        const float ca = 0.5f * (min_a + max_a), cb = 0.5f * (min_b + max_b);
        const cv::Point2f center{ cr * ca - sr * cb, sr * ca + cr * cb };
        const float size = std::max(max_a - min_a, max_b - min_b);
        next_box = { center.x - 0.5f * size, center.y - 0.5f * size, size, size };
    }

    const cv::Vec3f head_center = fit->scale * (fit->rotation * HEAD_CENTER_CM) + fit->translation;

    PoseEstimator::Face face{};
    face.rotation = cv::Quatf::createFromRotMat(fit->rotation).normalize();
    face.rotaxis_cov_tril = cv::Matx33f::zeros();
    face.box = next_box;
    face.center = { head_center[0], head_center[1] };
    face.size = fit->scale * HEAD_RADIUS_CM;
    face.center_size_cov_tril = cv::Matx33f::zeros();
    return face;
}

cv::Mat FaceMeshEstimator::last_network_input() const
{
    cv::Mat ret;
    input_mat_.convertTo(ret, CV_8U, 255.);
    return ret;
}

} // namespace neuralnet_tracker_ns
